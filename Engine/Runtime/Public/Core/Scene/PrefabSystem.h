// =============================================================================
// Dot Engine - Prefab System
// =============================================================================
// System for creating, saving, loading, and instantiating prefabs.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/Math/Vec3.h"
#include "Core/Scene/Prefab.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

// Forward declarations
class World;

/// Prefab System - manages prefab creation and instantiation.
///
/// Usage Example:
/// @code
///     // Create a prefab from an existing entity hierarchy
///     Entity player = scene.GetSelectedEntity();
///     Prefab prefab = PrefabSystem::CreateFromEntity(world, player, "Player");
///     prefab.SaveToFile("Assets/Prefabs/Player.prefab");
///
///     // Later, instantiate the prefab
///     Entity instance = PrefabSystem::InstantiateFromFile(
///         world,
///         "Assets/Prefabs/Player.prefab",
///         Vec3(10, 0, 0)
///     );
/// @endcode
class DOT_CORE_API PrefabSystem
{
public:
    /// Create a prefab from an entity and its children.
    /// Captures the full hierarchy, all components, and their values.
    /// @param world The world containing the entity
    /// @param root The root entity to create prefab from
    /// @param prefabName Name for the prefab (defaults to entity name)
    /// @return The created prefab
    static Prefab CreateFromEntity(World& world, Entity root, const std::string& prefabName = "");

    /// Instantiate a prefab into the world.
    /// Creates new entities with all components from the prefab.
    /// @param world The world to instantiate into
    /// @param prefab The prefab to instantiate
    /// @param position Optional position offset for the root entity
    /// @param parent Optional parent entity (kNullEntity for root-level)
    /// @return The root entity of the instantiated prefab
    static Entity Instantiate(World& world, const Prefab& prefab, const Vec3& position = Vec3::Zero(),
                              Entity parent = Entity());

    /// Load a prefab file and instantiate it.
    /// Combines LoadFromFile + Instantiate in one step.
    /// @param world The world to instantiate into
    /// @param path Path to the .prefab file
    /// @param position Position for the root entity
    /// @param parent Optional parent entity
    /// @return The root entity, or kNullEntity on failure
    static Entity InstantiateFromFile(World& world, const std::string& path, const Vec3& position = Vec3::Zero(),
                                      Entity parent = Entity());

    /// Generate a unique instance ID for prefab instances
    static uint32 GenerateInstanceId();

private:
    /// Recursively collect entity and children into PrefabEntities
    static void CollectEntityHierarchy(World& world, Entity entity, Prefab& prefab, int parentIndex,
                                       const std::unordered_map<uint32, int>& entityToPrefabIndex);

    /// Serialize a single entity's components to JSON
    static void SerializeEntityComponents(World& world, Entity entity, PrefabEntity& prefabEntity,
                                          const std::unordered_map<uint32, int>& entityToPrefabIndex);

    /// Deserialize and apply components from prefab data
    static void DeserializeEntityComponents(World& world, Entity entity, const PrefabEntity& prefabEntity,
                                            const std::vector<Entity>& createdEntities);

    /// Counter for unique instance IDs
    static uint32 s_NextInstanceId;
};

} // namespace Dot
