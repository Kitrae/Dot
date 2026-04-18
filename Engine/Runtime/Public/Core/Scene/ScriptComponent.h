// =============================================================================
// Dot Engine - Script Component
// =============================================================================
// Component that attaches a Lua script to an entity.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <string>

namespace Dot
{

/// Component for attaching Lua scripts to entities
struct DOT_CORE_API ScriptComponent
{
    /// Path to the Lua script file (relative to Assets/Scripts/)
    std::string scriptPath;

    /// Internal: Loaded script reference ID (-1 = not loaded)
    int scriptRef = -1;

    /// Internal: Whether OnStart has been called
    bool hasStarted = false;

    /// Whether the script is enabled
    bool enabled = true;

    ScriptComponent() = default;
    explicit ScriptComponent(const std::string& path) : scriptPath(path) {}
};

} // namespace Dot
