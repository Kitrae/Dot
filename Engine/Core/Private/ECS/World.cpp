// =============================================================================
// Dot Engine - World Implementation
// =============================================================================

#include "Core/ECS/World.h"

namespace Dot
{

World::World()
{
    m_Generations.reserve(1024);
    m_EntityArchetype.reserve(1024);
}

World::~World()
{
    Clear();
}

Entity World::CreateEntity()
{
    uint32 index;
    uint8 generation;

    if (!m_FreeIndices.empty())
    {
        // Reuse recycled index
        index = m_FreeIndices.back();
        m_FreeIndices.pop_back();
        generation = m_Generations[index];
        m_Alive[index] = true;
    }
    else
    {
        // Allocate new index
        index = static_cast<uint32>(m_Generations.size());
        m_Generations.push_back(1); // Start at generation 1 (0 is invalid)
        m_Alive.push_back(true);
        m_EntityArchetype.push_back(nullptr);
        generation = 1;
    }

    m_AliveCount++;
    return Entity(index, generation);
}

void World::DestroyEntity(Entity entity)
{
    if (!IsAlive(entity))
        return;

    uint32 index = entity.GetIndex();

    // Remove from archetype
    Archetype* arch = m_EntityArchetype[index];
    if (arch)
    {
        arch->RemoveEntity(entity);
        m_EntityArchetype[index] = nullptr;
    }

    // Increment generation to invalidate existing handles
    m_Generations[index]++;
    m_Alive[index] = false;
    m_FreeIndices.push_back(index);
    m_AliveCount--;
}

bool World::IsAlive(Entity entity) const
{
    uint32 index = entity.GetIndex();
    if (index >= m_Generations.size())
        return false;
    if (index >= m_Alive.size())
        return false;
    if (!m_Alive[index])
        return false;
    return m_Generations[index] == entity.GetGeneration();
}

Archetype* World::GetOrCreateArchetype(Entity entity, ComponentTypeId newType)
{
    uint32 index = entity.GetIndex();
    if (index >= m_EntityArchetype.size())
        return nullptr;
    Archetype* currentArch = m_EntityArchetype[index];

    // Build target signature
    ArchetypeSignature targetSig;
    if (currentArch)
    {
        targetSig = currentArch->GetSignature();
    }
    targetSig.Add(newType);

    // Find or create archetype
    auto it = m_Archetypes.find(targetSig);
    if (it != m_Archetypes.end())
    {
        return it->second.get();
    }

    // Create new archetype
    auto arch = std::make_unique<Archetype>(targetSig);
    Archetype* ptr = arch.get();
    m_Archetypes[targetSig] = std::move(arch);

    return ptr;
}

void World::Clear()
{
    // Clear entity tracking first to prevent any access during archetype cleanup
    m_EntityArchetype.clear();
    m_Alive.clear();
    m_Generations.clear();
    m_FreeIndices.clear();
    m_AliveCount = 0;

    // Clear archetypes LAST (after entity tracking is gone)
    // This destroys all components via ComponentArray destructors
    m_Archetypes.clear();
}

} // namespace Dot
