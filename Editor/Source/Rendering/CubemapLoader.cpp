// =============================================================================
// Dot Engine - Cubemap Loader Implementation
// =============================================================================

#include "CubemapLoader.h"

#include "Core/Assets/AssetManager.h"

#include "../../../ThirdParty/stb/stb_image.h"

#include <filesystem>

namespace Dot
{

CubemapData LoadCubemapFromFolder(const std::string& folderPath)
{
    CubemapData result;

    std::filesystem::path rootPath = AssetManager::Get().GetRootPath();
    std::filesystem::path basePath = rootPath / folderPath;

    // Check multiple locations relative to assets root
    if (!std::filesystem::exists(basePath))
    {
        basePath = rootPath.parent_path() / "Assets" / folderPath;
    }

    if (!std::filesystem::exists(basePath) || !std::filesystem::is_directory(basePath))
    {
        return result; // Empty/invalid
    }

    // Load each face
    for (int face = 0; face < CubemapData::FACE_COUNT; ++face)
    {
        std::filesystem::path facePath = basePath / (std::string(GetCubemapFaceName(face)) + ".png");

        if (!std::filesystem::exists(facePath))
        {
            // Try jpg as fallback
            facePath = basePath / (std::string(GetCubemapFaceName(face)) + ".jpg");
        }

        if (!std::filesystem::exists(facePath))
        {
            return result; // Missing face - return empty
        }

        int w, h, channels;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* data = stbi_load(facePath.string().c_str(), &w, &h, &channels, 4);

        if (!data)
        {
            return result; // Load failed
        }

        // Verify all faces have same dimensions
        if (face == 0)
        {
            result.width = w;
            result.height = h;
        }
        else if (w != result.width || h != result.height)
        {
            stbi_image_free(data);
            result = CubemapData{}; // Reset - mismatched sizes
            return result;
        }

        // Copy data
        size_t dataSize = static_cast<size_t>(w) * h * 4;
        result.faceData[face].resize(dataSize);
        memcpy(result.faceData[face].data(), data, dataSize);

        stbi_image_free(data);
    }

    return result;
}

CubemapData LoadCubemapFromImage(const std::string& imagePath)
{
    CubemapData result;

    // Try to find the image file - check multiple locations
    std::filesystem::path filePath;
    std::filesystem::path rootPath = AssetManager::Get().GetRootPath();

    std::vector<std::filesystem::path> searchPaths = {
        imagePath,                                                                // As given
        rootPath / "Cubemaps" / imagePath,                                        // Assets/Cubemaps/...
        rootPath.parent_path() / "Assets" / "Cubemaps" / imagePath,               // ../Assets/Cubemaps/...
        rootPath.parent_path().parent_path() / "Assets" / "Cubemaps" / imagePath, // ../../Assets/Cubemaps/...
        rootPath.parent_path().parent_path().parent_path() / "Assets" / "Cubemaps" /
            imagePath,                                                            // ../../../Assets/Cubemaps/...
        rootPath / imagePath,                                                     // Direct Assets/...
        rootPath.parent_path() / "Assets" / imagePath,                            // ../Assets/...
        rootPath.parent_path().parent_path() / "Assets" / imagePath,              // ../../Assets/...
        rootPath.parent_path().parent_path().parent_path() / "Assets" / imagePath // ../../../Assets/...
    };

    for (const auto& testPath : searchPaths)
    {
        if (std::filesystem::exists(testPath))
        {
            filePath = testPath;
            break;
        }
    }

    if (filePath.empty())
    {
        return result; // File not found
    }

    // Load the full image
    int imgWidth, imgHeight, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* imageData = stbi_load(filePath.string().c_str(), &imgWidth, &imgHeight, &channels, 4);

    if (!imageData)
    {
        return result; // Load failed
    }

    // Determine layout based on aspect ratio
    // Horizontal cross: 4:3 (e.g., 4096x3072 = 4 wide x 3 tall, face size = 1024)
    // Vertical cross: 3:4 (e.g., 3072x4096 = 3 wide x 4 tall, face size = 1024)
    // Horizontal strip: 6:1 (e.g., 6144x1024 = 6 wide x 1 tall, face size = 1024)

    int faceSize = 0;

    // Check for horizontal cross (4:3)
    if (imgWidth * 3 == imgHeight * 4)
    {
        faceSize = imgWidth / 4;
    }
    // Check for vertical cross (3:4)
    else if (imgWidth * 4 == imgHeight * 3)
    {
        faceSize = imgWidth / 3;
    }
    // Check for horizontal strip (6:1)
    else if (imgWidth == imgHeight * 6)
    {
        faceSize = imgHeight;
    }
    else
    {
        stbi_image_free(imageData);
        return result; // Unsupported aspect ratio
    }

    result.width = faceSize;
    result.height = faceSize;
    result.channels = 4;

    // Face positions in the source image (x, y in face units)
    // For horizontal cross layout (4:3):
    //       [+Y]
    // [-X] [+Z] [+X] [-Z]
    //       [-Y]
    // Face order: +X, -X, +Y, -Y, +Z, -Z

    struct FacePosition
    {
        int x, y;
    };
    std::array<FacePosition, 6> facePositions;

    if (imgWidth * 3 == imgHeight * 4) // Horizontal cross
    {
        facePositions = {{
            {2, 1}, // +X (right)
            {0, 1}, // -X (left)
            {1, 0}, // +Y (top)
            {1, 2}, // -Y (bottom)
            {1, 1}, // +Z (front/center)
            {3, 1}  // -Z (back)
        }};
    }
    else if (imgWidth * 4 == imgHeight * 3) // Vertical cross
    {
        facePositions = {{
            {2, 1}, // +X
            {0, 1}, // -X
            {1, 0}, // +Y
            {1, 2}, // -Y
            {1, 1}, // +Z
            {1, 3}  // -Z
        }};
    }
    else // Horizontal strip
    {
        facePositions = {{
            {0, 0}, // +X
            {1, 0}, // -X
            {2, 0}, // +Y
            {3, 0}, // -Y
            {4, 0}, // +Z
            {5, 0}  // -Z
        }};
    }

    // Extract each face
    for (int face = 0; face < 6; ++face)
    {
        result.faceData[face].resize(static_cast<size_t>(faceSize) * faceSize * 4);

        int srcX = facePositions[face].x * faceSize;
        int srcY = facePositions[face].y * faceSize;

        for (int y = 0; y < faceSize; ++y)
        {
            for (int x = 0; x < faceSize; ++x)
            {
                int srcIdx = ((srcY + y) * imgWidth + (srcX + x)) * 4;
                int dstIdx = (y * faceSize + x) * 4;

                result.faceData[face][dstIdx + 0] = imageData[srcIdx + 0];
                result.faceData[face][dstIdx + 1] = imageData[srcIdx + 1];
                result.faceData[face][dstIdx + 2] = imageData[srcIdx + 2];
                result.faceData[face][dstIdx + 3] = imageData[srcIdx + 3];
            }
        }
    }

    stbi_image_free(imageData);
    return result;
}

CubemapData CreateSolidColorCubemap(int size, uint8_t r, uint8_t g, uint8_t b)
{
    CubemapData result;
    result.width = size;
    result.height = size;
    result.channels = 4;

    size_t faceSize = static_cast<size_t>(size) * size * 4;

    for (int face = 0; face < CubemapData::FACE_COUNT; ++face)
    {
        result.faceData[face].resize(faceSize);

        // Fill with solid color
        for (size_t i = 0; i < faceSize; i += 4)
        {
            result.faceData[face][i + 0] = r;
            result.faceData[face][i + 1] = g;
            result.faceData[face][i + 2] = b;
            result.faceData[face][i + 3] = 255;
        }
    }

    return result;
}

CubemapData CreateMarkerCubemap(int size)
{
    CubemapData result;
    result.width = size;
    result.height = size;
    result.channels = 4;

    // Face colors: +X=Red(R), -X=Cyan(L), +Y=Green(U), -Y=Brown(D), +Z=Blue(F), -Z=Magenta(B)
    const uint8_t faceColors[6][3] = {
        {200, 50, 50},  // +X: Red (Right)
        {50, 200, 200}, // -X: Cyan (Left)
        {50, 200, 50},  // +Y: Green (Up)
        {150, 100, 50}, // -Y: Brown (Down)
        {50, 50, 200},  // +Z: Blue (Front)
        {200, 50, 200}  // -Z: Magenta (Back)
    };

    // 8x8 bitmap patterns for letters R, L, U, D, F, B
    // Each value is 8 bits representing one row of the letter
    const uint8_t letters[6][8] = {
        // R: Right (+X)
        {0b11111100, 0b10000010, 0b10000010, 0b11111100, 0b10001000, 0b10000100, 0b10000010, 0b10000010},
        // L: Left (-X)
        {0b10000000, 0b10000000, 0b10000000, 0b10000000, 0b10000000, 0b10000000, 0b10000000, 0b11111110},
        // U: Up (+Y)
        {0b10000010, 0b10000010, 0b10000010, 0b10000010, 0b10000010, 0b10000010, 0b01000100, 0b00111000},
        // D: Down (-Y)
        {0b11111000, 0b10000100, 0b10000010, 0b10000010, 0b10000010, 0b10000010, 0b10000100, 0b11111000},
        // F: Front (+Z)
        {0b11111110, 0b10000000, 0b10000000, 0b11111100, 0b10000000, 0b10000000, 0b10000000, 0b10000000},
        // B: Back (-Z)
        {0b11111100, 0b10000010, 0b10000010, 0b11111100, 0b10000010, 0b10000010, 0b10000010, 0b11111100}};

    size_t faceSize = static_cast<size_t>(size) * size * 4;

    for (int face = 0; face < CubemapData::FACE_COUNT; ++face)
    {
        result.faceData[face].resize(faceSize);

        uint8_t r = faceColors[face][0];
        uint8_t g = faceColors[face][1];
        uint8_t b = faceColors[face][2];

        // Fill with face color
        for (size_t i = 0; i < faceSize; i += 4)
        {
            result.faceData[face][i + 0] = r;
            result.faceData[face][i + 1] = g;
            result.faceData[face][i + 2] = b;
            result.faceData[face][i + 3] = 255;
        }

        // Draw letter in center with high contrast
        int letterScale = size / 8; // Scale letter to fit face
        int letterSize = 8 * letterScale;
        int startX = (size - letterSize) / 2;
        int startY = (size - letterSize) / 2;

        for (int row = 0; row < 8; ++row)
        {
            uint8_t bits = letters[face][row];
            for (int col = 0; col < 8; ++col)
            {
                bool isSet = (bits >> (7 - col)) & 1;
                if (isSet)
                {
                    // Draw scaled pixel block
                    for (int sy = 0; sy < letterScale; ++sy)
                    {
                        for (int sx = 0; sx < letterScale; ++sx)
                        {
                            int px = startX + col * letterScale + sx;
                            int py = startY + row * letterScale + sy;
                            if (px >= 0 && px < size && py >= 0 && py < size)
                            {
                                size_t idx = (static_cast<size_t>(py) * size + px) * 4;
                                // White letter with black outline effect
                                result.faceData[face][idx + 0] = 255;
                                result.faceData[face][idx + 1] = 255;
                                result.faceData[face][idx + 2] = 255;
                                result.faceData[face][idx + 3] = 255;
                            }
                        }
                    }
                }
            }
        }
    }

    return result;
}

} // namespace Dot
