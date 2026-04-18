// =============================================================================
// Dot Engine - Material Component
// =============================================================================
// Defines material properties for rendering (color, metallic, roughness, emissive).
// =============================================================================

#pragma once

#include "Core/Math/Vec3.h"

#include <cstdint>
#include <string>

namespace Dot
{

/// Material properties for PBR-like rendering
struct MaterialComponent
{
    /// Path to .dotmat material file (if empty, use inline values below)
    std::string materialPath;

    /// Whether to use the material file (true) or inline values (false)
    bool useMaterialFile = false;

    /// Base color / albedo (RGB, 0-1)
    Vec3 baseColor{0.7f, 0.7f, 0.7f};

    /// Metallic factor (0 = dielectric, 1 = metal)
    float metallic = 0.0f;

    /// Roughness factor (0 = smooth/shiny, 1 = rough/matte)
    float roughness = 0.5f;

    /// Emissive color (RGB, 0-1)
    Vec3 emissiveColor{0.0f, 0.0f, 0.0f};

    /// Emissive multiplier (0 = off)
    float emissiveStrength = 0.0f;
};

} // namespace Dot
