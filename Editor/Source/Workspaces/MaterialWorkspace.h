// =============================================================================
// Dot Engine - Material Workspace
// =============================================================================

#pragma once

#include "Workspace.h"

namespace Dot
{

class MaterialGraphPanel;
class AssetManagerPanel;

class MaterialWorkspace : public Workspace
{
public:
    MaterialWorkspace();

    void SetPanels(MaterialGraphPanel* materialGraph, AssetManagerPanel* assetManager);

    void OnImGui() override;

private:
    MaterialGraphPanel* m_MaterialGraphPanel = nullptr;
    AssetManagerPanel* m_AssetManagerPanel = nullptr;
};

} // namespace Dot
