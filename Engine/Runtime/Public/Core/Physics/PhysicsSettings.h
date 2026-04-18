#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec3.h"

namespace Dot
{

/// Physics system runtime settings (configurable via Editor)
struct DOT_CORE_API PhysicsSettings
{
    // Timestep settings
    float fixedTimestep = 1.0f / 120.0f; // Default 120Hz (was 240Hz - too fast)
    int maxSubSteps = 8;                 // Maximum physics substeps per frame

    // World settings
    Vec3 gravity = Vec3(0.0f, -9.81f, 0.0f); // World gravity

    // Solver settings
    float positionCorrectionPercent = 0.8f; // Baumgarte stabilization (0.8 = stronger anti-clipping)
    float positionCorrectionSlop = 0.005f;  // Smaller slop = less allowed overlap

    // Collision settings
    bool enableContinuousCollision = false; // CCD (not yet implemented)

    // Debug settings
    bool showColliders = false;     // Draw collider outlines
    bool showContactPoints = false; // Draw contact points

    // Singleton access
    static PhysicsSettings& Get()
    {
        static PhysicsSettings instance;
        return instance;
    }
};

} // namespace Dot
