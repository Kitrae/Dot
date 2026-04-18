#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/Math/Vec3.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Dot
{

class World;
class StaticWorldGeometry;
class CharacterControllerSystem;

enum class NavigationRequestStatus
{
    Pending,
    Succeeded,
    Failed,
    Cancelled
};

enum class NavigationMoveStatus
{
    Pending,
    Moving,
    Succeeded,
    Failed,
    Cancelled
};

struct NavigationPathOptions
{
    Vec3 projectionExtent = Vec3(2.0f, 4.0f, 2.0f);
};

struct NavigationMoveOptions
{
    float speed = 0.0f;
    float stoppingDistance = 0.0f;
    Vec3 projectionExtent = Vec3::Zero();
};

struct NavigationDebugLine
{
    Vec3 start = Vec3::Zero();
    Vec3 end = Vec3::Zero();
};

class DOT_CORE_API NavigationSystem
{
public:
    NavigationSystem();
    ~NavigationSystem();

    NavigationSystem(const NavigationSystem&) = delete;
    NavigationSystem& operator=(const NavigationSystem&) = delete;

    void Shutdown();

    void SetStaticWorldGeometry(const StaticWorldGeometry* staticWorldGeometry);
    const StaticWorldGeometry* GetStaticWorldGeometry() const { return m_StaticWorldGeometry; }

    bool EnsureNavMeshUpToDate();
    bool IsAvailable() const;

    bool FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath, const NavigationPathOptions& options = {});
    uint64 RequestPathAsync(const Vec3& start, const Vec3& goal, const NavigationPathOptions& options = {});
    void CancelPathRequest(uint64 requestId);
    NavigationRequestStatus GetPathRequestStatus(uint64 requestId) const;
    bool IsPathRequestDone(uint64 requestId) const;
    bool DidPathRequestSucceed(uint64 requestId) const;
    std::vector<Vec3> GetPathRequestPath(uint64 requestId) const;

    uint64 StartMove(World& world, Entity entity, const Vec3& target, const NavigationMoveOptions& options = {});
    void StopMove(World& world, Entity entity);
    NavigationMoveStatus GetMoveStatus(uint64 moveId) const;
    bool IsMoveDone(uint64 moveId) const;
    bool DidMoveSucceed(uint64 moveId) const;
    std::vector<Vec3> GetMovePath(uint64 moveId) const;
    bool IsEntityMoving(Entity entity) const;
    bool TryGetEntityMoveStatus(Entity entity, NavigationMoveStatus& outStatus) const;

    void Update(World& world, CharacterControllerSystem& characterControllerSystem, float dt);

    std::vector<NavigationDebugLine> GetDebugLines() const;

private:
    struct BuiltNavMesh;
    struct PathRequestRecord;
    struct MoveRequestRecord;

    bool BuildNavMeshFromStaticWorld(const StaticWorldGeometry* staticWorldGeometry,
                                     std::shared_ptr<BuiltNavMesh>& outNavMesh) const;
    bool SolvePath(const BuiltNavMesh& builtNavMesh, const Vec3& start, const Vec3& goal,
                   const NavigationPathOptions& options, std::vector<Vec3>& outPath) const;
    bool SamplePosition(const BuiltNavMesh& builtNavMesh, const Vec3& target, const Vec3& projectionExtent,
                        Vec3& outPosition) const;

    uint64 CreateFailedMoveRequest(Entity entity);
    void CancelMoveRequestLocked(World& world, uint64 moveId);

    const StaticWorldGeometry* m_StaticWorldGeometry = nullptr;
    uint64 m_LastBuiltStaticWorldRevision = 0;

    mutable std::mutex m_Mutex;
    std::shared_ptr<BuiltNavMesh> m_BuiltNavMesh;
    std::unordered_map<uint64, PathRequestRecord> m_PathRequests;
    std::unordered_map<uint64, MoveRequestRecord> m_MoveRequests;
    std::unordered_map<Entity, uint64, Entity::Hash> m_EntityMoveRequests;

    std::atomic<uint64> m_NextPathRequestId{1};
    std::atomic<uint64> m_NextMoveRequestId{1};
};

} // namespace Dot
