// =============================================================================
// Dot Engine - Static Lighting Component
// =============================================================================
// Per-entity baked lighting participation and runtime lightmap bindings.
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

struct StaticLightingComponent
{
    bool participateInBake = true;
    bool receiveBakedLighting = true;
    bool castBakedShadows = true;
    float resolutionScale = 1.0f;

    bool bakeValid = false;
    bool bakeStale = true;
    bool useBakedLighting = true;
    float lightmapIntensity = 1.0f;

    std::string lightmapTexturePath;
    std::string lightmapSidecarPath;
    std::string bakeSignature;

    float lightmapScaleU = 1.0f;
    float lightmapScaleV = 1.0f;
    float lightmapOffsetU = 0.0f;
    float lightmapOffsetV = 0.0f;
};

} // namespace Dot
