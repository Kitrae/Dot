// =============================================================================
// Dot Engine - Prefab Component
// =============================================================================
// Component that marks an entity as a prefab instance.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <string>

namespace Dot
{

/// Component attached to entities that were instantiated from a prefab.
/// The root entity of a prefab instance has isRootInstance = true.
/// Child entities have isRootInstance = false but still track the source prefab.
struct DOT_CORE_API PrefabComponent
{
    /// Path to the source .prefab file (relative to Assets folder)
    std::string prefabPath;

    /// True if this is the root entity of the prefab instance
    bool isRootInstance = false;

    /// Index within the prefab's entity list (for override tracking)
    int entityIndex = 0;

    /// Instance ID - all entities from same instantiation share this
    /// Used to identify which entities belong together
    uint32 instanceId = 0;

    PrefabComponent() = default;
    PrefabComponent(const std::string& path, bool isRoot, int index, uint32 instId)
        : prefabPath(path), isRootInstance(isRoot), entityIndex(index), instanceId(instId)
    {
    }
};

} // namespace Dot
