// =============================================================================
// Dot Engine - Light Components
// =============================================================================
// Entity-based lighting components for the rendering system.
// =============================================================================

#pragma once

#include "Core/Math/Vec3.h"

#include <cstdint>
#include <string>

namespace Dot
{

// =============================================================================
// Light Type Enumeration
// =============================================================================

enum class LightType : uint8
{
    Directional = 0,
    Point,
    Spot,
    Count
};

enum class LightingMode : uint8
{
    Realtime = 0,
    Baked
};

inline const char* GetLightTypeName(LightType type)
{
    switch (type)
    {
        case LightType::Directional:
            return "Directional";
        case LightType::Point:
            return "Point";
        case LightType::Spot:
            return "Spot";
        default:
            return "Unknown";
    }
}

inline const char* GetLightingModeName(LightingMode mode)
{
    switch (mode)
    {
        case LightingMode::Realtime:
            return "Realtime";
        case LightingMode::Baked:
            return "Baked";
        default:
            return "Unknown";
    }
}

// =============================================================================
// Directional Light Component
// =============================================================================
// Simulates distant light sources like the sun. All rays are parallel.
// Direction is typically set via the entity's rotation (forward vector).

struct DirectionalLightComponent
{
    Vec3 color = {1.0f, 1.0f, 1.0f}; // Light color (RGB, 0-1)
    float intensity = 1.0f;          // Light brightness multiplier
    LightingMode lightingMode = LightingMode::Realtime;

    // Shadow settings
    bool castShadows = true;
    float shadowBias = 0.0005f;
    float shadowDistance = 100.0f;

    DirectionalLightComponent() = default;
    DirectionalLightComponent(Vec3 col, float intens) : color(col), intensity(intens) {}
};

// =============================================================================
// Point Light Component
// =============================================================================
// Emits light in all directions from the entity's position.

struct PointLightComponent
{
    Vec3 color = {1.0f, 1.0f, 1.0f};  // Light color (RGB, 0-1)
    float intensity = 1.0f;           // Light brightness multiplier
    float range = 10.0f;              // Maximum light reach distance
    LightingMode lightingMode = LightingMode::Realtime;
    bool castShadows = false;         // Enable point-light shadow map rendering
    float shadowBias = 0.0025f;       // Depth bias used when sampling point-light shadows
    float constantAttenuation = 1.0f; // Attenuation: 1 / (c + l*d + q*d^2)
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;

    PointLightComponent() = default;
    PointLightComponent(Vec3 col, float intens, float rng) : color(col), intensity(intens), range(rng) {}
};

// =============================================================================
// Spot Light Component
// =============================================================================
// Emits light in a cone from the entity's position in its forward direction.

struct SpotLightComponent
{
    Vec3 color = {1.0f, 1.0f, 1.0f}; // Light color (RGB, 0-1)
    float intensity = 1.0f;          // Light brightness multiplier
    float range = 10.0f;             // Maximum light reach distance
    float innerConeAngle = 25.0f;    // Inner cone angle (degrees) - full intensity
    float outerConeAngle = 35.0f;    // Outer cone angle (degrees) - falloff edge
    LightingMode lightingMode = LightingMode::Realtime;
    bool castShadows = false;        // Enable projected spotlight shadow map rendering
    float shadowBias = 0.0025f;      // Depth bias used when sampling spotlight shadows

    // Attenuation
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;

    SpotLightComponent() = default;
    SpotLightComponent(Vec3 col, float intens, float rng, float inner, float outer)
        : color(col), intensity(intens), range(rng), innerConeAngle(inner), outerConeAngle(outer)
    {
    }
};

// =============================================================================
// Ambient Light (Global Scene Setting)
// =============================================================================
// Could be attached to a single "environment" entity or managed globally.

struct AmbientLightComponent
{
    Vec3 color = {0.1f, 0.1f, 0.1f}; // Ambient light color
    float intensity = 1.0f;          // Ambient intensity

    AmbientLightComponent() = default;
    AmbientLightComponent(Vec3 col, float intens) : color(col), intensity(intens) {}
};

enum class ReflectionProbeSourceMode : uint8
{
    ManualCubemap = 0,
    AutoSceneSkybox
};

inline const char* GetReflectionProbeSourceModeName(ReflectionProbeSourceMode mode)
{
    switch (mode)
    {
        case ReflectionProbeSourceMode::ManualCubemap:
            return "Manual Cubemap";
        case ReflectionProbeSourceMode::AutoSceneSkybox:
            return "Auto Scene Sky";
        default:
            return "Unknown";
    }
}

// =============================================================================
// Reflection Probe Component
// =============================================================================
// Manual cubemap-based local environment probe for specular reflections.

struct ReflectionProbeComponent
{
    ReflectionProbeSourceMode sourceMode = ReflectionProbeSourceMode::ManualCubemap;
    std::string cubemapPath;            // Relative or absolute cubemap path for manual mode
    Vec3 tint = {1.0f, 1.0f, 1.0f};     // Probe tint multiplier
    float intensity = 1.0f;             // Probe brightness
    float radius = 12.0f;               // World-space influence radius
    Vec3 boxExtents = {0.0f, 0.0f, 0.0f}; // Optional half-extents for box projection. Zero = use radius on all axes.
    float falloff = 0.25f;              // Fraction of the radius used to fade at the edge
    bool enabled = true;                // Toggle probe contribution

    ReflectionProbeComponent() = default;
};

} // namespace Dot
