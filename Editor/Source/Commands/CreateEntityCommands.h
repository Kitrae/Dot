// =============================================================================
// Dot Engine - Entity Commands
// =============================================================================
// Commands for creating, duplicating, and deleting entities.
// =============================================================================

#pragma once

#include "Core/ECS/Entity.h"
#include "Core/ECS/World.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MapComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/MaterialComponent.h"
#include "Core/Scene/PrefabComponent.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scene/SkyboxComponent.h"
#include "Core/Gameplay/HealthComponent.h"
#include "Core/Input/PlayerInputComponent.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"

#include "Command.h"
#include "CommandRegistry.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Dot
{

struct EntityComponentSnapshot
{
    std::optional<NameComponent> name;
    std::optional<TransformComponent> transform;
    std::optional<HierarchyComponent> hierarchy;
    std::optional<ActiveComponent> active;
    std::optional<PrimitiveComponent> primitive;
    std::optional<DirectionalLightComponent> directionalLight;
    std::optional<PointLightComponent> pointLight;
    std::optional<SpotLightComponent> spotLight;
    std::optional<AmbientLightComponent> ambientLight;
    std::optional<MeshComponent> mesh;
    std::optional<MaterialComponent> material;
    std::optional<MapComponent> map;
    std::optional<NavAgentComponent> navAgent;
    std::optional<SkyboxComponent> skybox;
    std::optional<CameraComponent> camera;
    std::optional<RigidBodyComponent> rigidBody;
    std::optional<BoxColliderComponent> boxCollider;
    std::optional<SphereColliderComponent> sphereCollider;
    std::optional<CharacterControllerComponent> characterController;
    std::optional<PlayerInputComponent> playerInput;
    std::optional<HealthComponent> health;
    std::optional<ScriptComponent> script;
    std::optional<PrefabComponent> prefab;
};

struct EntityClipboardEntry
{
    Entity sourceEntity = kNullEntity;
    EntityComponentSnapshot snapshot;
};

template <typename T>
inline void ApplyOptionalComponent(World& world, Entity entity, const std::optional<T>& component)
{
    if (component)
    {
        world.AddComponent<T>(entity) = *component;
    }
    else if (world.GetComponent<T>(entity))
    {
        world.RemoveComponent<T>(entity);
    }
}

inline EntityComponentSnapshot CaptureEntitySnapshot(World& world, Entity entity)
{
    EntityComponentSnapshot snapshot;

    if (auto* component = world.GetComponent<NameComponent>(entity))
        snapshot.name = *component;
    if (auto* component = world.GetComponent<TransformComponent>(entity))
        snapshot.transform = *component;
    if (auto* component = world.GetComponent<HierarchyComponent>(entity))
        snapshot.hierarchy = *component;
    if (auto* component = world.GetComponent<ActiveComponent>(entity))
        snapshot.active = *component;
    if (auto* component = world.GetComponent<PrimitiveComponent>(entity))
        snapshot.primitive = *component;
    if (auto* component = world.GetComponent<DirectionalLightComponent>(entity))
        snapshot.directionalLight = *component;
    if (auto* component = world.GetComponent<PointLightComponent>(entity))
        snapshot.pointLight = *component;
    if (auto* component = world.GetComponent<SpotLightComponent>(entity))
        snapshot.spotLight = *component;
    if (auto* component = world.GetComponent<AmbientLightComponent>(entity))
        snapshot.ambientLight = *component;
    if (auto* component = world.GetComponent<MeshComponent>(entity))
        snapshot.mesh = *component;
    if (auto* component = world.GetComponent<MaterialComponent>(entity))
        snapshot.material = *component;
    if (auto* component = world.GetComponent<MapComponent>(entity))
        snapshot.map = *component;
    if (auto* component = world.GetComponent<NavAgentComponent>(entity))
        snapshot.navAgent = *component;
    if (auto* component = world.GetComponent<SkyboxComponent>(entity))
        snapshot.skybox = *component;
    if (auto* component = world.GetComponent<CameraComponent>(entity))
        snapshot.camera = *component;
    if (auto* component = world.GetComponent<RigidBodyComponent>(entity))
        snapshot.rigidBody = *component;
    if (auto* component = world.GetComponent<BoxColliderComponent>(entity))
        snapshot.boxCollider = *component;
    if (auto* component = world.GetComponent<SphereColliderComponent>(entity))
        snapshot.sphereCollider = *component;
    if (auto* component = world.GetComponent<CharacterControllerComponent>(entity))
        snapshot.characterController = *component;
    if (auto* component = world.GetComponent<PlayerInputComponent>(entity))
        snapshot.playerInput = *component;
    if (auto* component = world.GetComponent<HealthComponent>(entity))
        snapshot.health = *component;
    if (auto* component = world.GetComponent<ScriptComponent>(entity))
        snapshot.script = *component;
    if (auto* component = world.GetComponent<PrefabComponent>(entity))
        snapshot.prefab = *component;

    return snapshot;
}

inline void RestoreEntitySnapshot(World& world, Entity entity, const EntityComponentSnapshot& snapshot)
{
    ApplyOptionalComponent(world, entity, snapshot.name);
    ApplyOptionalComponent(world, entity, snapshot.transform);
    ApplyOptionalComponent(world, entity, snapshot.hierarchy);
    ApplyOptionalComponent(world, entity, snapshot.active);
    ApplyOptionalComponent(world, entity, snapshot.primitive);
    ApplyOptionalComponent(world, entity, snapshot.directionalLight);
    ApplyOptionalComponent(world, entity, snapshot.pointLight);
    ApplyOptionalComponent(world, entity, snapshot.spotLight);
    ApplyOptionalComponent(world, entity, snapshot.ambientLight);
    ApplyOptionalComponent(world, entity, snapshot.mesh);
    ApplyOptionalComponent(world, entity, snapshot.material);
    ApplyOptionalComponent(world, entity, snapshot.map);
    ApplyOptionalComponent(world, entity, snapshot.navAgent);
    ApplyOptionalComponent(world, entity, snapshot.skybox);
    ApplyOptionalComponent(world, entity, snapshot.camera);
    ApplyOptionalComponent(world, entity, snapshot.rigidBody);
    ApplyOptionalComponent(world, entity, snapshot.boxCollider);
    ApplyOptionalComponent(world, entity, snapshot.sphereCollider);
    ApplyOptionalComponent(world, entity, snapshot.characterController);
    ApplyOptionalComponent(world, entity, snapshot.playerInput);
    ApplyOptionalComponent(world, entity, snapshot.health);
    ApplyOptionalComponent(world, entity, snapshot.script);
    ApplyOptionalComponent(world, entity, snapshot.prefab);
}

class EntitySnapshotCommand : public Command
{
public:
    EntitySnapshotCommand(World* world, Entity entity, EntityComponentSnapshot before, EntityComponentSnapshot after,
                          std::string name = "Edit Entity")
        : m_World(world), m_Entity(entity), m_Before(std::move(before)), m_After(std::move(after)),
          m_Name(std::move(name))
    {
    }

    void Execute() override
    {
        if (!m_World || !m_Entity.IsValid() || !m_World->IsAlive(m_Entity))
            return;

        RestoreEntitySnapshot(*m_World, m_Entity, m_After);
    }

    void Undo() override
    {
        if (!m_World || !m_Entity.IsValid() || !m_World->IsAlive(m_Entity))
            return;

        RestoreEntitySnapshot(*m_World, m_Entity, m_Before);
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return m_Name.c_str(); }

private:
    World* m_World = nullptr;
    Entity m_Entity = kNullEntity;
    EntityComponentSnapshot m_Before;
    EntityComponentSnapshot m_After;
    std::string m_Name;
};

// =============================================================================
// Create Commands
// =============================================================================

/// Command to create an empty entity
class CreateEmptyEntityCommand : public Command
{
public:
    CreateEmptyEntityCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "New Entity";
        m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity); // For parent/child support

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Empty Entity"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateEmptyEntityCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a primitive entity
class CreatePrimitiveCommand : public Command
{
public:
    CreatePrimitiveCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Primitive";
        m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity); // For parent/child support
        m_World->AddComponent<PrimitiveComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Primitive"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreatePrimitiveCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

// =============================================================================
// Duplicate Command
// =============================================================================

/// Command to duplicate an entity with all its components
class DuplicateEntityCommand : public Command
{
public:
    DuplicateEntityCommand(World* world, Entity source, Entity* outEntity)
        : m_World(world), m_SourceEntity(source), m_OutEntity(outEntity)
    {
    }

    void Execute() override
    {
        if (!m_World || !m_SourceEntity.IsValid())
            return;

        m_DuplicatedEntity = m_World->CreateEntity();
        EntityComponentSnapshot snapshot = CaptureEntitySnapshot(*m_World, m_SourceEntity);
        if (snapshot.name)
            snapshot.name->name += " (Copy)";
        if (snapshot.hierarchy)
        {
            snapshot.hierarchy->parent = kNullEntity;
            snapshot.hierarchy->children.clear();
        }
        RestoreEntitySnapshot(*m_World, m_DuplicatedEntity, snapshot);

        if (m_OutEntity)
            *m_OutEntity = m_DuplicatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_DuplicatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_DuplicatedEntity);
            m_DuplicatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Duplicate Entity"; }

private:
    World* m_World = nullptr;
    Entity m_SourceEntity = kNullEntity;
    Entity* m_OutEntity = nullptr;
    Entity m_DuplicatedEntity = kNullEntity;
};

/// Command to paste an entity from a stored component snapshot
class PasteEntitySnapshotCommand : public Command
{
public:
    PasteEntitySnapshotCommand(World* world, EntityComponentSnapshot snapshot, Entity* outEntity, bool appendCopySuffix)
        : m_World(world), m_Snapshot(std::move(snapshot)), m_OutEntity(outEntity), m_AppendCopySuffix(appendCopySuffix)
    {
    }

    void Execute() override
    {
        if (!m_World)
            return;

        m_PastedEntity = m_World->CreateEntity();
        EntityComponentSnapshot snapshot = m_Snapshot;
        if (snapshot.name && m_AppendCopySuffix)
            snapshot.name->name += " (Copy)";
        if (snapshot.hierarchy)
        {
            snapshot.hierarchy->parent = kNullEntity;
            snapshot.hierarchy->children.clear();
        }
        RestoreEntitySnapshot(*m_World, m_PastedEntity, snapshot);

        if (m_OutEntity)
            *m_OutEntity = m_PastedEntity;
    }

    void Undo() override
    {
        if (m_World && m_PastedEntity.IsValid())
        {
            m_World->DestroyEntity(m_PastedEntity);
            m_PastedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Paste Entity"; }

private:
    World* m_World = nullptr;
    EntityComponentSnapshot m_Snapshot;
    Entity* m_OutEntity = nullptr;
    Entity m_PastedEntity = kNullEntity;
    bool m_AppendCopySuffix = true;
};

/// Command to paste multiple entities from stored component snapshots
class PasteEntitiesCommand : public Command
{
public:
    PasteEntitiesCommand(World* world, std::vector<EntityClipboardEntry> entries, std::vector<Entity>* outEntities,
                         Entity* outPrimaryEntity, bool appendCopySuffix)
        : m_World(world), m_Entries(std::move(entries)), m_OutEntities(outEntities), m_OutPrimaryEntity(outPrimaryEntity),
          m_AppendCopySuffix(appendCopySuffix)
    {
    }

    void Execute() override
    {
        if (!m_World || m_Entries.empty())
            return;

        m_PastedEntities.clear();
        m_PastedEntities.reserve(m_Entries.size());

        std::unordered_map<uint32, Entity> remap;
        remap.reserve(m_Entries.size());
        for (const EntityClipboardEntry& entry : m_Entries)
        {
            Entity pastedEntity = m_World->CreateEntity();
            m_PastedEntities.push_back(pastedEntity);
            remap[entry.sourceEntity.id] = pastedEntity;
        }

        for (size_t i = 0; i < m_Entries.size(); ++i)
        {
            EntityComponentSnapshot snapshot = m_Entries[i].snapshot;
            if (snapshot.name && m_AppendCopySuffix)
                snapshot.name->name += " (Copy)";

            if (snapshot.hierarchy)
            {
                HierarchyComponent hierarchy = *snapshot.hierarchy;

                const auto parentIt = remap.find(hierarchy.parent.id);
                hierarchy.parent = (parentIt != remap.end()) ? parentIt->second : kNullEntity;

                std::vector<Entity> remappedChildren;
                remappedChildren.reserve(hierarchy.children.size());
                for (Entity child : hierarchy.children)
                {
                    const auto childIt = remap.find(child.id);
                    if (childIt != remap.end())
                        remappedChildren.push_back(childIt->second);
                }
                hierarchy.children = std::move(remappedChildren);
                snapshot.hierarchy = hierarchy;
            }

            RestoreEntitySnapshot(*m_World, m_PastedEntities[i], snapshot);
        }

        if (m_OutEntities)
            *m_OutEntities = m_PastedEntities;
        if (m_OutPrimaryEntity)
            *m_OutPrimaryEntity = m_PastedEntities.empty() ? kNullEntity : m_PastedEntities.back();
    }

    void Undo() override
    {
        if (!m_World)
            return;

        for (auto it = m_PastedEntities.rbegin(); it != m_PastedEntities.rend(); ++it)
        {
            if (it->IsValid() && m_World->IsAlive(*it))
                m_World->DestroyEntity(*it);
        }

        if (m_OutEntities)
            m_OutEntities->clear();
        if (m_OutPrimaryEntity)
            *m_OutPrimaryEntity = kNullEntity;
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return (m_Entries.size() > 1) ? "Paste Entities" : "Paste Entity"; }

private:
    World* m_World = nullptr;
    std::vector<EntityClipboardEntry> m_Entries;
    std::vector<Entity>* m_OutEntities = nullptr;
    Entity* m_OutPrimaryEntity = nullptr;
    std::vector<Entity> m_PastedEntities;
    bool m_AppendCopySuffix = true;
};

// =============================================================================
// Delete Command
// =============================================================================

/// Command to delete an entity (stores state for undo)
class DeleteEntityCommand : public Command
{
public:
    DeleteEntityCommand(World* world, Entity entity, Entity* selectedEntity)
        : m_World(world), m_Entity(entity), m_SelectedEntity(selectedEntity)
    {
    }

    void Execute() override
    {
        if (!m_World || !m_Entity.IsValid())
            return;

        m_Snapshot = CaptureEntitySnapshot(*m_World, m_Entity);

        if (m_Snapshot.hierarchy)
        {
            if (m_Snapshot.hierarchy->parent.IsValid() && m_World->IsAlive(m_Snapshot.hierarchy->parent))
            {
                if (auto* parentHierarchy = m_World->GetComponent<HierarchyComponent>(m_Snapshot.hierarchy->parent))
                    parentHierarchy->RemoveChild(m_Entity);
            }
        }

        // Clear selection if this was selected
        if (m_SelectedEntity && *m_SelectedEntity == m_Entity)
            *m_SelectedEntity = kNullEntity;

        m_World->DestroyEntity(m_Entity);
        m_WasDeleted = true;
    }

    void Undo() override
    {
        if (!m_World || !m_WasDeleted)
            return;

        // Recreate entity
        m_Entity = m_World->CreateEntity();
        RestoreEntitySnapshot(*m_World, m_Entity, m_Snapshot);

        if (m_Snapshot.hierarchy)
        {
            if (m_Snapshot.hierarchy->parent.IsValid() && m_World->IsAlive(m_Snapshot.hierarchy->parent))
            {
                if (auto* parentHierarchy = m_World->GetComponent<HierarchyComponent>(m_Snapshot.hierarchy->parent))
                {
                    const bool alreadyListed =
                        std::find(parentHierarchy->children.begin(), parentHierarchy->children.end(), m_Entity) !=
                        parentHierarchy->children.end();
                    if (!alreadyListed)
                        parentHierarchy->AddChild(m_Entity);
                }
            }

            for (Entity child : m_Snapshot.hierarchy->children)
            {
                if (!child.IsValid() || !m_World->IsAlive(child))
                    continue;

                if (auto* childHierarchy = m_World->GetComponent<HierarchyComponent>(child))
                    childHierarchy->parent = m_Entity;
            }
        }

        // Restore selection
        if (m_SelectedEntity)
            *m_SelectedEntity = m_Entity;

        m_WasDeleted = false;
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Delete Entity"; }

private:
    World* m_World = nullptr;
    Entity m_Entity = kNullEntity;
    Entity* m_SelectedEntity = nullptr;
    bool m_WasDeleted = false;
    EntityComponentSnapshot m_Snapshot;
};

/// Command to delete multiple entities with grouped undo support
class DeleteEntitiesCommand : public Command
{
public:
    DeleteEntitiesCommand(World* world, std::vector<Entity> entities, Entity* selectedEntity)
        : m_World(world), m_CurrentEntities(std::move(entities)), m_SelectedEntity(selectedEntity)
    {
    }

    void Execute() override
    {
        if (!m_World)
            return;

        std::vector<Entity> liveEntities;
        std::unordered_set<uint32> seen;
        for (Entity entity : m_CurrentEntities)
        {
            if (!entity.IsValid() || !m_World->IsAlive(entity))
                continue;
            if (!seen.insert(entity.id).second)
                continue;
            liveEntities.push_back(entity);
        }

        if (liveEntities.empty())
            return;

        m_DeletedEntries.clear();
        m_DeletedEntries.reserve(liveEntities.size());

        std::unordered_set<uint32> deletedIds;
        deletedIds.reserve(liveEntities.size());
        for (Entity entity : liveEntities)
        {
            deletedIds.insert(entity.id);
            m_DeletedEntries.push_back({entity, CaptureEntitySnapshot(*m_World, entity)});
        }

        for (const EntityClipboardEntry& entry : m_DeletedEntries)
        {
            if (!entry.snapshot.hierarchy)
                continue;

            const HierarchyComponent& hierarchy = *entry.snapshot.hierarchy;
            if (hierarchy.parent.IsValid() && m_World->IsAlive(hierarchy.parent) &&
                deletedIds.find(hierarchy.parent.id) == deletedIds.end())
            {
                if (auto* parentHierarchy = m_World->GetComponent<HierarchyComponent>(hierarchy.parent))
                    parentHierarchy->RemoveChild(entry.sourceEntity);
            }

            for (Entity child : hierarchy.children)
            {
                if (!child.IsValid() || !m_World->IsAlive(child))
                    continue;
                if (deletedIds.find(child.id) != deletedIds.end())
                    continue;

                if (auto* childHierarchy = m_World->GetComponent<HierarchyComponent>(child))
                    childHierarchy->parent = kNullEntity;
            }
        }

        if (m_SelectedEntity && deletedIds.find(m_SelectedEntity->id) != deletedIds.end())
            *m_SelectedEntity = kNullEntity;

        for (auto it = liveEntities.rbegin(); it != liveEntities.rend(); ++it)
            m_World->DestroyEntity(*it);

        m_CurrentEntities = std::move(liveEntities);
    }

    void Undo() override
    {
        if (!m_World || m_DeletedEntries.empty())
            return;

        std::unordered_map<uint32, Entity> remap;
        remap.reserve(m_DeletedEntries.size());

        std::vector<Entity> recreatedEntities;
        recreatedEntities.reserve(m_DeletedEntries.size());
        for (const EntityClipboardEntry& entry : m_DeletedEntries)
        {
            Entity recreated = m_World->CreateEntity();
            recreatedEntities.push_back(recreated);
            remap[entry.sourceEntity.id] = recreated;
        }

        for (size_t i = 0; i < m_DeletedEntries.size(); ++i)
        {
            EntityComponentSnapshot snapshot = m_DeletedEntries[i].snapshot;
            if (snapshot.hierarchy)
            {
                HierarchyComponent hierarchy = *snapshot.hierarchy;

                const auto parentIt = remap.find(hierarchy.parent.id);
                if (parentIt != remap.end())
                    hierarchy.parent = parentIt->second;
                else if (!m_World->IsAlive(hierarchy.parent))
                    hierarchy.parent = kNullEntity;

                std::vector<Entity> remappedChildren;
                remappedChildren.reserve(hierarchy.children.size());
                for (Entity child : hierarchy.children)
                {
                    const auto childIt = remap.find(child.id);
                    if (childIt != remap.end())
                    {
                        remappedChildren.push_back(childIt->second);
                    }
                    else if (m_World->IsAlive(child))
                    {
                        remappedChildren.push_back(child);
                    }
                }
                hierarchy.children = std::move(remappedChildren);
                snapshot.hierarchy = hierarchy;
            }

            RestoreEntitySnapshot(*m_World, recreatedEntities[i], snapshot);
        }

        m_CurrentEntities = recreatedEntities;
        if (m_SelectedEntity)
            *m_SelectedEntity = recreatedEntities.empty() ? kNullEntity : recreatedEntities.back();
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return (m_CurrentEntities.size() > 1) ? "Delete Entities" : "Delete Entity"; }

private:
    World* m_World = nullptr;
    std::vector<Entity> m_CurrentEntities;
    Entity* m_SelectedEntity = nullptr;
    std::vector<EntityClipboardEntry> m_DeletedEntries;
};

// =============================================================================
// Light Creation Commands
// =============================================================================

/// Command to create a directional light entity
class CreateDirectionalLightCommand : public Command
{
public:
    CreateDirectionalLightCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Directional Light";
        auto& transform = m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        transform.rotation = {-45.0f, 45.0f, 0.0f}; // Default sun angle
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        m_World->AddComponent<DirectionalLightComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Directional Light"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateDirectionalLightCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a point light entity
class CreatePointLightCommand : public Command
{
public:
    CreatePointLightCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Point Light";
        auto& transform = m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        transform.position = {0.0f, 2.0f, 0.0f}; // Default above origin
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        m_World->AddComponent<PointLightComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Point Light"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreatePointLightCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a spot light entity
class CreateSpotLightCommand : public Command
{
public:
    CreateSpotLightCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Spot Light";
        auto& transform = m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        transform.position = {0.0f, 3.0f, 0.0f};
        transform.rotation = {-90.0f, 0.0f, 0.0f}; // Pointing down
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        m_World->AddComponent<SpotLightComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Spot Light"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateSpotLightCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a reflection probe entity
class CreateReflectionProbeCommand : public Command
{
public:
    CreateReflectionProbeCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Reflection Probe";
        auto& transform = m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        transform.position = {0.0f, 1.5f, 0.0f};
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        auto& probe = m_World->AddComponent<ReflectionProbeComponent>(m_CreatedEntity);
        probe.sourceMode = ReflectionProbeSourceMode::AutoSceneSkybox;

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Reflection Probe"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateReflectionProbeCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a folder entity (for organizational grouping in hierarchy)
class CreateFolderCommand : public Command
{
public:
    CreateFolderCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Folder";
        m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        // No visual components - just for organization

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Folder"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateFolderCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

// =============================================================================
// Transform Command
// =============================================================================

/// Command for transform changes (position, rotation, scale)
class TransformCommand : public Command
{
public:
    TransformCommand(World* world, Entity entity, const Vec3& oldPosition, const Vec3& oldRotation,
                     const Vec3& oldScale, const Vec3& newPosition, const Vec3& newRotation, const Vec3& newScale)
        : m_World(world), m_Entity(entity), m_OldPosition(oldPosition), m_OldRotation(oldRotation),
          m_OldScale(oldScale), m_NewPosition(newPosition), m_NewRotation(newRotation), m_NewScale(newScale)
    {
    }

    void Execute() override
    {
        // Already applied during drag - nothing to do
        // This is called on redo
        ApplyTransform(m_NewPosition, m_NewRotation, m_NewScale);
    }

    void Undo() override { ApplyTransform(m_OldPosition, m_OldRotation, m_OldScale); }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Transform"; }

private:
    void ApplyTransform(const Vec3& pos, const Vec3& rot, const Vec3& scale)
    {
        if (!m_World || !m_Entity.IsValid())
            return;

        auto* transform = m_World->GetComponent<TransformComponent>(m_Entity);
        if (transform)
        {
            transform->position = pos;
            transform->rotation = rot;
            transform->scale = scale;
        }
    }

    World* m_World = nullptr;
    Entity m_Entity = kNullEntity;
    Vec3 m_OldPosition, m_OldRotation, m_OldScale;
    Vec3 m_NewPosition, m_NewRotation, m_NewScale;
};

struct EntityTransformState
{
    Entity entity = kNullEntity;
    Vec3 position = Vec3::Zero();
    Vec3 rotation = Vec3::Zero();
    Vec3 scale = Vec3::One();
};

/// Command for transforming multiple entities at once
class MultiTransformCommand : public Command
{
public:
    MultiTransformCommand(World* world, std::vector<EntityTransformState> before, std::vector<EntityTransformState> after)
        : m_World(world), m_Before(std::move(before)), m_After(std::move(after))
    {
    }

    void Execute() override { Apply(m_After); }

    void Undo() override { Apply(m_Before); }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Transform Entities"; }

private:
    void Apply(const std::vector<EntityTransformState>& states)
    {
        if (!m_World)
            return;

        for (const EntityTransformState& state : states)
        {
            if (!state.entity.IsValid())
                continue;

            auto* transform = m_World->GetComponent<TransformComponent>(state.entity);
            if (!transform)
                continue;

            transform->position = state.position;
            transform->rotation = state.rotation;
            transform->scale = state.scale;
        }
    }

    World* m_World = nullptr;
    std::vector<EntityTransformState> m_Before;
    std::vector<EntityTransformState> m_After;
};

/// Command to create a mesh entity
class CreateMeshEntityCommand : public Command
{
public:
    CreateMeshEntityCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Mesh";
        m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        m_World->AddComponent<MeshComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Mesh"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateMeshEntityCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a skybox entity
class CreateSkyboxCommand : public Command
{
public:
    CreateSkyboxCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Skybox";
        m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        m_World->AddComponent<SkyboxComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Skybox"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateSkyboxCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

/// Command to create a camera entity
class CreateCameraCommand : public Command
{
public:
    CreateCameraCommand(World* world, Entity* outEntity) : m_World(world), m_OutEntity(outEntity) {}

    void Execute() override
    {
        if (!m_World)
            return;

        m_CreatedEntity = m_World->CreateEntity();
        m_World->AddComponent<NameComponent>(m_CreatedEntity).name = "Camera";
        auto& transform = m_World->AddComponent<TransformComponent>(m_CreatedEntity);
        transform.position = {0.0f, 1.0f, -5.0f}; // Default camera position
        m_World->AddComponent<HierarchyComponent>(m_CreatedEntity);
        m_World->AddComponent<CameraComponent>(m_CreatedEntity);

        if (m_OutEntity)
            *m_OutEntity = m_CreatedEntity;
    }

    void Undo() override
    {
        if (m_World && m_CreatedEntity.IsValid())
        {
            m_World->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = kNullEntity;
        }
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return "Create Camera"; }

    static CommandPtr Create(World* world, Entity* outEntity)
    {
        return std::make_unique<CreateCameraCommand>(world, outEntity);
    }

private:
    World* m_World = nullptr;
    Entity* m_OutEntity = nullptr;
    Entity m_CreatedEntity = kNullEntity;
};

// =============================================================================
// Registration
// =============================================================================

/// Register all create commands
inline void RegisterCreateCommands()
{
    auto& registry = CommandRegistry::Get();
    registry.Register("Create/Empty Entity", CreateEmptyEntityCommand::Create);
    registry.Register("Create/Folder", CreateFolderCommand::Create);
    registry.Register("Create/Primitive", CreatePrimitiveCommand::Create);
    registry.Register("Create/Mesh", CreateMeshEntityCommand::Create);
    registry.Register("Create/Camera", CreateCameraCommand::Create);
    registry.Register("Create/Light/Directional Light", CreateDirectionalLightCommand::Create);
    registry.Register("Create/Light/Point Light", CreatePointLightCommand::Create);
    registry.Register("Create/Light/Spot Light", CreateSpotLightCommand::Create);
    registry.Register("Create/Light/Reflection Probe", CreateReflectionProbeCommand::Create);
    registry.Register("Create/Skybox", CreateSkyboxCommand::Create);
}

} // namespace Dot
