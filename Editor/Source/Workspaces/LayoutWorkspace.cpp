// =============================================================================
// Dot Engine - Layout Workspace Implementation
// =============================================================================

#include "LayoutWorkspace.h"

#include "Panels/AssetManagerPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/DebugPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/MaterialGraphPanel.h"
#include "Panels/PrefabViewerPanel.h"
#include "Panels/SettingsPanel.h"
#include "Panels/SceneSettingsPanel.h"
#include "Panels/TextEditorPanel.h"
#include "Panels/TextureViewerPanel.h"
#include "Panels/ViewportPanel.h"
#include "../Scene/EditorSceneContext.h"

#include <imgui.h>

namespace Dot
{

LayoutWorkspace::LayoutWorkspace() : Workspace("Layout", WorkspaceType::Layout) {}

void LayoutWorkspace::SetPanels(HierarchyPanel* hierarchy, InspectorPanel* inspector, ViewportPanel* viewport,
                                ConsolePanel* console, DebugPanel* debug, AssetManagerPanel* assets,
                                TextEditorPanel* textEditor, PrefabViewerPanel* prefabViewer,
                                TextureViewerPanel* textureViewer, SceneSettingsPanel* sceneSettings)
{
    m_HierarchyPanel = hierarchy;
    m_InspectorPanel = inspector;
    m_ViewportPanel = viewport;
    m_ConsolePanel = console;
    m_DebugPanel = debug;
    m_AssetManagerPanel = assets;
    m_TextEditorPanel = textEditor;
    m_PrefabViewerPanel = prefabViewer;
    m_TextureViewerPanel = textureViewer;
    m_SceneSettingsPanel = sceneSettings;

    if (m_InspectorPanel)
    {
        m_InspectorPanel->SetSelectionCallback(
            [this](Entity entity)
            {
                if (m_SceneContext)
                    m_SceneContext->GetEntitySelection().SetPrimaryEntity(m_SceneContext->GetWorld(), entity);
                if (m_HierarchyPanel)
                    m_HierarchyPanel->SetSelectedEntity(entity);
                if (m_ViewportPanel)
                    m_ViewportPanel->SetSelectedEntity(entity);
            });
    }
}

void LayoutWorkspace::OnActivate()
{
    // Nothing special needed when switching to layout workspace
}

void LayoutWorkspace::OnDeactivate()
{
    // Nothing special needed when leaving layout workspace
}

void LayoutWorkspace::OnImGui()
{
    // Draw all panels
    if (m_HierarchyPanel)
        m_HierarchyPanel->OnImGui();

    if (m_InspectorPanel && m_ViewportPanel)
    {
        World* inspectorWorld = m_SceneContext ? m_SceneContext->GetWorld() : &m_ViewportPanel->GetWorld();
        Entity selectedEntity = m_SceneContext ? m_SceneContext->GetEntitySelection().GetPrimaryEntity()
                                               : m_ViewportPanel->GetSelectedEntity();
        m_InspectorPanel->SetContext(inspectorWorld, selectedEntity);
        m_InspectorPanel->OnImGui();
    }

    if (m_ConsolePanel)
        m_ConsolePanel->OnImGui();

    if (m_AssetManagerPanel)
        m_AssetManagerPanel->OnImGui();

    if (m_TextEditorPanel)
        m_TextEditorPanel->OnImGui();

    if (m_PrefabViewerPanel)
        m_PrefabViewerPanel->OnImGui();

    if (m_TextureViewerPanel)
        m_TextureViewerPanel->OnImGui();

    if (m_SceneSettingsPanel)
        m_SceneSettingsPanel->OnImGui();

    // Settings windows
    SettingsPanel::OnImGui();

    if (m_DebugPanel)
        m_DebugPanel->Draw();

    // Viewport always renders
    if (m_ViewportPanel)
        m_ViewportPanel->OnImGui();
}

} // namespace Dot
