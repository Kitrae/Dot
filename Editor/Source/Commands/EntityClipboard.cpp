// =============================================================================
// Dot Engine - Entity Clipboard Helpers
// =============================================================================

#include "EntityClipboard.h"

#include <unordered_set>

namespace Dot
{

EntityClipboard& GetEntityClipboard()
{
    static EntityClipboard clipboard;
    return clipboard;
}

std::vector<EntityClipboardEntry> CreateClipboardEntries(World& world, const std::vector<Entity>& entities)
{
    std::vector<EntityClipboardEntry> entries;
    std::vector<Entity> validEntities;
    validEntities.reserve(entities.size());
    std::unordered_set<uint32> selectedIds;
    selectedIds.reserve(entities.size());

    for (Entity entity : entities)
    {
        if (!entity.IsValid() || !world.IsAlive(entity))
            continue;
        if (!selectedIds.insert(entity.id).second)
            continue;
        validEntities.push_back(entity);
    }

    for (Entity entity : validEntities)
    {
        EntityClipboardEntry entry;
        entry.sourceEntity = entity;
        entry.snapshot = CaptureEntitySnapshot(world, entity);

        if (entry.snapshot.hierarchy)
        {
            if (selectedIds.find(entry.snapshot.hierarchy->parent.id) == selectedIds.end())
                entry.snapshot.hierarchy->parent = kNullEntity;

            std::vector<Entity> filteredChildren;
            filteredChildren.reserve(entry.snapshot.hierarchy->children.size());
            for (Entity child : entry.snapshot.hierarchy->children)
            {
                if (selectedIds.find(child.id) != selectedIds.end())
                    filteredChildren.push_back(child);
            }
            entry.snapshot.hierarchy->children = std::move(filteredChildren);
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

EntityComponentSnapshot CreateClipboardSnapshot(World& world, Entity entity)
{
    const std::vector<EntityClipboardEntry> entries = CreateClipboardEntries(world, {entity});
    return entries.empty() ? EntityComponentSnapshot{} : entries.front().snapshot;
}

} // namespace Dot
