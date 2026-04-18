// =============================================================================
// Dot Engine - Archetype
// =============================================================================
// Stores entities with the same component composition.
// Components are packed in contiguous arrays for cache efficiency.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/ComponentType.h"
#include "Core/ECS/Entity.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Dot
{

/// Archetype signature - set of component types
class ArchetypeSignature
{
public:
    ArchetypeSignature() = default;

    void Add(ComponentTypeId id)
    {
        if (std::find(m_Types.begin(), m_Types.end(), id) == m_Types.end())
        {
            m_Types.push_back(id);
            std::sort(m_Types.begin(), m_Types.end());
        }
    }

    void Remove(ComponentTypeId id)
    {
        auto it = std::find(m_Types.begin(), m_Types.end(), id);
        if (it != m_Types.end())
        {
            m_Types.erase(it);
        }
    }

    bool Has(ComponentTypeId id) const { return std::find(m_Types.begin(), m_Types.end(), id) != m_Types.end(); }

    bool Contains(const ArchetypeSignature& other) const
    {
        for (auto id : other.m_Types)
        {
            if (!Has(id))
                return false;
        }
        return true;
    }

    const std::vector<ComponentTypeId>& GetTypes() const { return m_Types; }
    usize GetTypeCount() const { return m_Types.size(); }
    bool IsEmpty() const { return m_Types.empty(); }

    bool operator==(const ArchetypeSignature& other) const { return m_Types == other.m_Types; }
    bool operator!=(const ArchetypeSignature& other) const { return m_Types != other.m_Types; }

    struct Hash
    {
        size_t operator()(const ArchetypeSignature& sig) const
        {
            size_t hash = 0;
            for (auto id : sig.m_Types)
            {
                hash ^= std::hash<ComponentTypeId>{}(id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

private:
    std::vector<ComponentTypeId> m_Types;
};

/// Component array - type-erased storage for one component type
class ComponentArray
{
public:
    ComponentArray(const ComponentTypeInfo& info) : m_Info(info) {}

    ~ComponentArray() { Clear(); }

    void* Add()
    {
        EnsureCapacity(m_Size + 1);
        void* ptr = GetRaw(m_Size);
        m_Info.construct(ptr);
        m_Size++;
        return ptr;
    }

    void Remove(usize index)
    {
        if (index >= m_Size)
            return;

        // Destruct the element being removed
        m_Info.destruct(GetRaw(index));

        // Move last element to fill gap (swap-remove)
        if (index < m_Size - 1)
        {
            // Move-construct the last element into the vacated slot
            m_Info.move(GetRaw(index), GetRaw(m_Size - 1));
            // Destruct the moved-from element (it's in a valid but unspecified state)
            m_Info.destruct(GetRaw(m_Size - 1));
        }

        m_Size--;
    }

    void Clear()
    {
        for (usize i = 0; i < m_Size; ++i)
        {
            m_Info.destruct(GetRaw(i));
        }
        m_Size = 0;
    }

    void* GetRaw(usize index) { return m_Data.data() + index * m_Info.size; }
    const void* GetRaw(usize index) const { return m_Data.data() + index * m_Info.size; }

    template <typename T> T& Get(usize index) { return *static_cast<T*>(GetRaw(index)); }

    template <typename T> const T& Get(usize index) const { return *static_cast<const T*>(GetRaw(index)); }

    usize GetSize() const { return m_Size; }
    const ComponentTypeInfo& GetTypeInfo() const { return m_Info; }

    void EnsureCapacity(usize required)
    {
        const usize currentCapacity = (m_Info.size > 0) ? (m_Data.size() / m_Info.size) : 0;
        if (currentCapacity < required)
        {
            usize newCapacity = currentCapacity == 0 ? 16 : currentCapacity * 2;
            while (newCapacity < required)
            {
                newCapacity *= 2;
            }

            std::vector<uint8> newData(newCapacity * m_Info.size);
            for (usize i = 0; i < m_Size; ++i)
            {
                void* dst = newData.data() + (i * m_Info.size);
                void* src = GetRaw(i);
                m_Info.move(dst, src);
                m_Info.destruct(src);
            }
            m_Data.swap(newData);
        }
    }

private:
    const ComponentTypeInfo& m_Info;
    std::vector<uint8> m_Data;
    usize m_Size = 0;
};

/// Archetype - stores entities with matching component signature
class Archetype
{
public:
    explicit Archetype(const ArchetypeSignature& signature) : m_Signature(signature) {}

    usize AddEntity(Entity entity)
    {
        usize row = m_Entities.size();
        m_Entities.push_back(entity);
        m_EntityToRow[entity] = row;

        // Add empty components
        for (auto& [typeId, array] : m_Components)
        {
            array->Add();
        }

        return row;
    }

    void RemoveEntity(Entity entity)
    {
        auto it = m_EntityToRow.find(entity);
        if (it == m_EntityToRow.end())
            return;

        usize row = it->second;
        usize lastRow = m_Entities.size() - 1;

        // Swap-remove from components
        for (auto& [typeId, array] : m_Components)
        {
            array->Remove(row);
        }

        // Swap-remove from entity list
        if (row < lastRow)
        {
            Entity movedEntity = m_Entities[lastRow];
            m_Entities[row] = movedEntity;
            m_EntityToRow[movedEntity] = row;
        }
        m_Entities.pop_back();
        m_EntityToRow.erase(entity);
    }

    template <typename T> void AddComponentType()
    {
        ComponentTypeId id = GetComponentTypeId<T>();
        if (m_Components.find(id) == m_Components.end())
        {
            const auto& info = ComponentTypeRegistry::GetInfo<T>();
            m_Components[id] = std::make_unique<ComponentArray>(info);

            // Backfill existing entities
            for (usize i = 0; i < m_Entities.size(); ++i)
            {
                m_Components[id]->Add();
            }
        }
    }

    /// Copy component types from another archetype (used when migrating entities)
    /// Optionally exclude one component type (used by RemoveComponent migration).
    void CopyComponentTypesFrom(const Archetype& other, ComponentTypeId excludeTypeId = kInvalidComponentType)
    {
        for (const auto& [typeId, srcArray] : other.m_Components)
        {
            if (typeId == excludeTypeId)
                continue;

            if (m_Components.find(typeId) == m_Components.end())
            {
                // Create a new component array with the same type info
                m_Components[typeId] = std::make_unique<ComponentArray>(srcArray->GetTypeInfo());

                // Backfill for existing entities
                for (usize i = 0; i < m_Entities.size(); ++i)
                {
                    m_Components[typeId]->Add();
                }
            }
        }
    }

    /// Copy component data from another archetype for a specific entity
    void CopyComponentDataFrom(const Archetype& srcArch, Entity entity, usize dstRow)
    {
        // Find the entity's row in the source archetype
        auto srcRowIt = srcArch.m_EntityToRow.find(entity);
        if (srcRowIt == srcArch.m_EntityToRow.end())
            return;

        usize srcRow = srcRowIt->second;

        // Copy data for each component type that exists in both archetypes
        for (const auto& [typeId, srcArray] : srcArch.m_Components)
        {
            auto dstIt = m_Components.find(typeId);
            if (dstIt != m_Components.end() && m_Signature.Has(typeId))
            {
                // Copy raw bytes from source row to destination row
                const void* srcData = srcArray->GetRaw(srcRow);
                void* dstData = dstIt->second->GetRaw(dstRow);
                // Correctly copy component data (handle non-trivial types)
                const auto& info = srcArray->GetTypeInfo();

                // Destruct the default-constructed instance at destination
                info.destruct(dstData);

                // Use type-specific copy constructor (Deep Copy)
                info.copy(dstData, srcData);
            }
        }
    }

    template <typename T> T* GetComponent(Entity entity)
    {
        auto rowIt = m_EntityToRow.find(entity);
        if (rowIt == m_EntityToRow.end())
            return nullptr;

        ComponentTypeId id = GetComponentTypeId<T>();
        if (!m_Signature.Has(id))
            return nullptr;

        auto compIt = m_Components.find(id);
        if (compIt == m_Components.end())
            return nullptr;

        return &compIt->second->Get<T>(rowIt->second);
    }

    template <typename T> T* GetComponentByRow(usize row)
    {
        ComponentTypeId id = GetComponentTypeId<T>();
        if (!m_Signature.Has(id))
            return nullptr;

        auto it = m_Components.find(id);
        if (it == m_Components.end())
            return nullptr;
        return &it->second->Get<T>(row);
    }

    bool HasEntity(Entity entity) const { return m_EntityToRow.find(entity) != m_EntityToRow.end(); }
    usize GetEntityCount() const { return m_Entities.size(); }
    const ArchetypeSignature& GetSignature() const { return m_Signature; }
    const std::vector<Entity>& GetEntities() const { return m_Entities; }

    ComponentArray* GetComponentArray(ComponentTypeId id)
    {
        auto it = m_Components.find(id);
        return it != m_Components.end() ? it->second.get() : nullptr;
    }

private:
    ArchetypeSignature m_Signature;
    std::vector<Entity> m_Entities;
    std::unordered_map<Entity, usize, Entity::Hash> m_EntityToRow;
    std::unordered_map<ComponentTypeId, std::unique_ptr<ComponentArray>> m_Components;
};

} // namespace Dot
