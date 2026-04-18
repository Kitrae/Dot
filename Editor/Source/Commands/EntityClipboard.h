// =============================================================================
// Dot Engine - Entity Clipboard Helpers
// =============================================================================

#pragma once

#include "CreateEntityCommands.h"

namespace Dot
{

struct EntityClipboard
{
    bool hasData = false;
    bool wasCut = false;
    std::vector<EntityClipboardEntry> entries;
};

EntityClipboard& GetEntityClipboard();

std::vector<EntityClipboardEntry> CreateClipboardEntries(World& world, const std::vector<Entity>& entities);

EntityComponentSnapshot CreateClipboardSnapshot(World& world, Entity entity);

} // namespace Dot
