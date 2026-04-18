// =============================================================================
// Dot Engine - Prefab
// =============================================================================
// Prefab asset representing a reusable entity hierarchy template.
// Inspired by Unity prefabs with Unreal's Blueprint-style extensibility.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec3.h"

#include <map>
#include <string>
#include <vector>

namespace Dot
{

// =============================================================================
// Prefab Entity - Single entity within a prefab
// =============================================================================

/// Represents one entity in a prefab hierarchy
struct DOT_CORE_API PrefabEntity
{
    /// Display name of the entity
    std::string name;

    /// Index of parent entity (-1 for root entities)
    int parentIndex = -1;

    /// Components stored as typeName -> JSON data
    /// Using map for deterministic iteration order
    std::map<std::string, std::string> components;

    /// Original entity index (used during creation, not serialized)
    int sourceIndex = -1;
};

// =============================================================================
// Prefab - Complete prefab asset
// =============================================================================

/// Prefab asset containing a hierarchy of entities and their components.
/// Can be saved to/loaded from .prefab files (JSON format).
///
/// Usage:
/// @code
///     // Create from existing entity
///     Prefab prefab = PrefabSystem::CreateFromEntity(world, selectedEntity);
///     prefab.SaveToFile("Assets/Prefabs/Enemy.prefab");
///
///     // Instantiate
///     Entity instance = PrefabSystem::Instantiate(world, prefab, Vec3(0, 5, 0));
/// @endcode
class DOT_CORE_API Prefab
{
public:
    Prefab() = default;
    explicit Prefab(const std::string& name) : m_Name(name) {}

    // =========================================================================
    // Properties
    // =========================================================================

    /// Get/set prefab name
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    /// Get source file path (empty if not loaded from file)
    const std::string& GetSourcePath() const { return m_SourcePath; }

    /// Access entities
    std::vector<PrefabEntity>& GetEntities() { return m_Entities; }
    const std::vector<PrefabEntity>& GetEntities() const { return m_Entities; }

    /// Add an entity to the prefab
    void AddEntity(const PrefabEntity& entity) { m_Entities.push_back(entity); }

    /// Get entity count
    size_t GetEntityCount() const { return m_Entities.size(); }

    /// Check if prefab is valid (has at least one entity)
    bool IsValid() const { return !m_Entities.empty(); }

    // =========================================================================
    // Serialization
    // =========================================================================

    /// Save prefab to a .prefab file (JSON format)
    /// @param path File path to save to
    /// @return true on success
    bool SaveToFile(const std::string& path) const;

    /// Load prefab from a .prefab file
    /// @param path File path to load from
    /// @return true on success
    bool LoadFromFile(const std::string& path);

    /// Serialize to JSON string
    std::string ToJson() const;

    /// Deserialize from JSON string
    bool FromJson(const std::string& json);

private:
    std::string m_Name;
    std::string m_SourcePath;
    std::vector<PrefabEntity> m_Entities;
};

} // namespace Dot
