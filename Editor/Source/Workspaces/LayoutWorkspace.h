// =============================================================================
// Dot Engine - Layout Workspace
// =============================================================================
// The default editor workspace with Hierarchy, Viewport, Inspector, etc.
// This wraps the original editor layout into a workspace.
// =============================================================================

#pragma once

#include "Workspace.h"

namespace Dot
{

// Forward declarations
class HierarchyPanel;
class InspectorPanel;
class ViewportPanel;
class ConsolePanel;
class DebugPanel;
class AssetManagerPanel;
class TextEditorPanel;
class MaterialGraphPanel;
class PrefabViewerPanel;
class TextureViewerPanel;
class SceneSettingsPanel;
class EditorSceneContext;

/// Layout Workspace - the default editor layout
class LayoutWorkspace : public Workspace
{
public:
    LayoutWorkspace();
    ~LayoutWorkspace() override = default;

    void OnImGui() override;
    void OnActivate() override;
    void OnDeactivate() override;

    /// Set references to the panels (owned by Application)
    void SetPanels(HierarchyPanel* hierarchy, InspectorPanel* inspector, ViewportPanel* viewport, ConsolePanel* console,
                   DebugPanel* debug, AssetManagerPanel* assets, TextEditorPanel* textEditor,
                   PrefabViewerPanel* prefabViewer, TextureViewerPanel* textureViewer,
                   SceneSettingsPanel* sceneSettings);
    void SetSceneContext(EditorSceneContext* sceneContext) { m_SceneContext = sceneContext; }

    /// Access to viewport for selection sync
    ViewportPanel* GetViewportPanel() { return m_ViewportPanel; }
    HierarchyPanel* GetHierarchyPanel() { return m_HierarchyPanel; }

private:
    // Panel references (not owned)
    HierarchyPanel* m_HierarchyPanel = nullptr;
    InspectorPanel* m_InspectorPanel = nullptr;
    ViewportPanel* m_ViewportPanel = nullptr;
    ConsolePanel* m_ConsolePanel = nullptr;
    DebugPanel* m_DebugPanel = nullptr;
    AssetManagerPanel* m_AssetManagerPanel = nullptr;
    TextEditorPanel* m_TextEditorPanel = nullptr;
    PrefabViewerPanel* m_PrefabViewerPanel = nullptr;
    TextureViewerPanel* m_TextureViewerPanel = nullptr;
    SceneSettingsPanel* m_SceneSettingsPanel = nullptr;
    EditorSceneContext* m_SceneContext = nullptr;
};

} // namespace Dot
