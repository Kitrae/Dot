// =============================================================================
// Dot Engine - View Settings
// =============================================================================
// Shared runtime/editor rendering settings.
// =============================================================================

#pragma once

#include <cstdint>

namespace Dot
{

/// Legacy render mode alias (kept for serialization backward compat)
enum class RenderMode
{
    Normal,
    Wireframe,
    Depth
};

/// Comprehensive debug visualization modes (Unreal-style)
enum class DebugVisMode : uint8_t
{
    // === Standard ===
    Lit = 0,          // Normal lit rendering
    Unlit,            // Flat albedo, no lighting
    Wireframe,        // Wireframe only
    WireframeOverlay, // Lit + wireframe on top

    // === Lighting ===
    LightingOnly,         // White albedo, full lighting (isolates light contribution)
    AmbientOcclusionOnly, // Ambient occlusion debug view

    // === Geometry ===
    Normals, // World-space normals as RGB
    Depth,   // Depth buffer visualization

    // === Materials ===
    BaseColor, // Albedo only (no lighting, no AO)
    Metallic,  // Metallic channel grayscale
    Roughness, // Roughness channel grayscale

    // === Performance ===
    LODVisualization, // LOD debug tint
    LightComplexity,  // Number of lights affecting each pixel
    Overdraw,         // Additive per-draw overdraw accumulation

    Count
};

/// Human-readable names for each mode (indexed by enum value)
inline const char* GetDebugVisModeName(DebugVisMode mode)
{
    switch (mode)
    {
    case DebugVisMode::Lit:
        return "Lit";
    case DebugVisMode::Unlit:
        return "Unlit";
    case DebugVisMode::Wireframe:
        return "Wireframe";
    case DebugVisMode::WireframeOverlay:
        return "Wireframe Overlay";
    case DebugVisMode::LightingOnly:
        return "Lighting Only";
    case DebugVisMode::AmbientOcclusionOnly:
        return "Ambient Occlusion";
    case DebugVisMode::Normals:
        return "Normals";
    case DebugVisMode::Depth:
        return "Depth";
    case DebugVisMode::BaseColor:
        return "Base Color";
    case DebugVisMode::Metallic:
        return "Metallic";
    case DebugVisMode::Roughness:
        return "Roughness";
    case DebugVisMode::LODVisualization:
        return "LOD Visualization";
    case DebugVisMode::LightComplexity:
        return "Light Complexity";
    case DebugVisMode::Overdraw:
        return "Overdraw";
    default:
        return "Unknown";
    }
}

/// Category names for the dropdown
enum class DebugVisCategory : uint8_t
{
    Standard = 0,
    Lighting,
    Geometry,
    Materials,
    Performance,
    Count
};

inline const char* GetDebugVisCategoryName(DebugVisCategory category)
{
    switch (category)
    {
    case DebugVisCategory::Standard:
        return "Standard";
    case DebugVisCategory::Lighting:
        return "Lighting";
    case DebugVisCategory::Geometry:
        return "Geometry";
    case DebugVisCategory::Materials:
        return "Materials";
    case DebugVisCategory::Performance:
        return "Performance";
    default:
        return "Unknown";
    }
}

inline DebugVisCategory GetDebugVisModeCategory(DebugVisMode mode)
{
    switch (mode)
    {
    case DebugVisMode::Lit:
    case DebugVisMode::Unlit:
    case DebugVisMode::Wireframe:
    case DebugVisMode::WireframeOverlay:
        return DebugVisCategory::Standard;
    case DebugVisMode::LightingOnly:
    case DebugVisMode::AmbientOcclusionOnly:
    case DebugVisMode::LightComplexity:
        return DebugVisCategory::Lighting;
    case DebugVisMode::Normals:
    case DebugVisMode::Depth:
        return DebugVisCategory::Geometry;
    case DebugVisMode::BaseColor:
    case DebugVisMode::Metallic:
    case DebugVisMode::Roughness:
        return DebugVisCategory::Materials;
    case DebugVisMode::LODVisualization:
    case DebugVisMode::Overdraw:
        return DebugVisCategory::Performance;
    default:
        return DebugVisCategory::Standard;
    }
}

inline DebugVisMode SanitizeDebugVisMode(DebugVisMode mode)
{
    return mode;
}

inline bool IsDebugVisModeAvailable(DebugVisMode /*mode*/)
{
    return true;
}

class ViewSettings
{
public:
    static ViewSettings& Get()
    {
        static ViewSettings instance;
        return instance;
    }

    // Primary debug visualization mode (replaces old renderMode + scattered bools)
    DebugVisMode debugVisMode = DebugVisMode::Lit;

    // Legacy accessor for backward compat (maps to debugVisMode)
    RenderMode renderMode = RenderMode::Normal;
    bool wireframeOverlay = false;

    bool shadowsEnabled = true;
    bool ssaoEnabled = true;
    bool antiAliasingEnabled = true;
    bool frustumCullingEnabled = true;
    bool hzbEnabled = true;
    bool forwardPlusEnabled = true;
    bool lodDebugTint = false;
    float lodAggressiveness = 1.0f;
    bool ssaoDebugFullscreen = false;
    float ssaoRadius = 0.75f;
    float ssaoBias = 0.5f;
    float ssaoIntensity = 1.0f;
    float ssaoPower = 1.0f;
    float ssaoThickness = 2.0f;
    float ssaoMaxScreenRadius = 48.0f;
    float ssaoBlurDepthThreshold = 4.0f;
    int ssaoSampleCount = 8;
    bool ssaoHalfResolution = true;
    bool ssaoPreferExternalShaders = true;

    /// Sync legacy fields FROM the new debugVisMode
    void SyncLegacyFromDebugVis()
    {
        debugVisMode = SanitizeDebugVisMode(debugVisMode);

        // Reset legacy flags
        wireframeOverlay = false;
        lodDebugTint = false;
        ssaoDebugFullscreen = false;

        switch (debugVisMode)
        {
        case DebugVisMode::Wireframe:
            renderMode = RenderMode::Wireframe;
            break;
        case DebugVisMode::Depth:
            renderMode = RenderMode::Depth;
            break;
        case DebugVisMode::WireframeOverlay:
            renderMode = RenderMode::Normal;
            wireframeOverlay = true;
            break;
        case DebugVisMode::LODVisualization:
            renderMode = RenderMode::Normal;
            lodDebugTint = true;
            break;
        default:
            renderMode = RenderMode::Normal;
            break;
        }
    }

    /// Sync the primary debugVisMode FROM legacy flags/UI toggles, then normalize them.
    void SyncDebugVisFromLegacy()
    {
        ssaoDebugFullscreen = false;

        if (lodDebugTint)
            debugVisMode = DebugVisMode::LODVisualization;
        else if (wireframeOverlay)
            debugVisMode = DebugVisMode::WireframeOverlay;
        else
        {
            switch (renderMode)
            {
            case RenderMode::Wireframe:
                debugVisMode = DebugVisMode::Wireframe;
                break;
            case RenderMode::Depth:
                debugVisMode = DebugVisMode::Depth;
                break;
            default:
                debugVisMode = DebugVisMode::Lit;
                break;
            }
        }

        SyncLegacyFromDebugVis();
    }

private:
    ViewSettings() = default;
};

} // namespace Dot
