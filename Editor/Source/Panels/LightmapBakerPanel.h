// =============================================================================
// Dot Engine - Lightmap Baker Panel
// =============================================================================

#pragma once

#include "EditorPanel.h"

#include "../Lightmapping/LightmapBaker.h"

#include <string>
#include <vector>

namespace Dot
{

class MapDocument;
class World;

class LightmapBakerPanel : public EditorPanel
{
public:
    LightmapBakerPanel() : EditorPanel("Lightmap Baker") { m_Open = false; }

    void SetContext(World* world, const std::vector<Entity>& selectedEntities, const std::string& scenePath,
                    MapDocument* mapDocument);
    void OnImGui() override;

private:
    World* m_World = nullptr;
    MapDocument* m_MapDocument = nullptr;
    std::vector<Entity> m_SelectedEntities;
    std::string m_ScenePath;
    World* m_LastRefreshedWorld = nullptr;
    std::string m_LastRefreshedScenePath;
    uint64 m_LastRefreshedMapRevision = 0;
    LightmapBaker m_Baker;
};

} // namespace Dot
