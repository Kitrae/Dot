#include "Core/Navigation/NavigationSystem.h"

#include "Core/ECS/World.h"
#include "Core/Jobs/Job.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Map/StaticWorldGeometry.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Scene/Components.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace Dot
{

namespace
{

constexpr float kNavCellSize = 0.3f;
constexpr float kNavCellHeight = 0.2f;
constexpr float kNavAgentHeight = 1.8f;
constexpr float kNavAgentRadius = 0.35f;
constexpr float kNavAgentClimb = 0.6f;
constexpr float kDefaultTransformMoveSpeed = 4.0f;
const Vec3 kDefaultProjectionExtent(2.0f, 4.0f, 2.0f);
constexpr int kMaxPathPolys = 256;
constexpr int kMaxStraightPathPoints = 256;

struct RcHeightfieldDeleter
{
    void operator()(rcHeightfield* value) const
    {
        if (value)
            rcFreeHeightField(value);
    }
};

struct RcCompactHeightfieldDeleter
{
    void operator()(rcCompactHeightfield* value) const
    {
        if (value)
            rcFreeCompactHeightfield(value);
    }
};

struct RcContourSetDeleter
{
    void operator()(rcContourSet* value) const
    {
        if (value)
            rcFreeContourSet(value);
    }
};

struct RcPolyMeshDeleter
{
    void operator()(rcPolyMesh* value) const
    {
        if (value)
            rcFreePolyMesh(value);
    }
};

struct RcPolyMeshDetailDeleter
{
    void operator()(rcPolyMeshDetail* value) const
    {
        if (value)
            rcFreePolyMeshDetail(value);
    }
};

struct DtNavMeshDeleter
{
    void operator()(dtNavMesh* value) const
    {
        if (value)
            dtFreeNavMesh(value);
    }
};

struct DtNavMeshQueryDeleter
{
    void operator()(dtNavMeshQuery* value) const
    {
        if (value)
            dtFreeNavMeshQuery(value);
    }
};

class SilentRecastContext final : public rcContext
{
public:
    SilentRecastContext() : rcContext(false) {}
};

struct QuantizedEdgeKey
{
    int ax = 0;
    int ay = 0;
    int az = 0;
    int bx = 0;
    int by = 0;
    int bz = 0;

    bool operator==(const QuantizedEdgeKey& other) const
    {
        return ax == other.ax && ay == other.ay && az == other.az && bx == other.bx && by == other.by &&
               bz == other.bz;
    }
};

struct QuantizedEdgeKeyHash
{
    size_t operator()(const QuantizedEdgeKey& key) const
    {
        size_t seed = static_cast<size_t>(key.ax);
        auto combine = [&seed](int value)
        {
            seed ^= static_cast<size_t>(value) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };

        combine(key.ay);
        combine(key.az);
        combine(key.bx);
        combine(key.by);
        combine(key.bz);
        return seed;
    }
};

int QuantizeFloat(float value)
{
    return static_cast<int>(std::round(value * 1000.0f));
}

QuantizedEdgeKey MakeEdgeKey(const float* a, const float* b)
{
    QuantizedEdgeKey key;
    key.ax = QuantizeFloat(a[0]);
    key.ay = QuantizeFloat(a[1]);
    key.az = QuantizeFloat(a[2]);
    key.bx = QuantizeFloat(b[0]);
    key.by = QuantizeFloat(b[1]);
    key.bz = QuantizeFloat(b[2]);

    const bool shouldSwap =
        (key.bx < key.ax) || (key.bx == key.ax && key.by < key.ay) ||
        (key.bx == key.ax && key.by == key.ay && key.bz < key.az);
    if (shouldSwap)
    {
        std::swap(key.ax, key.bx);
        std::swap(key.ay, key.by);
        std::swap(key.az, key.bz);
    }

    return key;
}

Vec3 ProjectToPlaneXZ(const Vec3& value)
{
    return Vec3(value.x, 0.0f, value.z);
}

float DistanceXZ(const Vec3& a, const Vec3& b)
{
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

NavigationPathOptions ResolvePathOptions(const NavigationPathOptions& options)
{
    NavigationPathOptions resolved = options;
    if (resolved.projectionExtent.x <= 0.0f || resolved.projectionExtent.y <= 0.0f || resolved.projectionExtent.z <= 0.0f)
        resolved.projectionExtent = kDefaultProjectionExtent;
    return resolved;
}

NavigationMoveOptions ResolveMoveOptions(World& world, Entity entity, const NavigationMoveOptions& options)
{
    NavigationMoveOptions resolved = options;

    const NavAgentComponent* navAgent = world.GetComponent<NavAgentComponent>(entity);
    if (resolved.speed <= 0.0f)
    {
        if (navAgent)
            resolved.speed = navAgent->moveSpeed;
        else if (const CharacterControllerComponent* controller = world.GetComponent<CharacterControllerComponent>(entity))
            resolved.speed = controller->moveSpeed;
        else
            resolved.speed = kDefaultTransformMoveSpeed;
    }

    if (resolved.stoppingDistance <= 0.0f)
        resolved.stoppingDistance = navAgent ? navAgent->stoppingDistance : 0.1f;

    if (resolved.projectionExtent.x <= 0.0f || resolved.projectionExtent.y <= 0.0f || resolved.projectionExtent.z <= 0.0f)
        resolved.projectionExtent = navAgent ? navAgent->projectionExtent : kDefaultProjectionExtent;

    return resolved;
}

} // namespace

struct NavigationSystem::BuiltNavMesh
{
    std::unique_ptr<dtNavMesh, DtNavMeshDeleter> navMesh;
    std::vector<NavigationDebugLine> debugLines;
};

struct NavigationSystem::PathRequestRecord
{
    NavigationRequestStatus status = NavigationRequestStatus::Pending;
    std::vector<Vec3> path;
};

struct NavigationSystem::MoveRequestRecord
{
    NavigationMoveStatus status = NavigationMoveStatus::Pending;
    Entity entity = kNullEntity;
    Vec3 target = Vec3::Zero();
    NavigationMoveOptions options;
    uint64 pathRequestId = 0;
    std::vector<Vec3> path;
    size_t waypointIndex = 0;
    bool useCharacterController = false;
};

NavigationSystem::NavigationSystem() = default;

NavigationSystem::~NavigationSystem()
{
    Shutdown();
}

void NavigationSystem::Shutdown()
{
    std::scoped_lock lock(m_Mutex);
    m_BuiltNavMesh.reset();
    m_PathRequests.clear();
    m_MoveRequests.clear();
    m_EntityMoveRequests.clear();
    m_StaticWorldGeometry = nullptr;
    m_LastBuiltStaticWorldRevision = 0;
}

void NavigationSystem::SetStaticWorldGeometry(const StaticWorldGeometry* staticWorldGeometry)
{
    m_StaticWorldGeometry = staticWorldGeometry;
    m_LastBuiltStaticWorldRevision = 0;
    EnsureNavMeshUpToDate();
}

bool NavigationSystem::EnsureNavMeshUpToDate()
{
    const StaticWorldGeometry* staticWorldGeometry = m_StaticWorldGeometry;
    if (!staticWorldGeometry)
    {
        std::scoped_lock lock(m_Mutex);
        m_BuiltNavMesh.reset();
        m_LastBuiltStaticWorldRevision = 0;
        return false;
    }

    const uint64 revision = staticWorldGeometry->GetRevision();
    {
        std::scoped_lock lock(m_Mutex);
        if (m_BuiltNavMesh && revision == m_LastBuiltStaticWorldRevision)
            return true;
    }

    std::shared_ptr<BuiltNavMesh> rebuiltNavMesh;
    if (!BuildNavMeshFromStaticWorld(staticWorldGeometry, rebuiltNavMesh))
    {
        std::scoped_lock lock(m_Mutex);
        m_BuiltNavMesh.reset();
        m_LastBuiltStaticWorldRevision = revision;
        return false;
    }

    std::scoped_lock lock(m_Mutex);
    m_BuiltNavMesh = std::move(rebuiltNavMesh);
    m_LastBuiltStaticWorldRevision = revision;
    return true;
}

bool NavigationSystem::IsAvailable() const
{
    std::scoped_lock lock(m_Mutex);
    return m_BuiltNavMesh != nullptr;
}

bool NavigationSystem::FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath,
                                const NavigationPathOptions& options)
{
    outPath.clear();
    EnsureNavMeshUpToDate();

    std::shared_ptr<BuiltNavMesh> navSnapshot;
    {
        std::scoped_lock lock(m_Mutex);
        navSnapshot = m_BuiltNavMesh;
    }

    if (!navSnapshot)
        return false;

    return SolvePath(*navSnapshot, start, goal, ResolvePathOptions(options), outPath);
}

uint64 NavigationSystem::RequestPathAsync(const Vec3& start, const Vec3& goal, const NavigationPathOptions& options)
{
    EnsureNavMeshUpToDate();
    const NavigationPathOptions resolvedOptions = ResolvePathOptions(options);

    std::shared_ptr<BuiltNavMesh> navSnapshot;
    const uint64 requestId = m_NextPathRequestId.fetch_add(1, std::memory_order_relaxed);
    {
        std::scoped_lock lock(m_Mutex);
        m_PathRequests[requestId] = PathRequestRecord{};
        navSnapshot = m_BuiltNavMesh;
    }

    if (!navSnapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_PathRequests[requestId].status = NavigationRequestStatus::Failed;
        return requestId;
    }

    JobSystem::Get().Schedule(Job::CreateLambda(
        [this, requestId, navSnapshot, start, goal, resolvedOptions]()
        {
            std::vector<Vec3> path;
            const bool success = SolvePath(*navSnapshot, start, goal, resolvedOptions, path);

            std::scoped_lock lock(m_Mutex);
            auto it = m_PathRequests.find(requestId);
            if (it == m_PathRequests.end() || it->second.status == NavigationRequestStatus::Cancelled)
                return;

            it->second.status = success ? NavigationRequestStatus::Succeeded : NavigationRequestStatus::Failed;
            it->second.path = std::move(path);
        }));

    return requestId;
}

void NavigationSystem::CancelPathRequest(uint64 requestId)
{
    std::scoped_lock lock(m_Mutex);
    auto it = m_PathRequests.find(requestId);
    if (it != m_PathRequests.end() && it->second.status == NavigationRequestStatus::Pending)
        it->second.status = NavigationRequestStatus::Cancelled;
}

NavigationRequestStatus NavigationSystem::GetPathRequestStatus(uint64 requestId) const
{
    std::scoped_lock lock(m_Mutex);
    const auto it = m_PathRequests.find(requestId);
    if (it == m_PathRequests.end())
        return NavigationRequestStatus::Failed;
    return it->second.status;
}

bool NavigationSystem::IsPathRequestDone(uint64 requestId) const
{
    const NavigationRequestStatus status = GetPathRequestStatus(requestId);
    return status != NavigationRequestStatus::Pending;
}

bool NavigationSystem::DidPathRequestSucceed(uint64 requestId) const
{
    return GetPathRequestStatus(requestId) == NavigationRequestStatus::Succeeded;
}

std::vector<Vec3> NavigationSystem::GetPathRequestPath(uint64 requestId) const
{
    std::scoped_lock lock(m_Mutex);
    const auto it = m_PathRequests.find(requestId);
    if (it == m_PathRequests.end())
        return {};
    return it->second.path;
}

uint64 NavigationSystem::CreateFailedMoveRequest(Entity entity)
{
    const uint64 moveId = m_NextMoveRequestId.fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock lock(m_Mutex);
    auto& record = m_MoveRequests[moveId];
    record.status = NavigationMoveStatus::Failed;
    record.entity = entity;
    return moveId;
}

uint64 NavigationSystem::StartMove(World& world, Entity entity, const Vec3& target, const NavigationMoveOptions& options)
{
    if (!world.IsAlive(entity))
        return CreateFailedMoveRequest(entity);

    TransformComponent* transform = world.GetComponent<TransformComponent>(entity);
    if (!transform)
        return CreateFailedMoveRequest(entity);

    EnsureNavMeshUpToDate();

    const NavigationMoveOptions resolvedOptions = ResolveMoveOptions(world, entity, options);
    const bool hasCharacterController = world.HasComponent<CharacterControllerComponent>(entity);
    const uint64 pathRequestId = RequestPathAsync(transform->position, target, NavigationPathOptions{resolvedOptions.projectionExtent});
    const uint64 moveId = m_NextMoveRequestId.fetch_add(1, std::memory_order_relaxed);

    std::scoped_lock lock(m_Mutex);
    const auto existingMove = m_EntityMoveRequests.find(entity);
    if (existingMove != m_EntityMoveRequests.end())
        CancelMoveRequestLocked(world, existingMove->second);

    auto& record = m_MoveRequests[moveId];
    record.entity = entity;
    record.target = target;
    record.options = resolvedOptions;
    record.useCharacterController = hasCharacterController;
    record.pathRequestId = pathRequestId;
    record.status = NavigationMoveStatus::Pending;
    m_EntityMoveRequests[entity] = moveId;
    return moveId;
}

void NavigationSystem::StopMove(World& world, Entity entity)
{
    std::scoped_lock lock(m_Mutex);
    const auto it = m_EntityMoveRequests.find(entity);
    if (it == m_EntityMoveRequests.end())
        return;

    CancelMoveRequestLocked(world, it->second);
}

void NavigationSystem::CancelMoveRequestLocked(World& world, uint64 moveId)
{
    auto moveIt = m_MoveRequests.find(moveId);
    if (moveIt == m_MoveRequests.end())
        return;

    if (moveIt->second.pathRequestId != 0)
    {
        auto pathIt = m_PathRequests.find(moveIt->second.pathRequestId);
        if (pathIt != m_PathRequests.end() && pathIt->second.status == NavigationRequestStatus::Pending)
            pathIt->second.status = NavigationRequestStatus::Cancelled;
    }

    moveIt->second.status = NavigationMoveStatus::Cancelled;

    if (moveIt->second.entity.IsValid())
    {
        auto entityIt = m_EntityMoveRequests.find(moveIt->second.entity);
        if (entityIt != m_EntityMoveRequests.end() && entityIt->second == moveId)
            m_EntityMoveRequests.erase(entityIt);

        if (CharacterControllerComponent* controller = world.GetComponent<CharacterControllerComponent>(moveIt->second.entity))
        {
            controller->velocity.x = 0.0f;
            controller->velocity.z = 0.0f;
        }
    }
}

NavigationMoveStatus NavigationSystem::GetMoveStatus(uint64 moveId) const
{
    std::scoped_lock lock(m_Mutex);
    const auto it = m_MoveRequests.find(moveId);
    if (it == m_MoveRequests.end())
        return NavigationMoveStatus::Failed;
    return it->second.status;
}

bool NavigationSystem::IsMoveDone(uint64 moveId) const
{
    const NavigationMoveStatus status = GetMoveStatus(moveId);
    return status != NavigationMoveStatus::Pending && status != NavigationMoveStatus::Moving;
}

bool NavigationSystem::DidMoveSucceed(uint64 moveId) const
{
    return GetMoveStatus(moveId) == NavigationMoveStatus::Succeeded;
}

std::vector<Vec3> NavigationSystem::GetMovePath(uint64 moveId) const
{
    std::scoped_lock lock(m_Mutex);
    const auto it = m_MoveRequests.find(moveId);
    if (it == m_MoveRequests.end())
        return {};
    if (!it->second.path.empty())
        return it->second.path;

    const auto pathIt = m_PathRequests.find(it->second.pathRequestId);
    if (pathIt != m_PathRequests.end())
        return pathIt->second.path;
    return {};
}

bool NavigationSystem::IsEntityMoving(Entity entity) const
{
    NavigationMoveStatus status = NavigationMoveStatus::Failed;
    return TryGetEntityMoveStatus(entity, status) &&
           (status == NavigationMoveStatus::Pending || status == NavigationMoveStatus::Moving);
}

bool NavigationSystem::TryGetEntityMoveStatus(Entity entity, NavigationMoveStatus& outStatus) const
{
    std::scoped_lock lock(m_Mutex);
    const auto entityIt = m_EntityMoveRequests.find(entity);
    if (entityIt == m_EntityMoveRequests.end())
        return false;

    const auto moveIt = m_MoveRequests.find(entityIt->second);
    if (moveIt == m_MoveRequests.end())
        return false;

    outStatus = moveIt->second.status;
    return true;
}

void NavigationSystem::Update(World& world, CharacterControllerSystem& characterControllerSystem, float dt)
{
    EnsureNavMeshUpToDate();

    std::shared_ptr<BuiltNavMesh> navSnapshot;
    {
        std::scoped_lock lock(m_Mutex);
        navSnapshot = m_BuiltNavMesh;
    }

    std::vector<uint64> moveIds;
    {
        std::scoped_lock lock(m_Mutex);
        moveIds.reserve(m_MoveRequests.size());
        for (const auto& [moveId, _] : m_MoveRequests)
            moveIds.push_back(moveId);
    }

    for (uint64 moveId : moveIds)
    {
        std::scoped_lock lock(m_Mutex);
        auto moveIt = m_MoveRequests.find(moveId);
        if (moveIt == m_MoveRequests.end())
            continue;

        MoveRequestRecord& move = moveIt->second;
        if (move.status == NavigationMoveStatus::Cancelled || move.status == NavigationMoveStatus::Failed ||
            move.status == NavigationMoveStatus::Succeeded)
        {
            continue;
        }

        if (!world.IsAlive(move.entity))
        {
            move.status = NavigationMoveStatus::Cancelled;
            auto entityIt = m_EntityMoveRequests.find(move.entity);
            if (entityIt != m_EntityMoveRequests.end() && entityIt->second == moveId)
                m_EntityMoveRequests.erase(entityIt);
            continue;
        }

        auto pathIt = m_PathRequests.find(move.pathRequestId);
        if (move.status == NavigationMoveStatus::Pending)
        {
            if (pathIt == m_PathRequests.end())
            {
                move.status = NavigationMoveStatus::Failed;
                continue;
            }

            if (pathIt->second.status == NavigationRequestStatus::Pending)
                continue;

            if (pathIt->second.status != NavigationRequestStatus::Succeeded || pathIt->second.path.empty())
            {
                move.status = (pathIt->second.status == NavigationRequestStatus::Cancelled)
                                  ? NavigationMoveStatus::Cancelled
                                  : NavigationMoveStatus::Failed;
                auto entityIt = m_EntityMoveRequests.find(move.entity);
                if (entityIt != m_EntityMoveRequests.end() && entityIt->second == moveId)
                    m_EntityMoveRequests.erase(entityIt);
                continue;
            }

            move.path = pathIt->second.path;
            move.waypointIndex = 0;
            move.status = NavigationMoveStatus::Moving;
        }

        if (move.status != NavigationMoveStatus::Moving || move.path.empty() || !navSnapshot)
            continue;

        TransformComponent* transform = world.GetComponent<TransformComponent>(move.entity);
        if (!transform)
        {
            move.status = NavigationMoveStatus::Failed;
            continue;
        }

        while (move.waypointIndex < move.path.size() &&
               DistanceXZ(transform->position, move.path[move.waypointIndex]) <= std::max(0.05f, move.options.stoppingDistance))
        {
            ++move.waypointIndex;
        }

        if (move.waypointIndex >= move.path.size())
        {
            move.status = NavigationMoveStatus::Succeeded;
            auto entityIt = m_EntityMoveRequests.find(move.entity);
            if (entityIt != m_EntityMoveRequests.end() && entityIt->second == moveId)
                m_EntityMoveRequests.erase(entityIt);
            if (CharacterControllerComponent* controller = world.GetComponent<CharacterControllerComponent>(move.entity))
            {
                controller->velocity.x = 0.0f;
                controller->velocity.z = 0.0f;
            }
            continue;
        }

        const Vec3 currentWaypoint = move.path[move.waypointIndex];
        Vec3 planarDirection = ProjectToPlaneXZ(currentWaypoint - transform->position);
        if (planarDirection.LengthSquared() <= 1e-6f)
        {
            ++move.waypointIndex;
            continue;
        }
        planarDirection.Normalize();

        if (move.useCharacterController)
        {
            CharacterControllerComponent* controller = world.GetComponent<CharacterControllerComponent>(move.entity);
            if (!controller)
            {
                move.status = NavigationMoveStatus::Failed;
                continue;
            }

            const float originalSpeed = controller->moveSpeed;
            if (move.options.speed > 0.0f)
                controller->moveSpeed = move.options.speed;

            characterControllerSystem.Move(world, move.entity, planarDirection, false, false, dt);
            controller->moveSpeed = originalSpeed;
        }
        else
        {
            const float speed = move.options.speed > 0.0f ? move.options.speed : kDefaultTransformMoveSpeed;
            const float stepDistance = speed * dt;
            const float waypointDistance = DistanceXZ(transform->position, currentWaypoint);
            const float travelDistance = std::min(stepDistance, waypointDistance);

            Vec3 candidate = transform->position + planarDirection * travelDistance;
            const float queryHeight = std::max(move.options.projectionExtent.y, 0.1f);
            Vec3 sampled = candidate;
            const Vec3 sampleQuery(candidate.x, currentWaypoint.y + queryHeight, candidate.z);
            if (SamplePosition(*navSnapshot, sampleQuery, move.options.projectionExtent, sampled))
                candidate.y = sampled.y;
            else
                candidate.y = currentWaypoint.y;

            transform->position = candidate;
            transform->dirty = true;
        }

        if (DistanceXZ(transform->position, move.target) <= std::max(0.05f, move.options.stoppingDistance))
        {
            move.status = NavigationMoveStatus::Succeeded;
            auto entityIt = m_EntityMoveRequests.find(move.entity);
            if (entityIt != m_EntityMoveRequests.end() && entityIt->second == moveId)
                m_EntityMoveRequests.erase(entityIt);
            if (CharacterControllerComponent* controller = world.GetComponent<CharacterControllerComponent>(move.entity))
            {
                controller->velocity.x = 0.0f;
                controller->velocity.z = 0.0f;
            }
        }
    }
}

std::vector<NavigationDebugLine> NavigationSystem::GetDebugLines() const
{
    std::scoped_lock lock(m_Mutex);
    if (!m_BuiltNavMesh)
        return {};
    return m_BuiltNavMesh->debugLines;
}

bool NavigationSystem::BuildNavMeshFromStaticWorld(const StaticWorldGeometry* staticWorldGeometry,
                                                   std::shared_ptr<BuiltNavMesh>& outNavMesh) const
{
    outNavMesh.reset();
    if (!staticWorldGeometry || !staticWorldGeometry->HasGeometry())
        return false;

    const auto& triangles = staticWorldGeometry->GetTriangles();
    if (triangles.empty())
        return false;

    std::vector<float> verts;
    std::vector<int> indices;
    verts.reserve(triangles.size() * 9);
    indices.reserve(triangles.size() * 3);

    for (const MapCompiledTriangle& triangle : triangles)
    {
        const Vec3 triVerts[3] = {triangle.a, triangle.b, triangle.c};
        const int baseIndex = static_cast<int>(verts.size() / 3);
        for (const Vec3& vertex : triVerts)
        {
            verts.push_back(vertex.x);
            verts.push_back(vertex.y);
            verts.push_back(vertex.z);
        }
        indices.push_back(baseIndex);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
    }

    const int vertexCount = static_cast<int>(verts.size() / 3);
    const int triangleCount = static_cast<int>(indices.size() / 3);
    if (vertexCount == 0 || triangleCount == 0)
        return false;

    SilentRecastContext context;
    rcConfig config{};
    config.cs = kNavCellSize;
    config.ch = kNavCellHeight;
    config.walkableSlopeAngle = 45.0f;
    config.walkableHeight = std::max(2, static_cast<int>(std::ceil(kNavAgentHeight / config.ch)));
    config.walkableClimb = std::max(1, static_cast<int>(std::floor(kNavAgentClimb / config.ch)));
    config.walkableRadius = std::max(1, static_cast<int>(std::ceil(kNavAgentRadius / config.cs)));
    config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = rcSqr(8);
    config.mergeRegionArea = rcSqr(20);
    config.maxVertsPerPoly = 6;
    config.detailSampleDist = 6.0f * config.cs;
    config.detailSampleMaxError = 1.0f * config.ch;

    rcCalcBounds(verts.data(), vertexCount, config.bmin, config.bmax);
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    if (config.width <= 0 || config.height <= 0)
        return false;

    std::unique_ptr<rcHeightfield, RcHeightfieldDeleter> heightfield(rcAllocHeightfield());
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax,
                                             config.cs, config.ch))
    {
        return false;
    }

    std::vector<unsigned char> areas(static_cast<size_t>(triangleCount), RC_NULL_AREA);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, verts.data(), vertexCount, indices.data(), triangleCount,
                            areas.data());
    if (!rcRasterizeTriangles(&context, verts.data(), vertexCount, indices.data(), areas.data(), triangleCount, *heightfield,
                              config.walkableClimb))
    {
        return false;
    }

    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

    std::unique_ptr<rcCompactHeightfield, RcCompactHeightfieldDeleter> compactHeightfield(rcAllocCompactHeightfield());
    if (!compactHeightfield ||
        !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compactHeightfield))
    {
        return false;
    }

    if (!rcErodeWalkableArea(&context, config.walkableRadius, *compactHeightfield))
        return false;
    if (!rcBuildDistanceField(&context, *compactHeightfield))
        return false;
    if (!rcBuildRegions(&context, *compactHeightfield, 0, config.minRegionArea, config.mergeRegionArea))
        return false;

    std::unique_ptr<rcContourSet, RcContourSetDeleter> contourSet(rcAllocContourSet());
    if (!contourSet || !rcBuildContours(&context, *compactHeightfield, config.maxSimplificationError, config.maxEdgeLen,
                                        *contourSet))
    {
        return false;
    }

    std::unique_ptr<rcPolyMesh, RcPolyMeshDeleter> polyMesh(rcAllocPolyMesh());
    if (!polyMesh || !rcBuildPolyMesh(&context, *contourSet, config.maxVertsPerPoly, *polyMesh))
        return false;

    std::unique_ptr<rcPolyMeshDetail, RcPolyMeshDetailDeleter> detailMesh(rcAllocPolyMeshDetail());
    if (!detailMesh ||
        !rcBuildPolyMeshDetail(&context, *polyMesh, *compactHeightfield, config.detailSampleDist,
                               config.detailSampleMaxError, *detailMesh))
    {
        return false;
    }

    for (int polyIndex = 0; polyIndex < polyMesh->npolys; ++polyIndex)
    {
        if (polyMesh->areas[polyIndex] == RC_WALKABLE_AREA)
            polyMesh->areas[polyIndex] = 0;
        polyMesh->flags[polyIndex] = 1;
    }

    dtNavMeshCreateParams createParams{};
    createParams.verts = polyMesh->verts;
    createParams.vertCount = polyMesh->nverts;
    createParams.polys = polyMesh->polys;
    createParams.polyAreas = polyMesh->areas;
    createParams.polyFlags = polyMesh->flags;
    createParams.polyCount = polyMesh->npolys;
    createParams.nvp = polyMesh->nvp;
    createParams.detailMeshes = detailMesh->meshes;
    createParams.detailVerts = detailMesh->verts;
    createParams.detailVertsCount = detailMesh->nverts;
    createParams.detailTris = detailMesh->tris;
    createParams.detailTriCount = detailMesh->ntris;
    createParams.walkableHeight = kNavAgentHeight;
    createParams.walkableRadius = kNavAgentRadius;
    createParams.walkableClimb = kNavAgentClimb;
    rcVcopy(createParams.bmin, polyMesh->bmin);
    rcVcopy(createParams.bmax, polyMesh->bmax);
    createParams.cs = config.cs;
    createParams.ch = config.ch;
    createParams.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&createParams, &navData, &navDataSize) || !navData || navDataSize <= 0)
        return false;

    std::unique_ptr<dtNavMesh, DtNavMeshDeleter> navMesh(dtAllocNavMesh());
    if (!navMesh)
    {
        dtFree(navData);
        return false;
    }

    const dtStatus initStatus = navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(initStatus))
    {
        dtFree(navData);
        return false;
    }

    auto builtNavMesh = std::make_shared<BuiltNavMesh>();
    builtNavMesh->navMesh = std::move(navMesh);

    const dtNavMesh* navMeshView = builtNavMesh->navMesh.get();
    std::unordered_set<QuantizedEdgeKey, QuantizedEdgeKeyHash> seenEdges;
    for (int tileIndex = 0; tileIndex < navMeshView->getMaxTiles(); ++tileIndex)
    {
        const dtMeshTile* tile = navMeshView->getTile(tileIndex);
        if (!tile || !tile->header)
            continue;

        for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex)
        {
            const dtPoly& poly = tile->polys[polyIndex];
            if (poly.getType() != DT_POLYTYPE_GROUND)
                continue;

            for (uint8 edgeIndex = 0; edgeIndex < poly.vertCount; ++edgeIndex)
            {
                const uint16 vertexA = poly.verts[edgeIndex];
                const uint16 vertexB = poly.verts[(edgeIndex + 1u) % poly.vertCount];
                const float* a = &tile->verts[vertexA * 3];
                const float* b = &tile->verts[vertexB * 3];
                const QuantizedEdgeKey edgeKey = MakeEdgeKey(a, b);
                if (!seenEdges.insert(edgeKey).second)
                    continue;

                builtNavMesh->debugLines.push_back(
                    NavigationDebugLine{Vec3(a[0], a[1], a[2]), Vec3(b[0], b[1], b[2])});
            }
        }
    }

    outNavMesh = std::move(builtNavMesh);
    return true;
}

