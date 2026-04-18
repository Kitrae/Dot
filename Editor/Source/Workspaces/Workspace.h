// =============================================================================
// Dot Engine - Workspace Base Class
// =============================================================================
// Base class for editor workspaces (Blender-style full-screen layouts).
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

/// Workspace type enumeration
enum class WorkspaceType
{
    Layout,    // Default editor layout (Hierarchy, Viewport, Inspector, etc.)
    Map,       // Authored map/brush editing workspace
    UI,        // UI authoring workspace
    Scripting, // Script editor workspace
    Material   // Material editor workspace
};

/// Base class for all workspaces
class Workspace
{
public:
    Workspace(const std::string& name, WorkspaceType type) : m_Name(name), m_Type(type) {}
    virtual ~Workspace() = default;

    /// Draw the workspace UI (called each frame when active)
    virtual void OnImGui() = 0;

    /// Called when workspace becomes active
    virtual void OnActivate() {}

    /// Called when workspace is deactivated
    virtual void OnDeactivate() {}

    /// Get workspace name for tab display
    const std::string& GetName() const { return m_Name; }

    /// Get workspace type
    WorkspaceType GetType() const { return m_Type; }

protected:
    std::string m_Name;
    WorkspaceType m_Type;
};

} // namespace Dot
