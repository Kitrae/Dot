// =============================================================================
// Dot Engine - Script System Implementation
// =============================================================================

#include "Core/Scripting/ScriptSystem.h"

#include "Core/ECS/World.h"
#include "Core/Gameplay/HealthComponent.h"
#include "Core/Log.h"
#include "Core/Map/StaticWorldGeometry.h"
#include "Core/Math/Quat.h"
#include "Core/Math/Vec2.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Navigation/NavigationSystem.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/CollisionMath.h"
#include "Core/Physics/PhysicsSystem.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Scene/AttachmentResolver.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/PrefabSystem.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scripting/EntityProxy.h"
#include "Core/Scripting/FileWatcher.h"
#include "Core/Scripting/ScriptRuntime.h"
#include "Core/UI/UISystem.h"
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4100 4127 4189 4244 4702 4840 5054 4996 4505 26495 26451)
#endif
#include <sol/sol.hpp>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <algorithm>
#include <unordered_map>

namespace Dot
{

namespace
{

static OBB BuildScriptOBB(const TransformComponent& transform, const BoxColliderComponent& box)
{
    OBB obb;
    obb.center = Vec3(transform.position.x + box.center.x * transform.scale.x,
                      transform.position.y + box.center.y * transform.scale.y,
                      transform.position.z + box.center.z * transform.scale.z);
    obb.halfExtents = Vec3(box.size.x * 0.5f * transform.scale.x, box.size.y * 0.5f * transform.scale.y,
                           box.size.z * 0.5f * transform.scale.z);
    obb.orientation = Mat3::FromEulerDegrees(transform.rotation.x, transform.rotation.y, transform.rotation.z);
    return obb;
}

static Sphere BuildScriptSphere(const TransformComponent& transform, const SphereColliderComponent& sphereComp)
{
    Sphere sphere;
    sphere.center = Vec3(transform.position.x + sphereComp.center.x, transform.position.y + sphereComp.center.y,
                         transform.position.z + sphereComp.center.z);
    float maxScale = std::max({transform.scale.x, transform.scale.y, transform.scale.z});
    sphere.radius = sphereComp.radius * maxScale;
    return sphere;
}

static bool IsTriggerEntity(World& world, Entity entity)
{
    if (BoxColliderComponent* box = world.GetComponent<BoxColliderComponent>(entity))
    {
        if (box->isTrigger)
            return true;
    }
    if (SphereColliderComponent* sphere = world.GetComponent<SphereColliderComponent>(entity))
    {
        if (sphere->isTrigger)
            return true;
    }
    return false;
}

static bool QueryMaskIncludesLayer(uint32 queryMask, uint8 layer)
{
    return (queryMask & CollisionLayers::LayerBit(layer)) != 0;
}

static CameraComponent* FindActiveCameraComponent(World* world)
{
    if (!world)
        return nullptr;

    CameraComponent* activeCamera = nullptr;
    world->Each<CameraComponent>(
        [&activeCamera](Entity, CameraComponent& camera)
        {
            if (!activeCamera && camera.isActive)
                activeCamera = &camera;
        });
    return activeCamera;
}

static const CameraComponent* FindActiveCameraComponent(const World* world)
{
    return FindActiveCameraComponent(const_cast<World*>(world));
}

static unsigned long long PackEntity(Entity entity)
{
    return (static_cast<unsigned long long>(entity.GetGeneration()) << 32ull) |
           static_cast<unsigned long long>(entity.GetIndex());
}

static unsigned long long MakeCollisionKey(Entity a, Entity b, bool isTrigger)
{
    unsigned long long packedA = PackEntity(a);
    unsigned long long packedB = PackEntity(b);
    if (packedB < packedA)
        std::swap(packedA, packedB);

    unsigned long long key = packedA;
    key ^= packedB + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    if (isTrigger)
        key ^= 0x8000000000000000ull;
    return key;
}

struct NavigationPathHandle
{
    uint64 requestId = 0;
    NavigationSystem* navigation = nullptr;
};

struct NavigationMoveHandle
{
    uint64 moveId = 0;
    NavigationSystem* navigation = nullptr;
};

struct ScriptPerceptionOptions
{
    uint32 layerMask = CollisionLayers::kAllLayersMask;
    float maxDistance = 0.0f;
    float fieldOfViewDegrees = 90.0f;
    Vec3 eyeOffset = Vec3::Zero();
    Vec3 targetOffset = Vec3::Zero();
    bool includeTriggers = false;
    bool includeSelf = false;
};

struct ScriptRaycastResult
{
    bool hit = false;
    float distance = 0.0f;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Zero();
    Entity entity = kNullEntity;
    bool isWorld = false;
};

const char* ToLuaStatus(NavigationRequestStatus status)
{
    switch (status)
    {
        case NavigationRequestStatus::Pending:
            return "pending";
        case NavigationRequestStatus::Succeeded:
            return "succeeded";
        case NavigationRequestStatus::Failed:
            return "failed";
        case NavigationRequestStatus::Cancelled:
            return "cancelled";
        default:
            return "failed";
    }
}

const char* ToLuaStatus(NavigationMoveStatus status)
{
    switch (status)
    {
        case NavigationMoveStatus::Pending:
            return "pending";
        case NavigationMoveStatus::Moving:
            return "moving";
        case NavigationMoveStatus::Succeeded:
            return "succeeded";
        case NavigationMoveStatus::Failed:
            return "failed";
        case NavigationMoveStatus::Cancelled:
            return "cancelled";
        default:
            return "failed";
    }
}

float DegreesToRadians(float degrees)
{
    return degrees * 0.01745329251994329577f;
}

Vec3 GetEntityPosition(World& world, Entity entity)
{
    if (TransformComponent* transform = world.GetComponent<TransformComponent>(entity))
        return transform->position;
    return Vec3::Zero();
}

Vec3 GetEntityForward(World& world, Entity entity)
{
    const TransformComponent* transform = world.GetComponent<TransformComponent>(entity);
    if (!transform)
        return Vec3::Forward();

    const Vec3 radians(DegreesToRadians(transform->rotation.x), DegreesToRadians(transform->rotation.y),
                       DegreesToRadians(transform->rotation.z));
    const Quat rotation = Quat::FromEuler(radians).Normalized();
    return (rotation * Vec3(0.0f, 0.0f, 1.0f)).Normalized();
}

ScriptPerceptionOptions ParsePerceptionOptions(sol::optional<sol::table> optionsTable)
{
    ScriptPerceptionOptions options;
    if (!optionsTable)
        return options;

    sol::table table = optionsTable.value();
    if (table["layerMask"].valid())
        options.layerMask = table["layerMask"].get<uint32>();
    if (table["maxDistance"].valid())
        options.maxDistance = table["maxDistance"].get<float>();
    if (table["fieldOfView"].valid())
        options.fieldOfViewDegrees = table["fieldOfView"].get<float>();
    if (table["eyeOffset"].valid())
        options.eyeOffset = table["eyeOffset"].get<Vec3>();
    if (table["targetOffset"].valid())
        options.targetOffset = table["targetOffset"].get<Vec3>();
    if (table["includeTriggers"].valid())
        options.includeTriggers = table["includeTriggers"].get<bool>();
    if (table["includeSelf"].valid())
        options.includeSelf = table["includeSelf"].get<bool>();
    return options;
}

bool ShouldSkipScriptHit(Entity entity, const std::vector<Entity>& ignoredEntities)
{
    for (Entity ignored : ignoredEntities)
    {
        if (ignored.IsValid() && ignored == entity)
            return true;
    }
    return false;
}

bool RaycastSceneForScripts(World& world, const StaticWorldGeometry* staticWorldGeometry, const Vec3& origin,
                            const Vec3& direction, float maxDistance, uint32 queryMask, bool includeTriggers,
                            const std::vector<Entity>& ignoredEntities, ScriptRaycastResult& outHit)
{
    outHit = {};
    if (maxDistance <= 0.0f)
        return false;

    const float dirLen = direction.Length();
    if (dirLen < kVec3Epsilon)
        return false;

    Ray ray;
    ray.origin = origin;
    ray.direction = direction / dirLen;

    RaycastHit closestHit;
    closestHit.hit = false;
    closestHit.distance = maxDistance + 1.0f;
    Entity hitEntity = kNullEntity;
    bool hitWorld = false;

    world.Each<TransformComponent, BoxColliderComponent>(
        [&](Entity entity, TransformComponent& transform, BoxColliderComponent& box)
        {
            if (!QueryMaskIncludesLayer(queryMask, box.collisionLayer))
                return;
            if (!includeTriggers && box.isTrigger)
                return;
            if (ShouldSkipScriptHit(entity, ignoredEntities))
                return;

            RaycastHit thisHit;
            if (RaycastVsOBB(ray, BuildScriptOBB(transform, box), maxDistance, thisHit) && thisHit.distance < closestHit.distance)
            {
                closestHit = thisHit;
                hitEntity = entity;
                hitWorld = false;
            }
        });

    world.Each<TransformComponent, SphereColliderComponent>(
        [&](Entity entity, TransformComponent& transform, SphereColliderComponent& sphereComp)
        {
            if (!QueryMaskIncludesLayer(queryMask, sphereComp.collisionLayer))
                return;
            if (!includeTriggers && sphereComp.isTrigger)
                return;
            if (ShouldSkipScriptHit(entity, ignoredEntities))
                return;

            RaycastHit thisHit;
            if (RaycastVsSphere(ray, BuildScriptSphere(transform, sphereComp), maxDistance, thisHit) &&
                thisHit.distance < closestHit.distance)
            {
                closestHit = thisHit;
                hitEntity = entity;
                hitWorld = false;
            }
        });

    if (staticWorldGeometry && QueryMaskIncludesLayer(queryMask, 0))
    {
        StaticWorldHit worldHit;
        if (staticWorldGeometry->Raycast(ray, maxDistance, worldHit) && (!closestHit.hit || worldHit.distance < closestHit.distance))
        {
            closestHit.hit = true;
            closestHit.distance = worldHit.distance;
            closestHit.point = worldHit.point;
            closestHit.normal = worldHit.normal;
            hitEntity = kNullEntity;
            hitWorld = true;
        }
    }

    if (!closestHit.hit)
        return false;

    outHit.hit = true;
    outHit.distance = closestHit.distance;
    outHit.point = closestHit.point;
    outHit.normal = closestHit.normal;
    outHit.entity = hitEntity;
    outHit.isWorld = hitWorld;
    return true;
}

std::vector<Entity> CollectEntitiesInRadiusForScripts(World& world, const Vec3& center, float radius, uint32 queryMask,
                                                      bool includeTriggers, const std::vector<Entity>& ignoredEntities)
{
    std::vector<std::pair<float, Entity>> matches;
    std::unordered_map<unsigned long long, bool> seen;
    if (radius <= 0.0f)
        return {};

    const Sphere querySphere{center, radius};
    auto tryAddEntity = [&](Entity entity)
    {
        if (ShouldSkipScriptHit(entity, ignoredEntities))
            return;

        const unsigned long long packed = PackEntity(entity);
        if (seen.find(packed) != seen.end())
            return;

        seen[packed] = true;
        matches.emplace_back(Vec3::Distance(center, GetEntityPosition(world, entity)), entity);
    };

    world.Each<TransformComponent, BoxColliderComponent>(
        [&](Entity entity, TransformComponent& transform, BoxColliderComponent& box)
        {
            if (!QueryMaskIncludesLayer(queryMask, box.collisionLayer))
                return;
            if (!includeTriggers && box.isTrigger)
                return;

            ContactManifold manifold;
            if (OBBvsSphere(BuildScriptOBB(transform, box), querySphere, manifold) && manifold.numContacts > 0)
                tryAddEntity(entity);
        });

    world.Each<TransformComponent, SphereColliderComponent>(
        [&](Entity entity, TransformComponent& transform, SphereColliderComponent& sphereComp)
        {
            if (!QueryMaskIncludesLayer(queryMask, sphereComp.collisionLayer))
                return;
            if (!includeTriggers && sphereComp.isTrigger)
                return;

            ContactManifold manifold;
            if (SphereVsSphere(BuildScriptSphere(transform, sphereComp), querySphere, manifold) && manifold.numContacts > 0)
                tryAddEntity(entity);
        });

    std::sort(matches.begin(), matches.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Entity> entities;
    entities.reserve(matches.size());
    for (const auto& [_, entity] : matches)
        entities.push_back(entity);
    return entities;
}

} // namespace

ScriptSystem::ScriptSystem() = default;

ScriptSystem::~ScriptSystem()
{
    Shutdown();
}

bool ScriptSystem::Initialize(World* world)
{
    if (m_Initialized)
    {
        return true;
    }

    if (!world)
    {
        DOT_LOG_ERROR("ScriptSystem: Cannot initialize with null World");
        return false;
    }

    m_World = world;
    m_Runtime = std::make_unique<ScriptRuntime>();

    if (!m_Runtime->Initialize())
    {
        DOT_LOG_ERROR("ScriptSystem: Failed to initialize ScriptRuntime");
        m_Runtime.reset();
        m_World = nullptr;
        return false;
    }

    // Set console output callback
    m_Runtime->SetOutputCallback(
        [this](const std::string& msg)
        {
            if (m_ConsoleCallback)
                m_ConsoleCallback(msg);
        });

    // Register Entity bindings
    RegisterEntityBindings();

    // Initialize file watcher for hot reloading
    m_FileWatcher = std::make_unique<FileWatcher>();
    m_FileWatcher->SetCallback([this](const std::string& path) { ReloadScriptsByPath(path); });

    m_IsPlaying = false;
    m_Initialized = true;
    DOT_LOG_INFO("ScriptSystem initialized");
    return true;
}

void ScriptSystem::Shutdown()
{
    if (!m_Initialized)
        return;

    Stop();
    ClearNavigationMoveCallbacks();
    m_FileWatcher.reset();
    m_Runtime.reset();
    m_World = nullptr;
    m_IsPlaying = false;
    m_Initialized = false;
    DOT_LOG_INFO("ScriptSystem shutdown");
}

void ScriptSystem::Start()
{
    if (!m_World || !m_Runtime || m_IsPlaying)
        return;

    m_IsPlaying = true;
    m_PreviousCollisionStates.clear();
    ClearNavigationMoveCallbacks();

    // Load all scripts and call OnStart
    m_World->Each<ScriptComponent>(
        [this](Entity entity, ScriptComponent& script)
        {
            if (script.enabled && !script.scriptPath.empty())
            {
                LoadScript(entity, script);

                if (script.scriptRef >= 0 && !script.hasStarted)
                {
                    m_Runtime->CallScriptFunction(script.scriptRef, "OnStart");
                    script.hasStarted = true;
                }
            }
        });

    DOT_LOG_INFO("ScriptSystem started");
}

void ScriptSystem::Update(float deltaTime)
{
    if (!m_World || !m_Runtime || !m_IsPlaying)
        return;

    DispatchCollisionEvents();
    DispatchNavigationMoveEvents();

    m_World->Each<ScriptComponent>(
        [this, deltaTime](Entity entity, ScriptComponent& script)
        {
            (void)entity; // Suppress unused warning
            if (script.enabled && script.scriptRef >= 0)
            {
                m_Runtime->CallScriptUpdate(script.scriptRef, deltaTime);
            }
        });

    // Update timers after all script updates
    m_Runtime->UpdateTimers(deltaTime);
}

void ScriptSystem::Stop()
{
    if (!m_World || !m_Runtime || !m_IsPlaying)
        return;

    // Call OnDestroy and unload all scripts
    m_World->Each<ScriptComponent>(
        [this](Entity entity, ScriptComponent& script)
        {
            (void)entity;
            if (script.scriptRef >= 0)
            {
                m_Runtime->CallScriptFunction(script.scriptRef, "OnDestroy");
                UnloadScript(entity, script);
            }
        });

    m_IsPlaying = false;
    m_PreviousCollisionStates.clear();
    ClearNavigationMoveCallbacks();
    m_Runtime->GetState()["__uiClickCallbacks"] = m_Runtime->GetState().create_table();
    DOT_LOG_INFO("ScriptSystem stopped");
}

bool ScriptSystem::ReloadScript(Entity entity)
{
    if (!m_World || !m_Runtime)
        return false;

    ScriptComponent* script = m_World->GetComponent<ScriptComponent>(entity);
    if (!script)
        return false;

    // Unload existing
    if (script->scriptRef >= 0)
    {
        m_Runtime->CallScriptFunction(script->scriptRef, "OnDestroy");
        UnloadScript(entity, *script);
    }

    // Reload
    LoadScript(entity, *script);

    // Call OnStart if playing
    if (m_IsPlaying && script->scriptRef >= 0)
    {
        m_Runtime->CallScriptFunction(script->scriptRef, "OnStart");
        script->hasStarted = true;
    }

    return script->scriptRef >= 0;
}

void ScriptSystem::SetConsoleCallback(ScriptConsoleCallback callback)
{
    m_ConsoleCallback = callback;
    if (m_Runtime)
    {
        m_Runtime->SetOutputCallback(
            [this](const std::string& msg)
            {
                if (m_ConsoleCallback)
                    m_ConsoleCallback(msg);
            });
    }
}

void ScriptSystem::SetUISystem(UISystem* uiSystem)
{
    m_UISystem = uiSystem;

    if (!m_UISystem)
        return;

    m_UISystem->SetEventCallback(
        [this](const std::string& screenPath, const std::string& widgetId, const std::string& eventName)
        {
            if (!m_Runtime)
                return;

            sol::state& lua = m_Runtime->GetState();
            sol::table callbacks = lua["__uiClickCallbacks"];
            if (!callbacks.valid())
                return;

            const std::string key = screenPath + "|" + widgetId + "|" + eventName;
            sol::object callbackObject = callbacks[key];
            if (!callbackObject.valid() || callbackObject.get_type() != sol::type::function)
                return;

            sol::function callback = callbackObject.as<sol::function>();
            sol::protected_function_result result = callback(screenPath, widgetId, eventName);
            if (!result.valid())
            {
                sol::error err = result;
                DOT_LOG_ERROR("[Lua UI Error] %s", err.what());
                if (m_ConsoleCallback)
                    m_ConsoleCallback(std::string("[ERROR] ") + err.what());
            }
        });
}

bool ScriptSystem::ExecuteCode(const std::string& code)
{
    if (!m_Runtime)
        return false;
    return m_Runtime->ExecuteString(code);
}

float ScriptSystem::ApplyDamage(Entity target, float amount, Entity source)
{
    if (!m_World || !target.IsValid() || !m_World->IsAlive(target) || amount <= 0.0f)
        return 0.0f;

    HealthComponent* health = m_World->GetComponent<HealthComponent>(target);
    if (!health)
        return 0.0f;

    const bool wasDead = health->IsDead();
    const float beforeHealth = health->currentHealth;
    const float remainingHealth = health->ApplyDamage(amount);
    const float appliedAmount = beforeHealth - remainingHealth;

    if (appliedAmount > 0.0f)
        DispatchDamageEvent(target, appliedAmount, source);

    if (!wasDead && health->IsDead())
    {
        DispatchDeathEvent(target, source);
        if (health->destroyEntityOnDeath && m_World->IsAlive(target))
            m_World->DestroyEntity(target);
    }

    return remainingHealth;
}

float ScriptSystem::Heal(Entity target, float amount)
{
    if (!m_World || !target.IsValid() || !m_World->IsAlive(target) || amount <= 0.0f)
        return 0.0f;

    HealthComponent* health = m_World->GetComponent<HealthComponent>(target);
    if (!health)
        return 0.0f;

    return health->Heal(amount);
}

void ScriptSystem::LoadScript(Entity entity, ScriptComponent& script)
{
    if (script.scriptPath.empty())
        return;

    std::string fullPath = m_ScriptBasePath + script.scriptPath;
    script.scriptRef = m_Runtime->LoadScript(fullPath);

    if (script.scriptRef < 0)
    {
        DOT_LOG_ERROR("Failed to load script: %s", fullPath.c_str());
        return;
    }

    // Store entity reference in the script table
    sol::state& lua = m_Runtime->GetState();
    std::string tableName = "script_" + std::to_string(script.scriptRef);
    sol::table scriptTable = lua[tableName];

    if (scriptTable.valid())
    {
        // NEW: Inject 'self' as an EntityProxy for Roblox-style access
        // Scripts can now use: self.Position, self.Rotation, self.Scale, self.Name
        scriptTable["self"] = EntityProxy(entity, m_World);

        // LEGACY: Keep raw index/gen for backwards compatibility
        scriptTable["_entityIndex"] = entity.GetIndex();
        scriptTable["_entityGen"] = entity.GetGeneration();
    }

    DOT_LOG_INFO("Loaded script: %s for entity %u", script.scriptPath.c_str(), entity.GetIndex());
}

void ScriptSystem::UnloadScript(Entity entity, ScriptComponent& script)
{
    ClearTrackedNavigationMovesForEntity(entity);

    if (script.scriptRef >= 0)
    {
        m_Runtime->UnloadScript(script.scriptRef);
        script.scriptRef = -1;
        script.hasStarted = false;
    }
}

void ScriptSystem::RegisterEntityBindings()
{
    sol::state& lua = m_Runtime->GetState();
    lua["__navMoveCallbacks"] = lua.create_table();
    lua["__uiClickCallbacks"] = lua.create_table();

    auto parsePathOptions = [](sol::optional<sol::table> optionsTable) -> NavigationPathOptions
    {
        NavigationPathOptions options;
        if (!optionsTable)
            return options;

        sol::table table = optionsTable.value();
        if (table["projectionExtent"].valid())
            options.projectionExtent = table["projectionExtent"].get<Vec3>();
        return options;
    };

    auto registerTrackedMove = [this](Entity entity, uint64 moveId, sol::optional<sol::function> callback)
    {
        if (moveId == 0)
            return;

        m_TrackedNavigationMoves[moveId] = TrackedNavigationMove{entity};
        if (callback)
        {
            sol::state& luaState = m_Runtime->GetState();
            sol::table callbacks = luaState["__navMoveCallbacks"];
            callbacks[moveId] = callback.value();
        }
    };

    auto startMove = [this, registerTrackedMove](EntityProxy& proxy, const Vec3& target, const NavigationMoveOptions& options,
                                                 sol::optional<sol::function> callback) -> NavigationMoveHandle
    {
        if (!proxy.IsValid() || !m_NavigationSystem || !m_World)
            return {};

        NavigationMoveHandle handle;
        handle.navigation = m_NavigationSystem;
        handle.moveId = m_NavigationSystem->StartMove(*m_World, proxy.GetEntity(), target, options);
        registerTrackedMove(proxy.GetEntity(), handle.moveId, callback);
        return handle;
    };

    auto parseMoveOptions = [](sol::optional<sol::table> optionsTable) -> NavigationMoveOptions
    {
        NavigationMoveOptions options;
        if (!optionsTable)
            return options;

        sol::table table = optionsTable.value();
        if (table["speed"].valid())
            options.speed = table["speed"].get<float>();
        if (table["stoppingDistance"].valid())
            options.stoppingDistance = table["stoppingDistance"].get<float>();
        if (table["projectionExtent"].valid())
            options.projectionExtent = table["projectionExtent"].get<Vec3>();
        return options;
    };

    auto parseMoveOptionsTable = [parseMoveOptions](const sol::table& optionsTable) -> NavigationMoveOptions
    {
        return parseMoveOptions(sol::optional<sol::table>(optionsTable));
    };

    auto buildEntityProxyTable = [this](const std::vector<Entity>& entities) -> sol::table
    {
        sol::state& state = m_Runtime->GetState();
        sol::table result = state.create_table();
        for (size_t i = 0; i < entities.size(); ++i)
            result[static_cast<int>(i + 1)] = EntityProxy(entities[i], m_World);
        return result;
    };

    auto getAttachmentBinding = [](const EntityProxy& proxy) -> AttachmentBindingComponent*
    {
        if (!proxy.IsValid())
            return nullptr;
        return proxy.world->GetComponent<AttachmentBindingComponent>(proxy.GetEntity());
    };

    auto ensureAttachmentBinding = [](EntityProxy& proxy) -> AttachmentBindingComponent*
    {
        if (!proxy.IsValid())
            return nullptr;

        AttachmentBindingComponent* binding = proxy.world->GetComponent<AttachmentBindingComponent>(proxy.GetEntity());
        if (!binding)
            binding = &proxy.world->AddComponent<AttachmentBindingComponent>(proxy.GetEntity());
        return binding;
    };

    auto getSocketProxy = [this](Entity root, const std::string& socketName) -> sol::object
    {
        sol::state& state = m_Runtime->GetState();
        if (!m_World || !root.IsValid() || !m_World->IsAlive(root) || socketName.empty())
            return sol::make_object(state, sol::nil);

        const Entity socket = FindAttachmentSocket(*m_World, root, socketName);
        if (!socket.IsValid() || socket == root)
            return sol::make_object(state, sol::nil);
        return sol::make_object(state, EntityProxy(socket, m_World));
    };

    auto ensureSocketEntity = [this](Entity root, const std::string& socketName, const Vec3& localPosition,
                                     const Vec3& localRotation) -> Entity
    {
        if (!m_World || !root.IsValid() || !m_World->IsAlive(root) || socketName.empty())
            return kNullEntity;

        const Entity existing = FindAttachmentSocket(*m_World, root, socketName);
        if (existing.IsValid() && existing != root && m_World->IsAlive(existing))
            return existing;

        const Entity socket = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(socket).name = socketName + "_socket";
        TransformComponent& socketTransform = m_World->AddComponent<TransformComponent>(socket);
        socketTransform.position = localPosition;
        socketTransform.rotation = localRotation;
        socketTransform.scale = Vec3::One();
        socketTransform.dirty = true;
        m_World->AddComponent<AttachmentPointComponent>(socket).socketName = socketName;

        HierarchyComponent* parentHierarchy = m_World->GetComponent<HierarchyComponent>(root);
        if (!parentHierarchy)
            parentHierarchy = &m_World->AddComponent<HierarchyComponent>(root);
        if (std::find(parentHierarchy->children.begin(), parentHierarchy->children.end(), socket) ==
            parentHierarchy->children.end())
        {
            parentHierarchy->children.push_back(socket);
        }

        HierarchyComponent& socketHierarchy = m_World->AddComponent<HierarchyComponent>(socket);
        socketHierarchy.parent = root;
        return socket;
    };

    auto hasLineOfSightBetweenPoints = [this](const Vec3& origin, const Vec3& target,
                                              const ScriptPerceptionOptions& options,
                                              const std::vector<Entity>& ignoredEntities) -> bool
    {
        if (!m_World)
            return false;

        const Vec3 toTarget = target - origin;
        const float distance = toTarget.Length();
        if (distance <= kVec3Epsilon)
            return true;
        if (options.maxDistance > 0.0f && distance > options.maxDistance)
            return false;

        ScriptRaycastResult hit;
        return !RaycastSceneForScripts(*m_World, m_StaticWorldGeometry, origin, toTarget, distance, options.layerMask,
                                       options.includeTriggers, ignoredEntities, hit);
    };

    auto canSeeTargetPoint = [this, hasLineOfSightBetweenPoints](Entity observer, const Vec3& target,
                                                                 const ScriptPerceptionOptions& options,
                                                                 const std::vector<Entity>& ignoredEntities) -> bool
    {
        if (!m_World || !observer.IsValid() || !m_World->IsAlive(observer))
            return false;

        const Vec3 origin = GetEntityPosition(*m_World, observer) + options.eyeOffset;
        const Vec3 targetPoint = target + options.targetOffset;
        const Vec3 toTarget = targetPoint - origin;
        const float distance = toTarget.Length();
        if (distance <= kVec3Epsilon)
            return true;
        if (options.maxDistance > 0.0f && distance > options.maxDistance)
            return false;

        if (options.fieldOfViewDegrees > 0.0f && options.fieldOfViewDegrees < 360.0f)
        {
            const Vec3 forward = GetEntityForward(*m_World, observer);
            const Vec3 direction = toTarget / distance;
            const float minDot = std::cos(DegreesToRadians(options.fieldOfViewDegrees * 0.5f));
            if (Vec3::Dot(forward, direction) < minDot)
                return false;
        }

        return hasLineOfSightBetweenPoints(origin, targetPoint, options, ignoredEntities);
    };

    lua.new_usertype<NavigationPathHandle>(
        "NavigationPathRequest",
        sol::constructors<NavigationPathHandle()>(),
        "Status",
        sol::property(
            [](const NavigationPathHandle& handle) -> std::string
            {
                if (!handle.navigation || handle.requestId == 0)
                    return "failed";
                return ToLuaStatus(handle.navigation->GetPathRequestStatus(handle.requestId));
            }),
        "IsDone",
        sol::property(
            [](const NavigationPathHandle& handle) -> bool
            {
                return handle.navigation && handle.requestId != 0 && handle.navigation->IsPathRequestDone(handle.requestId);
            }),
        "Succeeded",
        sol::property(
            [](const NavigationPathHandle& handle) -> bool
            {
                return handle.navigation && handle.requestId != 0 &&
                       handle.navigation->DidPathRequestSucceed(handle.requestId);
            }),
        "Path",
        sol::property(
            [this](const NavigationPathHandle& handle) -> sol::table
            {
                sol::state& state = m_Runtime->GetState();
                sol::table result = state.create_table();
                if (!handle.navigation || handle.requestId == 0)
                    return result;

                const std::vector<Vec3> path = handle.navigation->GetPathRequestPath(handle.requestId);
                for (size_t i = 0; i < path.size(); ++i)
                    result[static_cast<int>(i + 1)] = path[i];
                return result;
            }));

    lua.new_usertype<NavigationMoveHandle>(
        "NavigationMoveHandle",
        sol::constructors<NavigationMoveHandle()>(),
        "Status",
        sol::property(
            [](const NavigationMoveHandle& handle) -> std::string
            {
                if (!handle.navigation || handle.moveId == 0)
                    return "failed";
                return ToLuaStatus(handle.navigation->GetMoveStatus(handle.moveId));
            }),
        "IsDone",
        sol::property(
            [](const NavigationMoveHandle& handle) -> bool
            {
                return handle.navigation && handle.moveId != 0 && handle.navigation->IsMoveDone(handle.moveId);
            }),
        "Succeeded",
        sol::property(
            [](const NavigationMoveHandle& handle) -> bool
            {
                return handle.navigation && handle.moveId != 0 && handle.navigation->DidMoveSucceed(handle.moveId);
            }),
        "Path",
        sol::property(
            [this](const NavigationMoveHandle& handle) -> sol::table
            {
                sol::state& state = m_Runtime->GetState();
                sol::table result = state.create_table();
                if (!handle.navigation || handle.moveId == 0)
                    return result;

                const std::vector<Vec3> path = handle.navigation->GetMovePath(handle.moveId);
                for (size_t i = 0; i < path.size(); ++i)
                    result[static_cast<int>(i + 1)] = path[i];
                return result;
            }));

    // =========================================================================
    // EntityProxy Usertype - Roblox-style property access
    // =========================================================================
    sol::usertype<EntityProxy> entityType = lua.new_usertype<EntityProxy>(
        "EntityProxy",
        // Constructor (usually created internally, not by scripts)
        sol::constructors<EntityProxy()>(),

        // IsValid property - check if entity still exists
        "IsValid", sol::property(&EntityProxy::IsValid),

        // Name property
        "Name", sol::property(&EntityProxy::GetName, &EntityProxy::SetName),

        // Health properties
        "HasHealth", sol::property(&EntityProxy::HasHealth),
        "Health", sol::property(&EntityProxy::GetHealth, &EntityProxy::SetHealth),
        "MaxHealth", sol::property(&EntityProxy::GetMaxHealth, &EntityProxy::SetMaxHealth),
        "Invulnerable", sol::property(&EntityProxy::GetInvulnerable, &EntityProxy::SetInvulnerable),
        "DestroyOnDeath", sol::property(&EntityProxy::GetDestroyOnDeath, &EntityProxy::SetDestroyOnDeath),
        "IsDead", sol::property(&EntityProxy::IsDead),
        "HealthPercent", sol::property(&EntityProxy::GetHealthPercent),
        "RenderLayerMask", sol::property(&EntityProxy::GetRenderLayerMask, &EntityProxy::SetRenderLayerMask),
        "InWorldLayer", sol::property(&EntityProxy::GetInWorldLayer, &EntityProxy::SetInWorldLayer),
        "InViewmodelLayer", sol::property(&EntityProxy::GetInViewmodelLayer, &EntityProxy::SetInViewmodelLayer),
        "AttachmentEnabled",
        sol::property(
            [getAttachmentBinding](const EntityProxy& proxy) -> bool
            {
                if (AttachmentBindingComponent* binding = getAttachmentBinding(proxy))
                    return binding->enabled;
                return false;
            },
            [ensureAttachmentBinding](EntityProxy& proxy, bool enabled)
            {
                if (AttachmentBindingComponent* binding = ensureAttachmentBinding(proxy))
                    binding->enabled = enabled;
            }),
        "AttachmentSocket",
        sol::property(
            [getAttachmentBinding](const EntityProxy& proxy) -> std::string
            {
                if (AttachmentBindingComponent* binding = getAttachmentBinding(proxy))
                    return binding->socketName;
                return "";
            },
            [ensureAttachmentBinding](EntityProxy& proxy, const std::string& socketName)
            {
                if (AttachmentBindingComponent* binding = ensureAttachmentBinding(proxy))
                    binding->socketName = socketName;
            }),
        "AttachmentTargetMode",
        sol::property(
            [getAttachmentBinding](const EntityProxy& proxy) -> std::string
            {
                AttachmentBindingComponent* binding = getAttachmentBinding(proxy);
                if (!binding || !binding->enabled)
                    return "none";
                return binding->targetMode == AttachmentTargetMode::ActiveCamera ? "camera" : "entity";
            }),

        // Position property
        "Position",
        sol::property(
            [](const EntityProxy& proxy) -> Vec3
            {
                if (!proxy.IsValid())
                    return Vec3();
                TransformComponent* t = proxy.world->GetComponent<TransformComponent>(proxy.GetEntity());
                return t ? t->position : Vec3();
            },
            [](EntityProxy& proxy, const Vec3& pos)
            {
                if (!proxy.IsValid())
                    return;
                TransformComponent* t = proxy.world->GetComponent<TransformComponent>(proxy.GetEntity());
                if (t)
                {
                    t->position = pos;
                    t->dirty = true;
                }
            }),

        // Rotation property
        "Rotation",
        sol::property(
            [](const EntityProxy& proxy) -> Vec3
            {
                if (!proxy.IsValid())
                    return Vec3();
                TransformComponent* t = proxy.world->GetComponent<TransformComponent>(proxy.GetEntity());
                return t ? t->rotation : Vec3();
            },
            [](EntityProxy& proxy, const Vec3& rot)
            {
                if (!proxy.IsValid())
                    return;
                TransformComponent* t = proxy.world->GetComponent<TransformComponent>(proxy.GetEntity());
                if (t)
                {
                    t->rotation = rot;
                    t->dirty = true;
                }
            }),

        // Scale property
        "Scale",
        sol::property(
            [](const EntityProxy& proxy) -> Vec3
            {
                if (!proxy.IsValid())
                    return Vec3(1, 1, 1);
                TransformComponent* t = proxy.world->GetComponent<TransformComponent>(proxy.GetEntity());
                return t ? t->scale : Vec3(1, 1, 1);
            },
            [](EntityProxy& proxy, const Vec3& scale)
            {
                if (!proxy.IsValid())
                    return;
                TransformComponent* t = proxy.world->GetComponent<TransformComponent>(proxy.GetEntity());
                if (t)
                {
                    t->scale = scale;
                    t->dirty = true;
                }
            }),

        // =====================================================================
        // Physics Properties (RigidBody)
        // =====================================================================

        // Velocity property
        "Velocity",
        sol::property(
            [](const EntityProxy& proxy) -> Vec3
            {
                if (!proxy.IsValid())
                    return Vec3();
                RigidBodyComponent* rb = proxy.world->GetComponent<RigidBodyComponent>(proxy.GetEntity());
                return rb ? rb->velocity : Vec3();
            },
            [](EntityProxy& proxy, const Vec3& vel)
            {
                if (!proxy.IsValid())
                    return;
                RigidBodyComponent* rb = proxy.world->GetComponent<RigidBodyComponent>(proxy.GetEntity());
                if (rb)
                    rb->velocity = vel;
            }),

        // HasRigidBody - check if entity has a RigidBody
        "HasRigidBody",
        sol::property(
            [](const EntityProxy& proxy) -> bool
            {
                if (!proxy.IsValid())
                    return false;
                return proxy.world->HasComponent<RigidBodyComponent>(proxy.GetEntity());
            }),

        // AddForce - applies a continuous force (use in OnUpdate)
        "AddForce",
        [](EntityProxy& proxy, const Vec3& force)
        {
            if (!proxy.IsValid())
                return;
            RigidBodyComponent* rb = proxy.world->GetComponent<RigidBodyComponent>(proxy.GetEntity());
            if (rb)
            {
                rb->forceAccumulator.x += force.x;
                rb->forceAccumulator.y += force.y;
                rb->forceAccumulator.z += force.z;
            }
        },

        // AddImpulse - applies instant velocity change
        "AddImpulse",
        [](EntityProxy& proxy, const Vec3& impulse)
        {
            if (!proxy.IsValid())
                return;
            RigidBodyComponent* rb = proxy.world->GetComponent<RigidBodyComponent>(proxy.GetEntity());
            if (rb && rb->mass > 0.0001f)
            {
                float invMass = 1.0f / rb->mass;
                rb->velocity.x += impulse.x * invMass;
                rb->velocity.y += impulse.y * invMass;
                rb->velocity.z += impulse.z * invMass;
            }
        },

        // Destroy - destroy this entity
        "Destroy",
        [](EntityProxy& proxy)
        {
            if (!proxy.IsValid())
                return;
            proxy.world->DestroyEntity(proxy.GetEntity());
        },

        // Health helpers
        "ApplyDamage",
        sol::overload(
            [this](EntityProxy& proxy, float amount) -> float
            {
                if (!proxy.IsValid())
                    return 0.0f;
                return ApplyDamage(proxy.GetEntity(), amount, kNullEntity);
            },
            [this](EntityProxy& proxy, float amount, const EntityProxy& source) -> float
            {
                if (!proxy.IsValid())
                    return 0.0f;
                const Entity sourceEntity = source.IsValid() ? source.GetEntity() : kNullEntity;
                return ApplyDamage(proxy.GetEntity(), amount, sourceEntity);
            }),
        "Heal",
        [this](EntityProxy& proxy, float amount) -> float
        {
            if (!proxy.IsValid())
                return 0.0f;
            return Heal(proxy.GetEntity(), amount);
        },
        "RestoreFullHealth", &EntityProxy::RestoreFullHealth,
        "AttachToEntity",
        sol::overload(
            [ensureAttachmentBinding](EntityProxy& proxy, const EntityProxy& target)
            {
                if (!proxy.IsValid())
                    return;

                AttachmentBindingComponent* binding = ensureAttachmentBinding(proxy);
                if (!binding)
                    return;

                binding->enabled = target.IsValid();
                binding->targetMode = AttachmentTargetMode::Entity;
                binding->targetEntity = target.IsValid() ? target.GetEntity() : kNullEntity;
                binding->socketName.clear();
            },
            [ensureAttachmentBinding](EntityProxy& proxy, const EntityProxy& target, const std::string& socketName)
            {
                if (!proxy.IsValid())
                    return;

                AttachmentBindingComponent* binding = ensureAttachmentBinding(proxy);
                if (!binding)
                    return;

                binding->enabled = target.IsValid();
                binding->targetMode = AttachmentTargetMode::Entity;
                binding->targetEntity = target.IsValid() ? target.GetEntity() : kNullEntity;
                binding->socketName = socketName;
            }),
        "AttachToCamera",
        sol::overload(
            [ensureAttachmentBinding](EntityProxy& proxy)
            {
                if (!proxy.IsValid())
                    return;

                AttachmentBindingComponent* binding = ensureAttachmentBinding(proxy);
                if (!binding)
                    return;

                binding->enabled = true;
                binding->targetMode = AttachmentTargetMode::ActiveCamera;
                binding->targetEntity = kNullEntity;
                binding->socketName.clear();
            },
            [ensureAttachmentBinding](EntityProxy& proxy, const std::string& socketName)
            {
                if (!proxy.IsValid())
                    return;

                AttachmentBindingComponent* binding = ensureAttachmentBinding(proxy);
                if (!binding)
                    return;

                binding->enabled = true;
                binding->targetMode = AttachmentTargetMode::ActiveCamera;
                binding->targetEntity = kNullEntity;
                binding->socketName = socketName;
            }),
        "Detach",
        [](EntityProxy& proxy)
        {
            if (!proxy.IsValid())
                return;

            if (AttachmentBindingComponent* binding = proxy.world->GetComponent<AttachmentBindingComponent>(proxy.GetEntity()))
            {
                binding->enabled = false;
                binding->targetEntity = kNullEntity;
                binding->socketName.clear();
            }
        },
        "GetAttachmentTarget",
        [this](EntityProxy& proxy) -> sol::object
        {
            sol::state& state = m_Runtime->GetState();
            if (!proxy.IsValid())
                return sol::make_object(state, sol::nil);

            AttachmentBindingComponent* binding = proxy.world->GetComponent<AttachmentBindingComponent>(proxy.GetEntity());
            if (!binding || !binding->enabled)
                return sol::make_object(state, sol::nil);

            if (binding->targetMode == AttachmentTargetMode::ActiveCamera)
            {
                const Entity activeCamera = FindActiveCameraEntity(*proxy.world);
                if (!activeCamera.IsValid())
                    return sol::make_object(state, sol::nil);
                return sol::make_object(state, EntityProxy(activeCamera, proxy.world));
            }

            if (!binding->targetEntity.IsValid() || !proxy.world->IsAlive(binding->targetEntity))
                return sol::make_object(state, sol::nil);
            return sol::make_object(state, EntityProxy(binding->targetEntity, proxy.world));
        },
        "IsAttachedToCamera",
        [getAttachmentBinding](const EntityProxy& proxy) -> bool
        {
            AttachmentBindingComponent* binding = getAttachmentBinding(proxy);
            return binding && binding->enabled && binding->targetMode == AttachmentTargetMode::ActiveCamera;
        },
        "HasSocket",
        [this](const EntityProxy& proxy, const std::string& socketName) -> bool
        {
            if (!proxy.IsValid() || socketName.empty())
                return false;
            const Entity socket = FindAttachmentSocket(*proxy.world, proxy.GetEntity(), socketName);
            return socket.IsValid() && socket != proxy.GetEntity();
        },
        "GetSocket",
        [getSocketProxy, this](const EntityProxy& proxy, const std::string& socketName) -> sol::object
        {
            if (!proxy.IsValid())
                return sol::make_object(m_Runtime->GetState(), sol::nil);
            return getSocketProxy(proxy.GetEntity(), socketName);
        },
        "CreateSocket",
        sol::overload(
            [ensureSocketEntity, this](EntityProxy& proxy, const std::string& socketName) -> sol::object
            {
                sol::state& state = m_Runtime->GetState();
                if (!proxy.IsValid())
                    return sol::make_object(state, sol::nil);
                const Entity socket = ensureSocketEntity(proxy.GetEntity(), socketName, Vec3::Zero(), Vec3::Zero());
                if (!socket.IsValid())
                    return sol::make_object(state, sol::nil);
                return sol::make_object(state, EntityProxy(socket, proxy.world));
            },
            [ensureSocketEntity, this](EntityProxy& proxy, const std::string& socketName,
                                       const Vec3& localPosition) -> sol::object
            {
                sol::state& state = m_Runtime->GetState();
                if (!proxy.IsValid())
                    return sol::make_object(state, sol::nil);
                const Entity socket = ensureSocketEntity(proxy.GetEntity(), socketName, localPosition, Vec3::Zero());
                if (!socket.IsValid())
                    return sol::make_object(state, sol::nil);
                return sol::make_object(state, EntityProxy(socket, proxy.world));
            },
            [ensureSocketEntity, this](EntityProxy& proxy, const std::string& socketName, const Vec3& localPosition,
                                       const Vec3& localRotation) -> sol::object
            {
                sol::state& state = m_Runtime->GetState();
                if (!proxy.IsValid())
                    return sol::make_object(state, sol::nil);
                const Entity socket =
                    ensureSocketEntity(proxy.GetEntity(), socketName, localPosition, localRotation);
                if (!socket.IsValid())
                    return sol::make_object(state, sol::nil);
                return sol::make_object(state, EntityProxy(socket, proxy.world));
            }),
        "MoveTo",
        sol::overload(
            [startMove](EntityProxy& proxy, const Vec3& target) -> NavigationMoveHandle
            { return startMove(proxy, target, NavigationMoveOptions{}, sol::nullopt); },
            [startMove, parseMoveOptionsTable](EntityProxy& proxy, const Vec3& target,
                                               const sol::table& optionsTable) -> NavigationMoveHandle
            { return startMove(proxy, target, parseMoveOptionsTable(optionsTable), sol::nullopt); },
            [startMove](EntityProxy& proxy, const Vec3& target, const sol::function& callback) -> NavigationMoveHandle
            { return startMove(proxy, target, NavigationMoveOptions{}, callback); },
            [startMove, parseMoveOptionsTable](EntityProxy& proxy, const Vec3& target, const sol::table& optionsTable,
                                               const sol::function& callback) -> NavigationMoveHandle
            { return startMove(proxy, target, parseMoveOptionsTable(optionsTable), callback); }),
        "StopMove",
        [this](EntityProxy& proxy)
        {
            if (!proxy.IsValid() || !m_NavigationSystem || !m_World)
                return;
            m_NavigationSystem->StopMove(*m_World, proxy.GetEntity());
        },
        "IsMoving",
        [this](EntityProxy& proxy) -> bool
        {
            return proxy.IsValid() && m_NavigationSystem && m_NavigationSystem->IsEntityMoving(proxy.GetEntity());
        },
        "GetMoveStatus",
        [this](EntityProxy& proxy) -> std::string
        {
            if (!proxy.IsValid() || !m_NavigationSystem)
                return "idle";

            NavigationMoveStatus status = NavigationMoveStatus::Failed;
            if (!m_NavigationSystem->TryGetEntityMoveStatus(proxy.GetEntity(), status))
                return "idle";
            return ToLuaStatus(status);
        },
        "HasLineOfSight",
        sol::overload(
            [this, hasLineOfSightBetweenPoints](EntityProxy& proxy, const EntityProxy& target,
                                                sol::optional<sol::table> optionsTable) -> bool
            {
                if (!proxy.IsValid() || !target.IsValid())
                    return false;

                const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
                const Vec3 origin = GetEntityPosition(*m_World, proxy.GetEntity()) + options.eyeOffset;
                const Vec3 targetPoint = GetEntityPosition(*m_World, target.GetEntity()) + options.targetOffset;
                return hasLineOfSightBetweenPoints(origin, targetPoint, options,
                                                   {proxy.GetEntity(), target.GetEntity()});
            },
            [this, hasLineOfSightBetweenPoints](EntityProxy& proxy, const Vec3& target,
                                                sol::optional<sol::table> optionsTable) -> bool
            {
                if (!proxy.IsValid())
                    return false;

                const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
                const Vec3 origin = GetEntityPosition(*m_World, proxy.GetEntity()) + options.eyeOffset;
                return hasLineOfSightBetweenPoints(origin, target + options.targetOffset, options, {proxy.GetEntity()});
            }),
        "CanSee",
        sol::overload(
            [canSeeTargetPoint](EntityProxy& proxy, const EntityProxy& target,
                                sol::optional<sol::table> optionsTable) -> bool
            {
                if (!proxy.IsValid() || !target.IsValid())
                    return false;

                const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
                return canSeeTargetPoint(proxy.GetEntity(), GetEntityPosition(*proxy.world, target.GetEntity()), options,
                                         {proxy.GetEntity(), target.GetEntity()});
            },
            [canSeeTargetPoint](EntityProxy& proxy, const Vec3& target,
                                sol::optional<sol::table> optionsTable) -> bool
            {
                if (!proxy.IsValid())
                    return false;

                const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
                return canSeeTargetPoint(proxy.GetEntity(), target, options, {proxy.GetEntity()});
            }),
        "GetEntitiesInRadius",
        [this, buildEntityProxyTable](EntityProxy& proxy, float radius, sol::optional<sol::table> optionsTable) -> sol::table
        {
            if (!proxy.IsValid() || !m_World || radius <= 0.0f)
                return buildEntityProxyTable({});

            const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
            std::vector<Entity> ignoredEntities;
            if (!options.includeSelf)
                ignoredEntities.push_back(proxy.GetEntity());
            return buildEntityProxyTable(
                CollectEntitiesInRadiusForScripts(*m_World, GetEntityPosition(*m_World, proxy.GetEntity()), radius,
                                                  options.layerMask, options.includeTriggers, ignoredEntities));
        },

        // GetEntity method - get the raw Entity handle
        "GetEntity", &EntityProxy::GetEntity);

    // =========================================================================
    // Navigation API
    // =========================================================================
    sol::table navigationTable = lua.create_named_table("Navigation");

    navigationTable["IsAvailable"] = [this]() -> bool
    {
        return m_NavigationSystem && m_NavigationSystem->EnsureNavMeshUpToDate() && m_NavigationSystem->IsAvailable();
    };

    navigationTable["FindPathAsync"] = [this, parsePathOptions](const Vec3& start, const Vec3& goal,
                                                                sol::optional<sol::table> optionsTable) -> NavigationPathHandle
    {
        NavigationPathHandle handle;
        if (!m_NavigationSystem)
            return handle;

        handle.navigation = m_NavigationSystem;
        handle.requestId = m_NavigationSystem->RequestPathAsync(start, goal, parsePathOptions(optionsTable));
        return handle;
    };

    navigationTable["FindPath"] = [this, parsePathOptions](const Vec3& start, const Vec3& goal,
                                                           sol::optional<sol::table> optionsTable) -> sol::object
    {
        if (!m_NavigationSystem)
            return sol::nil;

        std::vector<Vec3> path;
        if (!m_NavigationSystem->FindPath(start, goal, path, parsePathOptions(optionsTable)))
            return sol::nil;

        sol::state& luaState = m_Runtime->GetState();
        sol::table result = luaState.create_table();
        for (size_t i = 0; i < path.size(); ++i)
            result[static_cast<int>(i + 1)] = path[i];
        return sol::make_object(luaState, result);
    };

    // =========================================================================
    // Perception API - line of sight, vision, and radius queries
    // =========================================================================
    sol::table perceptionTable = lua.create_named_table("Perception");

    perceptionTable["HasLineOfSight"] = sol::overload(
        [this, hasLineOfSightBetweenPoints](const EntityProxy& observer, const EntityProxy& target,
                                            sol::optional<sol::table> optionsTable) -> bool
        {
            if (!observer.IsValid() || !target.IsValid())
                return false;

            const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
            const Vec3 origin = GetEntityPosition(*m_World, observer.GetEntity()) + options.eyeOffset;
            const Vec3 targetPoint = GetEntityPosition(*m_World, target.GetEntity()) + options.targetOffset;
            return hasLineOfSightBetweenPoints(origin, targetPoint, options, {observer.GetEntity(), target.GetEntity()});
        },
        [this, hasLineOfSightBetweenPoints](const EntityProxy& observer, const Vec3& target,
                                            sol::optional<sol::table> optionsTable) -> bool
        {
            if (!observer.IsValid())
                return false;

            const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
            const Vec3 origin = GetEntityPosition(*m_World, observer.GetEntity()) + options.eyeOffset;
            return hasLineOfSightBetweenPoints(origin, target + options.targetOffset, options, {observer.GetEntity()});
        },
        [this, hasLineOfSightBetweenPoints](const Vec3& origin, const Vec3& target,
                                            sol::optional<sol::table> optionsTable) -> bool
        {
            const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
            return hasLineOfSightBetweenPoints(origin + options.eyeOffset, target + options.targetOffset, options, {});
        });

    perceptionTable["CanSee"] = sol::overload(
        [canSeeTargetPoint](const EntityProxy& observer, const EntityProxy& target,
                            sol::optional<sol::table> optionsTable) -> bool
        {
            if (!observer.IsValid() || !target.IsValid())
                return false;

            const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
            return canSeeTargetPoint(observer.GetEntity(), GetEntityPosition(*observer.world, target.GetEntity()), options,
                                     {observer.GetEntity(), target.GetEntity()});
        },
        [canSeeTargetPoint](const EntityProxy& observer, const Vec3& target,
                            sol::optional<sol::table> optionsTable) -> bool
        {
            if (!observer.IsValid())
                return false;

            const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
            return canSeeTargetPoint(observer.GetEntity(), target, options, {observer.GetEntity()});
        });

    perceptionTable["GetEntitiesInRadius"] =
        [this, buildEntityProxyTable](const Vec3& center, float radius, sol::optional<sol::table> optionsTable) -> sol::table
    {
        if (!m_World || radius <= 0.0f)
            return buildEntityProxyTable({});

        const ScriptPerceptionOptions options = ParsePerceptionOptions(optionsTable);
        return buildEntityProxyTable(
            CollectEntitiesInRadiusForScripts(*m_World, center, radius, options.layerMask, options.includeTriggers, {}));
    };

    // =========================================================================
    // World API - Query and create entities
    // =========================================================================
    sol::table worldTable = lua.create_named_table("World");

    // Find entity by name - returns EntityProxy
    worldTable["FindByName"] = [this](const std::string& name) -> sol::object
    {
        if (!m_World)
            return sol::nil;

        Entity found = kNullEntity;
        m_World->Each<NameComponent>(
            [&](Entity entity, NameComponent& nameComp)
            {
                if (nameComp.name == name)
                    found = entity;
            });

        if (found == kNullEntity)
            return sol::nil;

        return sol::make_object(m_Runtime->GetState(), EntityProxy(found, m_World));
    };

    // Get all entities with a specific component by name
    worldTable["GetEntitiesWithName"] = [this]() -> sol::table
    {
        sol::state& lua = m_Runtime->GetState();
        sol::table result = lua.create_table();

        if (m_World)
        {
            int index = 1;
            m_World->Each<NameComponent>([&](Entity entity, NameComponent& /*nameComp*/)
                                         { result[index++] = EntityProxy(entity, m_World); });
        }

        return result;
    };

    // World.Spawn(name) - create a new entity with just a transform
    worldTable["Spawn"] = [this](const std::string& name) -> sol::object
    {
        if (!m_World)
            return sol::nil;

        Entity entity = m_World->CreateEntity();
        auto& nameComp = m_World->AddComponent<NameComponent>(entity);
        nameComp.name = name;
        m_World->AddComponent<TransformComponent>(entity);

        return sol::make_object(m_Runtime->GetState(), EntityProxy(entity, m_World));
    };

    // World.SpawnAt(name, position) - create entity at position
    worldTable["SpawnAt"] = [this](const std::string& name, const Vec3& position) -> sol::object
    {
        if (!m_World)
            return sol::nil;

        Entity entity = m_World->CreateEntity();
        auto& nameComp = m_World->AddComponent<NameComponent>(entity);
        nameComp.name = name;
        auto& transform = m_World->AddComponent<TransformComponent>(entity);
        transform.position = position;

        return sol::make_object(m_Runtime->GetState(), EntityProxy(entity, m_World));
    };

    // World.SpawnPrimitive(name, primitiveType, position?) - create a renderable primitive entity
    worldTable["SpawnPrimitive"] =
        [this](const std::string& name, const std::string& primitiveType, sol::optional<Vec3> position) -> sol::object
    {
        if (!m_World)
            return sol::nil;

        PrimitiveType type = PrimitiveType::Cube;
        std::string normalizedType = primitiveType;
        std::transform(normalizedType.begin(), normalizedType.end(), normalizedType.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (normalizedType == "sphere")
            type = PrimitiveType::Sphere;
        else if (normalizedType == "cylinder")
            type = PrimitiveType::Cylinder;
        else if (normalizedType == "plane")
            type = PrimitiveType::Plane;
        else if (normalizedType == "cone")
            type = PrimitiveType::Cone;
        else if (normalizedType == "capsule")
            type = PrimitiveType::Capsule;

        Entity entity = m_World->CreateEntity();
        auto& nameComp = m_World->AddComponent<NameComponent>(entity);
        nameComp.name = name;

        auto& transform = m_World->AddComponent<TransformComponent>(entity);
        transform.position = position.value_or(Vec3(0, 0, 0));

        auto& primitive = m_World->AddComponent<PrimitiveComponent>(entity);
        primitive.type = type;

        return sol::make_object(m_Runtime->GetState(), EntityProxy(entity, m_World));
    };

    // World.DestroyEntity(entity) - destroy an entity
    worldTable["DestroyEntity"] = [this](const EntityProxy& proxy)
    {
        if (!m_World || !proxy.IsValid())
            return;
        m_World->DestroyEntity(proxy.GetEntity());
    };

    // World.Instantiate(prefabPath, position) - instantiate a prefab
    worldTable["Instantiate"] = [this](const std::string& prefabPath, sol::optional<Vec3> position) -> sol::object
    {
        if (!m_World)
            return sol::nil;

        Vec3 pos = position.value_or(Vec3(0, 0, 0));
        Entity entity = PrefabSystem::InstantiateFromFile(*m_World, prefabPath, pos);

        if (!entity.IsValid())
            return sol::nil;

        return sol::make_object(m_Runtime->GetState(), EntityProxy(entity, m_World));
    };

    // =========================================================================
    // Tween API - Simple tweening for animations
    // =========================================================================
    sol::table tweenTable = lua.create_named_table("Tween");

    // Store active tweens in Lua (simple approach)
    lua["__activeTweens"] = lua.create_table();
    lua["__tweenId"] = 0;

    // Tween.To(from, to, duration, callback) - call callback with interpolated value
    tweenTable["To"] = [this](float from, float to, float duration, sol::function callback) -> int
    {
        sol::state& lua = m_Runtime->GetState();
        int id = lua["__tweenId"].get<int>() + 1;
        lua["__tweenId"] = id;

        sol::table tween = lua.create_table();
        tween["from"] = from;
        tween["to"] = to;
        tween["duration"] = duration;
        tween["elapsed"] = 0.0f;
        tween["callback"] = callback;
        tween["active"] = true;

        lua["__activeTweens"][id] = tween;
        return id;
    };

    // Tween.Cancel(id)
    tweenTable["Cancel"] = [this](int id)
    {
        sol::state& lua = m_Runtime->GetState();
        sol::table tweens = lua["__activeTweens"];
        if (tweens[id].valid())
        {
            sol::table tween = tweens[id];
            tween["active"] = false;
        }
    };

    // Internal: Update tweens (called from C++ each frame)
    tweenTable["_Update"] = [this](float dt)
    {
        sol::state& lua = m_Runtime->GetState();
        sol::table tweens = lua["__activeTweens"];
        std::vector<int> toRemove;

        for (auto& pair : tweens)
        {
            if (!pair.second.is<sol::table>())
                continue;

            sol::table tween = pair.second;
            if (!tween["active"].get<bool>())
            {
                toRemove.push_back(pair.first.as<int>());
                continue;
            }

            float elapsed = tween["elapsed"].get<float>() + dt;
            float duration = tween["duration"].get<float>();
            float from = tween["from"].get<float>();
            float to = tween["to"].get<float>();

            float t = elapsed / duration;
            if (t >= 1.0f)
            {
                t = 1.0f;
                tween["active"] = false;
                toRemove.push_back(pair.first.as<int>());
            }

            float value = from + (to - from) * t;
            tween["elapsed"] = elapsed;

            sol::function callback = tween["callback"];
            if (callback.valid())
            {
                try
                {
                    callback(value);
                }
                catch (...)
                {
                }
            }
        }

        for (int id : toRemove)
        {
            tweens[id] = sol::nil;
        }
    };

    // =========================================================================
    // Physics API - Raycasting and physics queries
    // =========================================================================
    sol::table physicsTable = lua.create_named_table("Physics");

    // Physics.Raycast(origin, direction, maxDistance) -> table or nil
    // Returns: { hit=bool, point=Vec3, normal=Vec3, distance=float, entity=EntityProxy }
    physicsTable["Raycast"] = [this](const Vec3& origin, const Vec3& direction, float maxDistance,
                                     sol::optional<uint32> layerMaskOpt) -> sol::object
    {
        if (!m_World)
            return sol::nil;

        const uint32 queryMask = layerMaskOpt.value_or(CollisionLayers::kAllLayersMask);

        // Normalize direction
        float dirLen = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (dirLen < 0.0001f)
            return sol::nil;

        Ray ray;
        ray.origin = origin;
        ray.direction = Vec3(direction.x / dirLen, direction.y / dirLen, direction.z / dirLen);

        RaycastHit closestHit;
        closestHit.hit = false;
        closestHit.distance = maxDistance + 1.0f;
        Entity hitEntity = kNullEntity;

        // Check all box colliders
        m_World->Each<TransformComponent, BoxColliderComponent>(
            [&](Entity entity, TransformComponent& transform, BoxColliderComponent& box)
            {
                if (!QueryMaskIncludesLayer(queryMask, box.collisionLayer))
                    return;

                // Build OBB from transform and collider
                OBB obb;
                obb = BuildScriptOBB(transform, box);

                RaycastHit thisHit;
                if (RaycastVsOBB(ray, obb, maxDistance, thisHit))
                {
                    if (thisHit.distance < closestHit.distance)
                    {
                        closestHit = thisHit;
                        hitEntity = entity;
                    }
                }
            });

        // Check all sphere colliders
        m_World->Each<TransformComponent, SphereColliderComponent>(
            [&](Entity entity, TransformComponent& transform, SphereColliderComponent& sphereComp)
            {
                if (!QueryMaskIncludesLayer(queryMask, sphereComp.collisionLayer))
                    return;

                Sphere sphere;
                sphere = BuildScriptSphere(transform, sphereComp);

                RaycastHit thisHit;
                if (RaycastVsSphere(ray, sphere, maxDistance, thisHit))
                {
                    if (thisHit.distance < closestHit.distance)
                    {
                        closestHit = thisHit;
                        hitEntity = entity;
                    }
                }
            });

        if (m_StaticWorldGeometry && QueryMaskIncludesLayer(queryMask, 0))
        {
            StaticWorldHit worldHit;
            if (m_StaticWorldGeometry->Raycast(ray, maxDistance, worldHit) &&
                (!closestHit.hit || worldHit.distance < closestHit.distance))
            {
                closestHit.hit = true;
                closestHit.distance = worldHit.distance;
                closestHit.point = worldHit.point;
                closestHit.normal = worldHit.normal;
                hitEntity = kNullEntity;
            }
        }

        // Return result
        if (!closestHit.hit)
            return sol::nil;

        sol::state& lua = m_Runtime->GetState();
        sol::table result = lua.create_table();
        result["hit"] = true;
        result["point"] = closestHit.point;
        result["normal"] = closestHit.normal;
        result["distance"] = closestHit.distance;
        if (hitEntity != kNullEntity)
            result["entity"] = EntityProxy(hitEntity, m_World);
        result["isWorld"] = (hitEntity == kNullEntity);

        return sol::make_object(lua, result);
    };

    // Physics.OverlapSphere(center, radius) -> table of hit entries
    physicsTable["OverlapSphere"] = [this](const Vec3& center, float radius, sol::optional<uint32> layerMaskOpt) -> sol::table
    {
        sol::state& lua = m_Runtime->GetState();
        sol::table hits = lua.create_table();
        if (!m_World || radius <= 0.0f)
            return hits;

        const uint32 queryMask = layerMaskOpt.value_or(CollisionLayers::kAllLayersMask);
        const Sphere querySphere{center, radius};
        int hitIndex = 1;

        m_World->Each<TransformComponent, BoxColliderComponent>(
            [&](Entity entity, TransformComponent& transform, BoxColliderComponent& box)
            {
                if (!QueryMaskIncludesLayer(queryMask, box.collisionLayer))
                    return;

                ContactManifold manifold;
                if (!OBBvsSphere(BuildScriptOBB(transform, box), querySphere, manifold) || manifold.numContacts <= 0)
                    return;

                sol::table hit = lua.create_table();
                hit["entity"] = EntityProxy(entity, m_World);
                hit["point"] = manifold.contacts[0].point;
                hit["normal"] = manifold.contacts[0].normal;
                hit["depth"] = manifold.contacts[0].depth;
                hit["isTrigger"] = box.isTrigger;
                hits[hitIndex++] = hit;
            });

        m_World->Each<TransformComponent, SphereColliderComponent>(
            [&](Entity entity, TransformComponent& transform, SphereColliderComponent& sphereComp)
            {
                if (!QueryMaskIncludesLayer(queryMask, sphereComp.collisionLayer))
                    return;

                ContactManifold manifold;
                if (!SphereVsSphere(BuildScriptSphere(transform, sphereComp), querySphere, manifold) ||
                    manifold.numContacts <= 0)
                    return;

                sol::table hit = lua.create_table();
                hit["entity"] = EntityProxy(entity, m_World);
                hit["point"] = manifold.contacts[0].point;
                hit["normal"] = manifold.contacts[0].normal;
                hit["depth"] = manifold.contacts[0].depth;
                hit["isTrigger"] = sphereComp.isTrigger;
                hits[hitIndex++] = hit;
            });

        if (m_StaticWorldGeometry && QueryMaskIncludesLayer(queryMask, 0))
        {
            StaticWorldHit worldHit;
            if (m_StaticWorldGeometry->OverlapSphere(center, radius, worldHit))
            {
                sol::table hit = lua.create_table();
                hit["entity"] = sol::nil;
                hit["point"] = worldHit.point;
                hit["normal"] = worldHit.normal;
                hit["depth"] = radius - worldHit.distance;
                hit["isTrigger"] = false;
                hit["isWorld"] = true;
                hits[hitIndex++] = hit;
            }
        }

        return hits;
    };

    // Physics.CheckSphere(center, radius) -> bool
    physicsTable["CheckSphere"] = [this](const Vec3& center, float radius, sol::optional<uint32> layerMaskOpt) -> bool
    {
        if (!m_World || radius <= 0.0f)
            return false;

        const uint32 queryMask = layerMaskOpt.value_or(CollisionLayers::kAllLayersMask);
        const Sphere querySphere{center, radius};
        bool foundHit = false;

        m_World->Each<TransformComponent, BoxColliderComponent>(
            [&](Entity, TransformComponent& transform, BoxColliderComponent& box)
            {
                if (foundHit)
                    return;
                if (!QueryMaskIncludesLayer(queryMask, box.collisionLayer))
                    return;
                ContactManifold manifold;
                foundHit = OBBvsSphere(BuildScriptOBB(transform, box), querySphere, manifold) && manifold.numContacts > 0;
            });

        if (foundHit)
            return true;

        m_World->Each<TransformComponent, SphereColliderComponent>(
            [&](Entity, TransformComponent& transform, SphereColliderComponent& sphereComp)
            {
                if (foundHit)
                    return;
                if (!QueryMaskIncludesLayer(queryMask, sphereComp.collisionLayer))
                    return;
                ContactManifold manifold;
                foundHit =
                    SphereVsSphere(BuildScriptSphere(transform, sphereComp), querySphere, manifold) && manifold.numContacts > 0;
            });

        if (!foundHit && m_StaticWorldGeometry && QueryMaskIncludesLayer(queryMask, 0))
        {
            StaticWorldHit worldHit;
            foundHit = m_StaticWorldGeometry->OverlapSphere(center, radius, worldHit);
        }

        return foundHit;
    };

    physicsTable["GetLayerIndex"] = [](const std::string& layerName) -> int
    {
        auto& layers = CollisionLayers::Get();
        for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
        {
            if (layers.GetLayerName(i) == layerName)
                return static_cast<int>(i);
        }
        return -1;
    };

    physicsTable["GetLayerMask"] = [](const std::string& layerName) -> uint32
    {
        auto& layers = CollisionLayers::Get();
        for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
        {
            if (layers.GetLayerName(i) == layerName)
                return CollisionLayers::LayerBit(i);
        }
        return 0u;
    };

    // =========================================================================
    // Camera API - for screen-to-world raycasting
    // =========================================================================
    sol::table cameraTable = lua.create_named_table("Camera");
    cameraTable["WorldLayerMask"] = RenderLayerMask::World;
    cameraTable["ViewmodelLayerMask"] = RenderLayerMask::Viewmodel;

    // Camera.GetPosition() -> Vec3
    cameraTable["GetPosition"] = [this]() -> Vec3
    { return Vec3(m_CameraInfo.posX, m_CameraInfo.posY, m_CameraInfo.posZ); };

    // Camera.GetForward() -> Vec3
    cameraTable["GetForward"] = [this]() -> Vec3
    { return Vec3(m_CameraInfo.forwardX, m_CameraInfo.forwardY, m_CameraInfo.forwardZ); };

    // Camera.GetUp() -> Vec3
    cameraTable["GetUp"] = [this]() -> Vec3 { return Vec3(m_CameraInfo.upX, m_CameraInfo.upY, m_CameraInfo.upZ); };

    // Camera.GetRight() -> Vec3
    cameraTable["GetRight"] = [this]() -> Vec3
    { return Vec3(m_CameraInfo.rightX, m_CameraInfo.rightY, m_CameraInfo.rightZ); };

    // Camera.ScreenToWorldRay(x, y) -> table { origin=Vec3, direction=Vec3 }
    // Takes viewport-relative coordinates (0,0 = top-left of viewport)
    cameraTable["ScreenToWorldRay"] = [this](float x, float y) -> sol::object
    {
        sol::state& lua = m_Runtime->GetState();

        // Convert viewport coords to normalized device coords
        float ndcX = (2.0f * x / m_ViewportInfo.width) - 1.0f;
        float ndcY = 1.0f - (2.0f * y / m_ViewportInfo.height); // Flip Y

        // Calculate ray direction in world space using FOV and aspect ratio
        float aspectRatio = m_ViewportInfo.width / m_ViewportInfo.height;
        float tanHalfFov = std::tan(m_CameraInfo.fovDegrees * 0.5f * 3.14159265f / 180.0f);

        // Ray in camera space
        float rayX = ndcX * aspectRatio * tanHalfFov;
        float rayY = ndcY * tanHalfFov;
        float rayZ = 1.0f; // Forward is +Z in camera space

        // Transform ray direction to world space using camera basis vectors
        Vec3 right(m_CameraInfo.rightX, m_CameraInfo.rightY, m_CameraInfo.rightZ);
        Vec3 up(m_CameraInfo.upX, m_CameraInfo.upY, m_CameraInfo.upZ);
        Vec3 forward(m_CameraInfo.forwardX, m_CameraInfo.forwardY, m_CameraInfo.forwardZ);

        Vec3 direction(right.x * rayX + up.x * rayY + forward.x * rayZ, right.y * rayX + up.y * rayY + forward.y * rayZ,
                       right.z * rayX + up.z * rayY + forward.z * rayZ);

        // Normalize direction
        float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (len > 0.0001f)
        {
            direction.x /= len;
            direction.y /= len;
            direction.z /= len;
        }

        sol::table result = lua.create_table();
        result["origin"] = Vec3(m_CameraInfo.posX, m_CameraInfo.posY, m_CameraInfo.posZ);
        result["direction"] = direction;
        return sol::make_object(lua, result);
    };

    cameraTable["IsViewmodelPassEnabled"] = [this]() -> bool
    {
        if (const CameraComponent* camera = FindActiveCameraComponent(m_World))
            return camera->enableViewmodelPass;
        return m_CameraInfo.enableViewmodelPass;
    };

    cameraTable["SetViewmodelPassEnabled"] = [this](bool enabled)
    {
        if (CameraComponent* camera = FindActiveCameraComponent(m_World))
            camera->enableViewmodelPass = enabled;
        m_CameraInfo.enableViewmodelPass = enabled;
    };

    cameraTable["GetViewmodelFov"] = [this]() -> float
    {
        if (const CameraComponent* camera = FindActiveCameraComponent(m_World))
            return camera->viewmodelFov;
        return m_CameraInfo.viewmodelFov;
    };

    cameraTable["SetViewmodelFov"] = [this](float fov)
    {
        const float clampedFov = std::clamp(fov, 1.0f, 179.0f);
        if (CameraComponent* camera = FindActiveCameraComponent(m_World))
            camera->viewmodelFov = clampedFov;
        m_CameraInfo.viewmodelFov = clampedFov;
    };

    cameraTable["GetViewmodelNearPlane"] = [this]() -> float
    {
        if (const CameraComponent* camera = FindActiveCameraComponent(m_World))
            return camera->viewmodelNearPlane;
        return m_CameraInfo.viewmodelNearPlane;
    };

    cameraTable["SetViewmodelNearPlane"] = [this](float nearPlane)
    {
        float clampedNearPlane = std::max(0.001f, nearPlane);
        if (CameraComponent* camera = FindActiveCameraComponent(m_World))
        {
            clampedNearPlane = std::min(clampedNearPlane, camera->farPlane - 0.01f);
            camera->viewmodelNearPlane = clampedNearPlane;
        }
        m_CameraInfo.viewmodelNearPlane = clampedNearPlane;
    };

    cameraTable["IsViewmodelLayerVisible"] = [this]() -> bool
    {
        if (const CameraComponent* camera = FindActiveCameraComponent(m_World))
            return (camera->renderMask & RenderLayerMask::Viewmodel) != 0;
        return (m_CameraInfo.renderMask & RenderLayerMask::Viewmodel) != 0;
    };

    cameraTable["SetViewmodelLayerVisible"] = [this](bool visible)
    {
        if (CameraComponent* camera = FindActiveCameraComponent(m_World))
        {
            if (visible)
                camera->renderMask |= RenderLayerMask::Viewmodel;
            else
                camera->renderMask &= ~RenderLayerMask::Viewmodel;
            m_CameraInfo.renderMask = camera->renderMask;
            return;
        }

        if (visible)
            m_CameraInfo.renderMask |= RenderLayerMask::Viewmodel;
        else
            m_CameraInfo.renderMask &= ~RenderLayerMask::Viewmodel;
    };

    cameraTable["HasSocket"] = [this](const std::string& socketName) -> bool
    {
        if (!m_World || socketName.empty())
            return false;
        const Entity activeCamera = FindActiveCameraEntity(*m_World);
        if (!activeCamera.IsValid())
            return false;
        const Entity socket = FindAttachmentSocket(*m_World, activeCamera, socketName);
        return socket.IsValid() && socket != activeCamera;
    };

    cameraTable["GetSocket"] = [this, getSocketProxy](const std::string& socketName) -> sol::object
    {
        sol::state& state = m_Runtime->GetState();
        if (!m_World)
            return sol::make_object(state, sol::nil);
        const Entity activeCamera = FindActiveCameraEntity(*m_World);
        if (!activeCamera.IsValid())
            return sol::make_object(state, sol::nil);
        return getSocketProxy(activeCamera, socketName);
    };

    cameraTable["CreateSocket"] = sol::overload(
        [ensureSocketEntity, this](const std::string& socketName) -> sol::object
        {
            sol::state& state = m_Runtime->GetState();
            if (!m_World)
                return sol::make_object(state, sol::nil);
            const Entity activeCamera = FindActiveCameraEntity(*m_World);
            if (!activeCamera.IsValid())
                return sol::make_object(state, sol::nil);
            const Entity socket = ensureSocketEntity(activeCamera, socketName, Vec3::Zero(), Vec3::Zero());
            if (!socket.IsValid())
                return sol::make_object(state, sol::nil);
            return sol::make_object(state, EntityProxy(socket, m_World));
        },
        [ensureSocketEntity, this](const std::string& socketName, const Vec3& localPosition) -> sol::object
        {
            sol::state& state = m_Runtime->GetState();
            if (!m_World)
                return sol::make_object(state, sol::nil);
            const Entity activeCamera = FindActiveCameraEntity(*m_World);
            if (!activeCamera.IsValid())
                return sol::make_object(state, sol::nil);
            const Entity socket = ensureSocketEntity(activeCamera, socketName, localPosition, Vec3::Zero());
            if (!socket.IsValid())
                return sol::make_object(state, sol::nil);
            return sol::make_object(state, EntityProxy(socket, m_World));
        },
        [ensureSocketEntity, this](const std::string& socketName, const Vec3& localPosition,
                                   const Vec3& localRotation) -> sol::object
        {
            sol::state& state = m_Runtime->GetState();
            if (!m_World)
                return sol::make_object(state, sol::nil);
            const Entity activeCamera = FindActiveCameraEntity(*m_World);
            if (!activeCamera.IsValid())
                return sol::make_object(state, sol::nil);
            const Entity socket = ensureSocketEntity(activeCamera, socketName, localPosition, localRotation);
            if (!socket.IsValid())
                return sol::make_object(state, sol::nil);
            return sol::make_object(state, EntityProxy(socket, m_World));
        });

    // =========================================================================
    // Input extensions - viewport mouse position
    // =========================================================================
    sol::table inputTable = lua["Input"];
    if (inputTable.valid())
    {
        // Input.GetViewportMousePosition() -> Vec2
        // For full-screen game: just use raw screen coordinates
        // If viewport bounds are set, use them; otherwise use screen coords
        inputTable["GetViewportMousePosition"] = [this]() -> Vec2
        {
            POINT pt;
            if (GetCursorPos(&pt))
            {
                // If viewport info is set (non-zero size), use it
                if (m_ViewportInfo.width > 0 && m_ViewportInfo.height > 0)
                {
                    float relX = static_cast<float>(pt.x) - m_ViewportInfo.x;
                    float relY = static_cast<float>(pt.y) - m_ViewportInfo.y;
                    return Vec2(relX, relY);
                }
                // Otherwise just return screen position
                return Vec2(static_cast<float>(pt.x), static_cast<float>(pt.y));
            }
            return Vec2(0, 0);
        };

        // Input.IsMouseInViewport() -> bool
        // For full-screen: always returns true (whole window is the viewport)
        inputTable["IsMouseInViewport"] = [this]() -> bool
        {
            // If viewport bounds not set, assume full screen - always in viewport
            if (m_ViewportInfo.width <= 0 || m_ViewportInfo.height <= 0)
                return true;

            POINT pt;
            if (GetCursorPos(&pt))
            {
                float relX = static_cast<float>(pt.x) - m_ViewportInfo.x;
                float relY = static_cast<float>(pt.y) - m_ViewportInfo.y;
                return relX >= 0 && relX < m_ViewportInfo.width && relY >= 0 && relY < m_ViewportInfo.height;
            }
            return false;
        };
    }

    sol::table uiTable = lua.create_table();
    uiTable["Show"] = [this](const std::string& assetPath) -> bool
    {
        return m_UISystem ? m_UISystem->ShowScreen(assetPath) : false;
    };
    uiTable["Hide"] = [this](const std::string& assetPath) -> bool
    {
        return m_UISystem ? m_UISystem->HideScreen(assetPath) : false;
    };
    uiTable["PushModal"] = [this](const std::string& assetPath) -> bool
    {
        return m_UISystem ? m_UISystem->PushModal(assetPath) : false;
    };
    uiTable["PopModal"] = [this]()
    {
        if (m_UISystem)
            m_UISystem->PopModal();
    };
    uiTable["SetText"] = [this](const std::string& widgetId, const std::string& text) -> bool
    {
        return m_UISystem ? m_UISystem->SetWidgetText(widgetId, text) : false;
    };
    uiTable["SetImage"] = [this](const std::string& widgetId, const std::string& imagePath) -> bool
    {
        return m_UISystem ? m_UISystem->SetWidgetImage(widgetId, imagePath) : false;
    };
    uiTable["SetProgress"] = [this](const std::string& widgetId, float progress) -> bool
    {
        return m_UISystem ? m_UISystem->SetWidgetProgress(widgetId, progress) : false;
    };
    uiTable["SetVisible"] = [this](const std::string& widgetId, bool visible) -> bool
    {
        return m_UISystem ? m_UISystem->SetWidgetVisible(widgetId, visible) : false;
    };
    uiTable["SetEnabled"] = [this](const std::string& widgetId, bool enabled) -> bool
    {
        return m_UISystem ? m_UISystem->SetWidgetEnabled(widgetId, enabled) : false;
    };
    uiTable["Focus"] = [this](const std::string& widgetId)
    {
        if (m_UISystem)
            m_UISystem->SetFocus(widgetId);
    };
    uiTable["GetFocus"] = [this]() -> std::string
    {
        return m_UISystem ? m_UISystem->GetFocus() : std::string{};
    };
    uiTable["OverlayText"] =
        [this](const std::string& id, const std::string& text, float x, float y, float r, float g, float b, float a)
    {
        if (m_UISystem)
            m_UISystem->GetOverlayContext().SetText(id, text, Vec2(x, y), Vec4(r, g, b, a));
    };
    uiTable["OverlayImage"] = [this](const std::string& id, const std::string& imagePath, float x, float y, float width,
                                     float height, float r, float g, float b, float a)
    {
        if (m_UISystem)
            m_UISystem->GetOverlayContext().SetImage(id, imagePath, Vec2(x, y), Vec2(width, height), Vec4(r, g, b, a));
    };
    uiTable["ClearOverlay"] = [this]()
    {
        if (m_UISystem)
            m_UISystem->GetOverlayContext().Clear();
    };
    uiTable["OnClick"] = [this](const std::string& screenPath, const std::string& widgetId, sol::function callback)
    {
        if (!m_Runtime)
            return;
        sol::state& state = m_Runtime->GetState();
        sol::table callbacks = state["__uiClickCallbacks"];
        callbacks[screenPath + "|" + widgetId + "|click"] = callback;
    };
    lua["UI"] = uiTable;

    // =========================================================================
    // Legacy API - Keep for backwards compatibility
    // These functions work with raw index/gen pairs
    // =========================================================================

    // Create EntityRef usertype - a lightweight reference to an entity
    lua.new_usertype<Entity>("Entity", sol::constructors<>(), "GetIndex", &Entity::GetIndex, "IsValid",
                             [](const Entity& e) { return e != kNullEntity; });

    // Legacy transform functions (keep for backwards compatibility)
    lua["GetPosition"] = [this](uint32 entityIndex, uint8 entityGen) -> Vec3
    {
        if (!m_World)
            return Vec3();
        Entity entity(entityIndex, entityGen);
        TransformComponent* transform = m_World->GetComponent<TransformComponent>(entity);
        return transform ? transform->position : Vec3();
    };

    lua["SetPosition"] = [this](uint32 entityIndex, uint8 entityGen, const Vec3& pos)
    {
        if (!m_World)
            return;
        Entity entity(entityIndex, entityGen);
        TransformComponent* transform = m_World->GetComponent<TransformComponent>(entity);
        if (transform)
        {
            transform->position = pos;
            transform->dirty = true;
        }
    };

    lua["GetRotation"] = [this](uint32 entityIndex, uint8 entityGen) -> Vec3
    {
        if (!m_World)
            return Vec3();
        Entity entity(entityIndex, entityGen);
        TransformComponent* transform = m_World->GetComponent<TransformComponent>(entity);
        return transform ? transform->rotation : Vec3();
    };

    lua["SetRotation"] = [this](uint32 entityIndex, uint8 entityGen, const Vec3& rot)
    {
        if (!m_World)
            return;
        Entity entity(entityIndex, entityGen);
        TransformComponent* transform = m_World->GetComponent<TransformComponent>(entity);
        if (transform)
        {
            transform->rotation = rot;
            transform->dirty = true;
        }
    };

    lua["GetScale"] = [this](uint32 entityIndex, uint8 entityGen) -> Vec3
    {
        if (!m_World)
            return Vec3(1, 1, 1);
        Entity entity(entityIndex, entityGen);
        TransformComponent* transform = m_World->GetComponent<TransformComponent>(entity);
        return transform ? transform->scale : Vec3(1, 1, 1);
    };

    lua["SetScale"] = [this](uint32 entityIndex, uint8 entityGen, const Vec3& scale)
    {
        if (!m_World)
            return;
        Entity entity(entityIndex, entityGen);
        TransformComponent* transform = m_World->GetComponent<TransformComponent>(entity);
        if (transform)
        {
            transform->scale = scale;
            transform->dirty = true;
        }
    };

    lua["GetEntityName"] = [this](uint32 entityIndex, uint8 entityGen) -> std::string
    {
        if (!m_World)
            return "";
        Entity entity(entityIndex, entityGen);
        NameComponent* name = m_World->GetComponent<NameComponent>(entity);
        return name ? name->name : "";
    };

    // FindEntityByName (legacy) - returns raw Entity
    lua["FindEntityByName"] = [this](const std::string& name) -> Entity
    {
        Entity found = kNullEntity;
        if (m_World)
        {
            m_World->Each<NameComponent>(
                [&](Entity entity, NameComponent& nameComp)
                {
                    if (nameComp.name == name)
                        found = entity;
                });
        }
        return found;
    };

    // Time globals
    lua["Time"] = lua.create_table_with("deltaTime", 0.0f, "totalTime", 0.0f);

    if (!m_FeatureConfig.enablePhysicsBindings)
        lua.safe_script("EntityProxy.Velocity = nil; EntityProxy.HasRigidBody = nil; EntityProxy.AddForce = nil; EntityProxy.AddImpulse = nil;");

    if (!m_FeatureConfig.enableNavigationBindings)
        lua.safe_script(
            "EntityProxy.MoveTo = nil; EntityProxy.StopMove = nil; EntityProxy.IsMoving = nil; "
            "EntityProxy.GetMoveStatus = nil; Navigation = nil;");

    if (!m_FeatureConfig.enablePerceptionBindings)
        lua.safe_script(
            "EntityProxy.HasLineOfSight = nil; EntityProxy.CanSee = nil; EntityProxy.GetEntitiesInRadius = nil; "
            "Perception = nil;");

    DOT_LOG_INFO("Entity bindings registered (EntityProxy + legacy API)");
}

void ScriptSystem::DispatchDamageEvent(Entity target, float amount, Entity source)
{
    if (!m_World || !m_Runtime || !target.IsValid() || !m_World->IsAlive(target))
        return;

    ScriptComponent* script = m_World->GetComponent<ScriptComponent>(target);
    if (!script || !script->enabled || script->scriptRef < 0)
        return;

    const EntityProxy sourceProxy((source.IsValid() && m_World->IsAlive(source)) ? source : kNullEntity, m_World);
    m_Runtime->CallScriptFunction(script->scriptRef, "OnDamaged", amount, sourceProxy);
}

void ScriptSystem::DispatchDeathEvent(Entity target, Entity source)
{
    if (!m_World || !m_Runtime || !target.IsValid() || !m_World->IsAlive(target))
        return;

    ScriptComponent* script = m_World->GetComponent<ScriptComponent>(target);
    if (!script || !script->enabled || script->scriptRef < 0)
        return;

    const EntityProxy sourceProxy((source.IsValid() && m_World->IsAlive(source)) ? source : kNullEntity, m_World);
    m_Runtime->CallScriptFunction(script->scriptRef, "OnDeath", sourceProxy);
}

void ScriptSystem::DispatchNavigationMoveEvents()
{
    if (!m_World || !m_Runtime || !m_NavigationSystem || m_TrackedNavigationMoves.empty())
        return;

    struct CompletedMoveEvent
    {
        uint64 moveId = 0;
        Entity entity = kNullEntity;
        NavigationMoveStatus status = NavigationMoveStatus::Failed;
    };

    std::vector<CompletedMoveEvent> completedMoves;
    completedMoves.reserve(m_TrackedNavigationMoves.size());

    for (const auto& [moveId, trackedMove] : m_TrackedNavigationMoves)
    {
        const NavigationMoveStatus status = m_NavigationSystem->GetMoveStatus(moveId);
        if (status == NavigationMoveStatus::Pending || status == NavigationMoveStatus::Moving)
            continue;

        completedMoves.push_back(CompletedMoveEvent{moveId, trackedMove.entity, status});
    }

    if (completedMoves.empty())
        return;

    sol::state& lua = m_Runtime->GetState();
    sol::table callbacks = lua["__navMoveCallbacks"];

    auto reportLuaError = [this](const sol::protected_function_result& result)
    {
        if (result.valid())
            return;

        sol::error err = result;
        DOT_LOG_ERROR("[Lua Error] %s", err.what());
        if (m_ConsoleCallback)
            m_ConsoleCallback(std::string("[ERROR] ") + err.what());
    };

    for (const CompletedMoveEvent& event : completedMoves)
    {
        const std::string status = ToLuaStatus(event.status);

        if (event.entity.IsValid() && m_World->IsAlive(event.entity))
        {
            ScriptComponent* script = m_World->GetComponent<ScriptComponent>(event.entity);
            if (script && script->enabled && script->scriptRef >= 0)
            {
                const std::string tableName = "script_" + std::to_string(script->scriptRef);
                sol::table scriptTable = lua[tableName];
                sol::object moveFinishedObject = scriptTable["OnMoveFinished"];
                if (moveFinishedObject.valid() && moveFinishedObject.get_type() == sol::type::function)
                {
                    sol::function moveFinished = moveFinishedObject.as<sol::function>();
                    reportLuaError(moveFinished(status, NavigationMoveHandle{event.moveId, m_NavigationSystem}));
                }
            }
        }

        sol::object callbackObject = callbacks[event.moveId];
        if (callbackObject.valid() && callbackObject.get_type() == sol::type::function)
        {
            sol::function callback = callbackObject.as<sol::function>();
            reportLuaError(callback(status, NavigationMoveHandle{event.moveId, m_NavigationSystem}));
        }

        callbacks[event.moveId] = sol::nil;
        m_TrackedNavigationMoves.erase(event.moveId);
    }
}

void ScriptSystem::ClearNavigationMoveCallbacks()
{
    m_TrackedNavigationMoves.clear();

    if (!m_Runtime)
        return;

    sol::state& lua = m_Runtime->GetState();
    lua["__navMoveCallbacks"] = lua.create_table();
}

void ScriptSystem::ClearTrackedNavigationMovesForEntity(Entity entity)
{
    if (!entity.IsValid() || m_TrackedNavigationMoves.empty())
        return;

    sol::table callbacks;
    const bool hasCallbacksTable = m_Runtime && (callbacks = m_Runtime->GetState()["__navMoveCallbacks"]).valid();

    for (auto it = m_TrackedNavigationMoves.begin(); it != m_TrackedNavigationMoves.end();)
    {
        if (it->second.entity != entity)
        {
            ++it;
            continue;
        }

        if (hasCallbacksTable)
            callbacks[it->first] = sol::nil;
        it = m_TrackedNavigationMoves.erase(it);
    }
}

void ScriptSystem::DispatchCollisionEvents()
{
    if (!m_World || !m_Runtime || !m_PhysicsSystem)
        return;

    auto dispatchToScript =
        [this](Entity targetEntity, Entity otherEntity, const CollisionState& state, const char* functionName)
    {
        if (!targetEntity.IsValid() || !otherEntity.IsValid())
            return;

        ScriptComponent* script = m_World->GetComponent<ScriptComponent>(targetEntity);
        if (!script || !script->enabled || script->scriptRef < 0)
            return;

        sol::state& lua = m_Runtime->GetState();
        std::string tableName = "script_" + std::to_string(script->scriptRef);
        sol::table scriptTable = lua[tableName];
        if (!scriptTable.valid())
            return;

        sol::function func = scriptTable[functionName];
        if (!func.valid())
            return;

        sol::table eventData = lua.create_table();
        eventData["point"] = Vec3(state.pointX, state.pointY, state.pointZ);
        eventData["normal"] = Vec3(state.normalX, state.normalY, state.normalZ);
        eventData["depth"] = state.depth;
        eventData["isTrigger"] = state.isTrigger;
        eventData["self"] = EntityProxy(targetEntity, m_World);
        eventData["other"] = EntityProxy(otherEntity, m_World);

        auto result = func(EntityProxy(otherEntity, m_World), eventData);
        if (!result.valid())
        {
            sol::error err = result;
            DOT_LOG_ERROR("[Lua Error] %s", err.what());
            if (m_ConsoleCallback)
                m_ConsoleCallback(std::string("[ERROR] ") + err.what());
        }
    };

    std::unordered_map<unsigned long long, CollisionState> currentCollisionStates;
    size_t expectedPairs = m_PhysicsSystem->GetCollisionPairs().size();
    if (m_CharacterControllerSystem)
        expectedPairs += m_CharacterControllerSystem->GetTriggerPairs().size();
    currentCollisionStates.reserve(expectedPairs);

    for (const CollisionPair& pair : m_PhysicsSystem->GetCollisionPairs())
    {
        const bool isTrigger = IsTriggerEntity(*m_World, pair.entityA) || IsTriggerEntity(*m_World, pair.entityB);
        if (pair.manifold.numContacts <= 0)
            continue;

        const ContactPoint& contact = pair.manifold.contacts[0];
        const unsigned long long key = MakeCollisionKey(pair.entityA, pair.entityB, isTrigger);

        CollisionState stateA;
        stateA.entityA = pair.entityA;
        stateA.entityB = pair.entityB;
        stateA.sourceEntity = pair.entityA;
        stateA.otherEntity = pair.entityB;
        stateA.pointX = contact.point.x;
        stateA.pointY = contact.point.y;
        stateA.pointZ = contact.point.z;
        stateA.normalX = contact.normal.x;
        stateA.normalY = contact.normal.y;
        stateA.normalZ = contact.normal.z;
        stateA.depth = contact.depth;
        stateA.isTrigger = isTrigger;
        currentCollisionStates[key] = stateA;

        const bool existedLastFrame = m_PreviousCollisionStates.find(key) != m_PreviousCollisionStates.end();
        const char* enterOrStayName = isTrigger ? (existedLastFrame ? "OnTriggerStay" : "OnTriggerEnter")
                                                : (existedLastFrame ? "OnCollisionStay" : "OnCollisionEnter");

        dispatchToScript(pair.entityA, pair.entityB, stateA, enterOrStayName);

        CollisionState stateB = stateA;
        stateB.sourceEntity = pair.entityB;
        stateB.otherEntity = pair.entityA;
        stateB.normalX = -stateA.normalX;
        stateB.normalY = -stateA.normalY;
        stateB.normalZ = -stateA.normalZ;
        dispatchToScript(pair.entityB, pair.entityA, stateB, enterOrStayName);
    }

    if (m_CharacterControllerSystem)
    {
        for (const CharacterControllerTriggerPair& pair : m_CharacterControllerSystem->GetTriggerPairs())
        {
            if (pair.manifold.numContacts <= 0)
                continue;

            const ContactPoint& contact = pair.manifold.contacts[0];
            const bool isTrigger = true;
            const unsigned long long key = MakeCollisionKey(pair.controllerEntity, pair.triggerEntity, isTrigger);

            CollisionState stateA;
            stateA.entityA = pair.controllerEntity;
            stateA.entityB = pair.triggerEntity;
            stateA.sourceEntity = pair.controllerEntity;
            stateA.otherEntity = pair.triggerEntity;
            stateA.pointX = contact.point.x;
            stateA.pointY = contact.point.y;
            stateA.pointZ = contact.point.z;
            stateA.normalX = contact.normal.x;
            stateA.normalY = contact.normal.y;
            stateA.normalZ = contact.normal.z;
            stateA.depth = contact.depth;
            stateA.isTrigger = true;
            currentCollisionStates[key] = stateA;

            const bool existedLastFrame = m_PreviousCollisionStates.find(key) != m_PreviousCollisionStates.end();
            const char* enterOrStayName = existedLastFrame ? "OnTriggerStay" : "OnTriggerEnter";

            dispatchToScript(pair.controllerEntity, pair.triggerEntity, stateA, enterOrStayName);

            CollisionState stateB = stateA;
            stateB.sourceEntity = pair.triggerEntity;
            stateB.otherEntity = pair.controllerEntity;
            stateB.normalX = -stateA.normalX;
            stateB.normalY = -stateA.normalY;
            stateB.normalZ = -stateA.normalZ;
            dispatchToScript(pair.triggerEntity, pair.controllerEntity, stateB, enterOrStayName);
        }
    }

    for (const auto& [key, previousState] : m_PreviousCollisionStates)
    {
        if (currentCollisionStates.find(key) != currentCollisionStates.end())
            continue;

        const char* exitName = previousState.isTrigger ? "OnTriggerExit" : "OnCollisionExit";
        dispatchToScript(previousState.entityA, previousState.entityB, previousState, exitName);

        CollisionState reversed = previousState;
        reversed.sourceEntity = previousState.entityB;
        reversed.otherEntity = previousState.entityA;
        reversed.normalX = -previousState.normalX;
        reversed.normalY = -previousState.normalY;
        reversed.normalZ = -previousState.normalZ;
        dispatchToScript(previousState.entityB, previousState.entityA, reversed, exitName);
    }

    m_PreviousCollisionStates.swap(currentCollisionStates);
}

// =============================================================================
// Hot Reloading
// =============================================================================

void ScriptSystem::SetHotReloadEnabled(bool enabled)
{
    m_HotReloadEnabled = enabled;

    if (enabled && m_FileWatcher && !m_FileWatcher->IsWatching())
    {
        m_FileWatcher->Start(m_ScriptBasePath, true);
    }
    else if (!enabled && m_FileWatcher && m_FileWatcher->IsWatching())
    {
        m_FileWatcher->Stop();
    }

    DOT_LOG_INFO("Script hot reloading {}", enabled ? "enabled" : "disabled");
}

void ScriptSystem::PollFileChanges()
{
    if (!m_HotReloadEnabled || !m_FileWatcher || !m_IsPlaying)
        return;

    // Start watching if not already (lazily start when play begins)
    if (!m_FileWatcher->IsWatching())
    {
        m_FileWatcher->Start(m_ScriptBasePath, true);
    }

    m_FileWatcher->PollChanges();
}

void ScriptSystem::ReloadScriptsByPath(const std::string& changedPath)
{
    if (!m_World || !m_Runtime)
        return;

    // Normalize path separators for comparison
    std::string normalizedChanged = changedPath;
    std::replace(normalizedChanged.begin(), normalizedChanged.end(), '\\', '/');

    int reloadCount = 0;

    m_World->Each<ScriptComponent>(
        [&](Entity entity, ScriptComponent& script)
        {
            std::string scriptFullPath = m_ScriptBasePath + script.scriptPath;
            std::replace(scriptFullPath.begin(), scriptFullPath.end(), '\\', '/');

            if (scriptFullPath == normalizedChanged)
            {
                DOT_LOG_INFO("Hot reloading script: {}", script.scriptPath);

                if (m_ConsoleCallback)
                {
                    m_ConsoleCallback("[Hot Reload] Reloading: " + script.scriptPath);
                }

                if (ReloadScript(entity))
                {
                    reloadCount++;
                }
            }
        });

    if (reloadCount > 0)
    {
        DOT_LOG_INFO("Hot reloaded {} script instance(s)", reloadCount);
    }
}

} // namespace Dot
