// =============================================================================
// Dot Engine - Material Importer Utility
// =============================================================================
// Automatically creates MaterialGraph materials from FBX files.
// =============================================================================

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Dot
{
class MaterialImporter
{
public:
    /// Extracts ALL materials from FBX and creates .dotmat files for each
    /// @param fbxPath Relative path to FBX (e.g. "Models/test.fbx")
    /// @return Vector of relative paths to created materials
    static std::vector<std::string> ImportAllFromFbx(const std::string& fbxPath);

    /// Extracts FIRST material from FBX (legacy/convenience)
    /// @param fbxPath Relative path to FBX (e.g. "Models/test.fbx")
    /// @return Relative path to first material, or empty if none
    static std::string ImportFromFbx(const std::string& fbxPath);

private:
    /// Recursively search for texture file starting from a root directory
    static std::filesystem::path FindTextureRecursive(const std::filesystem::path& searchRoot,
                                                      const std::string& filename, int maxDepth = 3);

    /// Extract and copy texture to project, returns relative path
    static std::string ExtractTexture(const std::string& fbxFullPath, const std::string& rawTexturePath);

    /// Create a single material graph for the given texture
    static bool CreateMaterialGraph(const std::string& matFullPath, const std::string& texRelativePath);
};
} // namespace Dot