bool NavigationSystem::SolvePath(const BuiltNavMesh& builtNavMesh, const Vec3& start, const Vec3& goal,
                                 const NavigationPathOptions& options, std::vector<Vec3>& outPath) const
{
    outPath.clear();

    std::unique_ptr<dtNavMeshQuery, DtNavMeshQueryDeleter> query(dtAllocNavMeshQuery());
    if (!query)
        return false;
    if (dtStatusFailed(query->init(builtNavMesh.navMesh.get(), 2048)))
        return false;

    const dtQueryFilter filter;
    const float extents[3] = {std::max(0.1f, options.projectionExtent.x), std::max(0.1f, options.projectionExtent.y),
                              std::max(0.1f, options.projectionExtent.z)};
    const float startPos[3] = {start.x, start.y, start.z};
    const float endPos[3] = {goal.x, goal.y, goal.z};

    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    float nearestStart[3] = {};
    float nearestEnd[3] = {};
    if (dtStatusFailed(query->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart)) || startRef == 0)
        return false;
    if (dtStatusFailed(query->findNearestPoly(endPos, extents, &filter, &endRef, nearestEnd)) || endRef == 0)
        return false;

    dtPolyRef polys[kMaxPathPolys] = {};
    int polyCount = 0;
    const dtStatus pathStatus =
        query->findPath(startRef, endRef, nearestStart, nearestEnd, &filter, polys, &polyCount, kMaxPathPolys);
    if (dtStatusFailed(pathStatus) || polyCount <= 0 || polys[polyCount - 1] != endRef ||
        dtStatusDetail(pathStatus, DT_PARTIAL_RESULT))
    {
        return false;
    }

    float straightPath[kMaxStraightPathPoints * 3] = {};
    unsigned char straightFlags[kMaxStraightPathPoints] = {};
    dtPolyRef straightRefs[kMaxStraightPathPoints] = {};
    int straightPointCount = 0;
    const dtStatus straightStatus = query->findStraightPath(nearestStart, nearestEnd, polys, polyCount, straightPath,
                                                            straightFlags, straightRefs, &straightPointCount,
                                                            kMaxStraightPathPoints);
    if (dtStatusFailed(straightStatus) || straightPointCount <= 0)
        return false;

    outPath.reserve(static_cast<size_t>(straightPointCount));
    for (int pointIndex = 0; pointIndex < straightPointCount; ++pointIndex)
    {
        const float* point = &straightPath[pointIndex * 3];
        outPath.emplace_back(point[0], point[1], point[2]);
    }

    return !outPath.empty();
}

bool NavigationSystem::SamplePosition(const BuiltNavMesh& builtNavMesh, const Vec3& target, const Vec3& projectionExtent,
                                      Vec3& outPosition) const
{
    std::unique_ptr<dtNavMeshQuery, DtNavMeshQueryDeleter> query(dtAllocNavMeshQuery());
    if (!query)
        return false;
    if (dtStatusFailed(query->init(builtNavMesh.navMesh.get(), 2048)))
        return false;

    const dtQueryFilter filter;
    const float extents[3] = {std::max(0.1f, projectionExtent.x), std::max(0.1f, projectionExtent.y),
                              std::max(0.1f, projectionExtent.z)};
    const float point[3] = {target.x, target.y, target.z};

    dtPolyRef polyRef = 0;
    float nearestPoint[3] = {};
    if (dtStatusFailed(query->findNearestPoly(point, extents, &filter, &polyRef, nearestPoint)) || polyRef == 0)
        return false;

    outPosition = Vec3(nearestPoint[0], nearestPoint[1], nearestPoint[2]);
    return true;
}

} // namespace Dot
