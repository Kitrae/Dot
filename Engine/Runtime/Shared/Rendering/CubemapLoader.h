// =============================================================================
// Dot Engine - Cubemap Loader
// =============================================================================
// Loads cubemap textures from:
//   - Single image file (cross layout - horizontal or vertical)
//   - Folder with 6 PNG files (px.png, nx.png, py.png, ny.png, pz.png, nz.png)
// =============================================================================

#pragma once

#include <RHI/RHI.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Dot
{

/// Loaded cubemap data (6 faces, RGBA)
struct CubemapData
{
    static constexpr int FACE_COUNT = 6;

    int width = 0;
    int height = 0;
    int channels = 4; // Always convert to RGBA
    RHIFormat format = RHIFormat::R8G8B8A8_UNORM;
    uint32_t bytesPerChannel = 1;

    // Face order: +X, -X, +Y, -Y, +Z, -Z
    std::array<std::vector<uint8_t>, FACE_COUNT> faceData;

    bool IsValid() const { return width > 0 && height > 0 && !faceData[0].empty(); }
    bool IsHdr() const { return bytesPerChannel > 1 || format == RHIFormat::R32G32B32A32_FLOAT; }
    uint32_t GetFaceRowPitch() const { return static_cast<uint32_t>(width * channels * bytesPerChannel); }
};

/// Cubemap face names (expected filenames without extension)
inline const char* GetCubemapFaceName(int faceIndex)
{
    static const char* faceNames[] = {"px", "nx", "py", "ny", "pz", "nz"};
    return (faceIndex >= 0 && faceIndex < 6) ? faceNames[faceIndex] : "";
}

/// Load a cubemap from a single image file (cross layout)
/// Supports horizontal cross (4:3 aspect), vertical cross (3:4 aspect),
/// and horizontal strip (6:1 aspect)
/// Returns empty CubemapData on failure
CubemapData LoadCubemapFromImage(const std::string& imagePath);

/// Load a cubemap from a folder containing 6 PNG files
/// Expected files: px.png, nx.png, py.png, ny.png, pz.png, nz.png
/// Returns empty CubemapData on failure
CubemapData LoadCubemapFromFolder(const std::string& folderPath);

/// Load a cubemap from either a face folder or a single image path.
/// This is the preferred unified cubemap load entry point.
CubemapData LoadCubemap(const std::string& path);

/// Create a solid color cubemap (for testing/fallback)
CubemapData CreateSolidColorCubemap(int size, uint8_t r, uint8_t g, uint8_t b);

/// Create a debug marker cubemap with face labels (R/L/U/D/F/B)
/// Each face has a distinct color and a large letter centered on it
CubemapData CreateMarkerCubemap(int size = 256);

} // namespace Dot
