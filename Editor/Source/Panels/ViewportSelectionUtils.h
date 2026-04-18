// =============================================================================
// Dot Engine - Viewport Selection Utilities
// =============================================================================

#pragma once

#include "Core/ECS/World.h"
#include "Core/Scene/Components.h"

#include "../Map/MapDocument.h"
#include "../Rendering/Camera.h"

#include <imgui.h>
#include <vector>

namespace Dot
{

bool ScreenPointToCameraPlaneWorld(const Camera& camera, float screenX, float screenY, float viewportX, float viewportY,
                                   float viewportWidth, float viewportHeight, float planeDepth, Vec3& outPoint);

std::vector<MapSelection> CollectMapSelectionsInScreenRect(const Camera& camera, const MapDocument& document,
                                                           const ImVec2& start, const ImVec2& end, float viewportX,
                                                           float viewportY, float screenWidth, float screenHeight);

std::vector<Entity> CollectEntitiesInScreenRect(const Camera& camera, World& world, const ImVec2& start,
                                                const ImVec2& end, float viewportX, float viewportY, float screenWidth,
                                                float screenHeight);

} // namespace Dot
