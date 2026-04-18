// =============================================================================
// Dot Engine - Lightmap Baker
// =============================================================================

#pragma once

#include "Core/ECS/Entity.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Dot
{

class MapDocument;
class World;

struct LightmapBakeSummary
{
    bool success = false;
    bool staleDataDetected = false;
    int atlasCount = 0;
    int bakedEntityCount = 0;
    size_t estimatedBytes = 0;
    std::string outputFolder;
    std::string lastBakeTimestamp;
    std::string warning;
    std::string error;
};

class LightmapBaker
{
public:
    bool BakeAll(World& world, const std::string& scenePath, MapDocument* mapDocument = nullptr);
    bool BakeSelected(World& world, const std::vector<Entity>& selectedEntities, const std::string& scenePath,
                      MapDocument* mapDocument = nullptr);
    bool ClearBakeData(World& world, const std::string& scenePath, const std::vector<Entity>* entities = nullptr,
                       MapDocument* mapDocument = nullptr);
    void RefreshBakeStates(World& world, const std::string& scenePath, MapDocument* mapDocument = nullptr);

    const LightmapBakeSummary& GetLastSummary() const { return m_LastSummary; }
    std::filesystem::path GetOutputDirectory(const std::string& scenePath) const;
    void OpenOutputFolder(const std::string& scenePath) const;

private:
    bool Bake(World& world, const std::vector<Entity>* selectedEntities, const std::string& scenePath,
              MapDocument* mapDocument);
    std::string BuildSceneBakeSignature(World& world, const std::string& scenePath, const MapDocument* mapDocument) const;

    LightmapBakeSummary m_LastSummary;
};

} // namespace Dot
