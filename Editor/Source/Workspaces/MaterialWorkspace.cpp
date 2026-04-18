// =============================================================================
// Dot Engine - Material Workspace Implementation
// =============================================================================

#include "MaterialWorkspace.h"

#include "Panels/AssetManagerPanel.h"
#include "Panels/MaterialGraphPanel.h"

#include <imgui.h>

namespace Dot
{

MaterialWorkspace::MaterialWorkspace() : Workspace("Material", WorkspaceType::Material) {}

void MaterialWorkspace::SetPanels(MaterialGraphPanel* materialGraph, AssetManagerPanel* assetManager)
{
    m_MaterialGraphPanel = materialGraph;
    m_AssetManagerPanel = assetManager;
}

void MaterialWorkspace::OnImGui()
{

    // We can use a simpler layout or just draw the panels
    // For now, let's keep it simple and just draw the panels which use their own windows or dock nodes

    if (m_MaterialGraphPanel)
    {
        // Material Graph usually takes up most of the screen
        // It manages its own windows (Graph, Nodes, Properties, Preview)

        // We ensure it's open
        bool open = true;
        if (ImGui::Begin("Material Graph", &open))
        {
            m_MaterialGraphPanel->OnImGui();
        }
        ImGui::End();
    }

    if (m_AssetManagerPanel)
        m_AssetManagerPanel->OnImGui();
}

} // namespace Dot
