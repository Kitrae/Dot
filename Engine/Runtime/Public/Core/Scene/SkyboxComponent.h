// =============================================================================
// Dot Engine - Skybox Component
// =============================================================================
// Defines a cubemap skybox for environment background rendering.
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

/// Texture wrap mode for skybox cubemap
enum class SkyboxWrapMode
{
    Clamp = 0, // Clamp to edge (no wrapping)
    Repeat,    // Repeat/wrap texture
    Mirror     // Mirror at boundaries
};

/// Skybox component for cubemap-based environment backgrounds
struct SkyboxComponent
{
    /// Path to cubemap image (single cross-layout image)
    std::string cubemapPath;

    /// Tint color multiplier (RGB, 0-1)
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;

    /// Texture wrap mode
    SkyboxWrapMode wrapMode = SkyboxWrapMode::Clamp;

    /// Horizontal rotation in degrees (0-360)
    float rotation = 0.0f;

    /// Debug: Show face markers (F/B/L/R/U/D) on each face
    bool showMarkers = false;

    // ---- Ambient Light Settings ----
    /// Enable ambient fill lighting from this skybox
    bool ambientEnabled = false;

    /// Ambient color (RGB, 0-1)
    float ambientColorR = 0.3f;
    float ambientColorG = 0.35f;
    float ambientColorB = 0.4f;

    /// Ambient intensity (0-10)
    float ambientIntensity = 0.3f;

    // ---- Sun/Directional Light Settings ----
    /// Enable sun light from skybox
    bool sunEnabled = true;

    /// Sun rotation (Euler angles in degrees - pitch/yaw)
    float sunRotationX = 45.0f; // Pitch (up/down)
    float sunRotationY = 30.0f; // Yaw (left/right)

    /// Sun color (RGB, 0-1)
    float sunColorR = 1.0f;
    float sunColorG = 0.95f;
    float sunColorB = 0.9f;

    /// Sun intensity (0-10)
    float sunIntensity = 1.0f;

    // ---- Sun Shadow Settings ----
    /// Enable shadows for the sun
    bool sunCastShadows = true;

    /// Shadow bias for the sun
    float sunShadowBias = 0.0005f;

    /// Shadow distance for the sun
    float sunShadowDistance = 100.0f;

    /// Runtime flag - set when texture is loaded
    bool isLoaded = false;
};

} // namespace Dot
