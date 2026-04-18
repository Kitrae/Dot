// =============================================================================
// Dot Engine - World
// =============================================================================
// ECS world - manages entities, archetypes, and provides queries.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Archetype.h"
#include "Core/ECS/Entity.h"

#include <functional>
#include <memory>
#include <vector>

namespace Dot
{

/// ECS World - central container for all entities and components
class DOT_CORE_API World
{
public:
    World();
    ~World();

    // Entity operations
    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsAlive(Entity entity) const;
    usize GetEntityCount() const { return m_AliveCount; }

    // Simple iteration over all alive entities
    template <typename Func> void EachEntity(Func func)
    {
        for (uint32 i = 0; i < static_cast<uint32>(m_Alive.size()); ++i)
        {
            if (m_Alive[i])
            {
                Entity entity(i, m_Generations[i]);
                func(entity);
            }
        }
    }

    // Component operations
    template <typename T> T& AddComponent(Entity entity)
    {
        if (!IsAlive(entity))
        {
            static T dummy{};
            return dummy;
        }

        uint32 entityIndex = entity.GetIndex();

        // Bounds check for safety
        if (entityIndex >= m_EntityArchetype.size())
        {
            static T dummy{};
            return dummy;
        }

        ComponentTypeId typeId = GetComponentTypeId<T>();

        // Check if already has this component
        Archetype* oldArch = m_EntityArchetype[entityIndex];
        if (oldArch && oldArch->GetSignature().Has(typeId))
        {
            // Already has component, just return it
            T* comp = oldArch->GetComponent<T>(entity);
            if (comp)
                return *comp;
            static T dummy{};
            return dummy;
        }

        // Get or create archetype with this component
        Archetype* newArch = GetOrCreateArchetype(entity, typeId);
        if (!newArch)
        {
            static T dummy{};
            return dummy;
        }

        // Copy component types from old archetype to new archetype
        if (oldArch)
        {
            newArch->CopyComponentTypesFrom(*oldArch);
        }
        newArch->AddComponentType<T>();

        // Add entity to new archetype FIRST (creates empty component slots)
        usize newRow = 0;
        if (!newArch->HasEntity(entity))
        {
            newRow = newArch->AddEntity(entity);
        }

        // Copy component data from old archetype BEFORE removing
        if (oldArch && oldArch != newArch)
        {
            newArch->CopyComponentDataFrom(*oldArch, entity, newRow);
            oldArch->RemoveEntity(entity);
        }

        m_EntityArchetype[entityIndex] = newArch;

        T* result = newArch->GetComponent<T>(entity);
        if (result)
            return *result;
        static T dummy{};
        return dummy;
    }

    template <typename T> void RemoveComponent(Entity entity)
    {
        if (!IsAlive(entity))
            return;

        ComponentTypeId typeId = GetComponentTypeId<T>();
        uint32 index = entity.GetIndex();

        // Bounds check
        if (index >= m_EntityArchetype.size())
            return;

        Archetype* oldArch = m_EntityArchetype[index];

        if (!oldArch || !oldArch->GetSignature().Has(typeId))
            return; // Entity doesn't have this component

        // Build new signature without this component
        ArchetypeSignature newSig = oldArch->GetSignature();
        newSig.Remove(typeId);

        if (newSig.IsEmpty())
        {
            // No components left, just remove from old archetype
            oldArch->RemoveEntity(entity);
            m_EntityArchetype[index] = nullptr;
            return;
        }

        // Find or create archetype with new signature
        auto it = m_Archetypes.find(newSig);
        Archetype* newArch = nullptr;
        if (it != m_Archetypes.end())
        {
            newArch = it->second.get();
        }
        else
        {
            auto arch = std::make_unique<Archetype>(newSig);
            newArch = arch.get();
            m_Archetypes[newSig] = std::move(arch);
        }

        // Ensure destination archetype has all component arrays except the removed type.
        newArch->CopyComponentTypesFrom(*oldArch, typeId);

        // Add entity to destination first, then copy retained component payloads.
        usize newRow = 0;
        if (!newArch->HasEntity(entity))
            newRow = newArch->AddEntity(entity);

        newArch->CopyComponentDataFrom(*oldArch, entity, newRow);
        oldArch->RemoveEntity(entity);
        m_EntityArchetype[index] = newArch;
    }

    template <typename T> T* GetComponent(Entity entity)
    {
        if (!IsAlive(entity))
            return nullptr;

        uint32 index = entity.GetIndex();
        if (index >= m_EntityArchetype.size())
            return nullptr;

        Archetype* arch = m_EntityArchetype[index];
        if (!arch)
            return nullptr;

        return arch->GetComponent<T>(entity);
    }

    template <typename T> bool HasComponent(Entity entity) const
    {
        if (!IsAlive(entity))
            return false;

        const Archetype* arch = m_EntityArchetype[entity.GetIndex()];
        if (!arch)
            return false;

        return arch->GetSignature().Has(GetComponentTypeId<T>());
    }

    // Query - iterate entities with specific components
    template <typename... Components> void Each(std::function<void(Entity, Components&...)> func)
    {
        ArchetypeSignature required;
        (required.Add(GetComponentTypeId<Components>()), ...);

        for (auto& [sig, arch] : m_Archetypes)
        {
            if (arch->GetSignature().Contains(required))
            {
                for (usize i = 0; i < arch->GetEntityCount(); ++i)
                {
                    Entity entity = arch->GetEntities()[i];
                    func(entity, *arch->GetComponentByRow<Components>(i)...);
                }
            }
        }
    }

    /// Iterate over all archetypes (for debugging/visualization)
    template <typename Func> void EachArchetype(Func func)
    {
        for (auto& [sig, arch] : m_Archetypes)
        {
            func(*arch);
        }
    }

    /// Get the number of archetypes
    usize GetArchetypeCount() const { return m_Archetypes.size(); }

    // Clear everything
    void Clear();

private:
    Archetype* GetOrCreateArchetype(Entity entity, ComponentTypeId newType);

    // Entity storage
    std::vector<uint8> m_Generations;          // Generation per entity slot
    std::vector<bool> m_Alive;                 // Is slot currently alive?
    std::vector<uint32> m_FreeIndices;         // Recycled entity indices
    std::vector<Archetype*> m_EntityArchetype; // Archetype per entity
    usize m_AliveCount = 0;

    // Archetype storage
    std::unordered_map<ArchetypeSignature, std::unique_ptr<Archetype>, ArchetypeSignature::Hash> m_Archetypes;
};

} // namespace Dot
