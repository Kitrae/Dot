// =============================================================================
// Dot Engine - Material Importer Utility Implementation
// =============================================================================

#include "MaterialImporter.h"

#include "Core/Assets/AssetManager.h"
#include "Core/Log.h"
#include "Core/Material/MaterialGraph.h"
#include "Core/Material/MaterialNode.h"
#include "Core/Material/MaterialTextureUtils.h"
#include "Core/Material/NodeProperty.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ufbx.h>

namespace Dot
{

namespace
{

std::string UfbxStringToStd(ufbx_string value)
{
    if (!value.data || value.length == 0)
        return {};
    return std::string(value.data, value.length);
}

std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool ContainsAnyToken(const std::string& haystack, std::initializer_list<const char*> tokens)
{
    for (const char* token : tokens)
    {
        if (haystack.find(token) != std::string::npos)
            return true;
    }
    return false;
}

bool TryGetEmbeddedTextureBlob(ufbx_texture* tex, const void*& blobData, size_t& blobSize)
{
    blobData = nullptr;
    blobSize = 0;
    if (!tex)
        return false;

    if (tex->content.data && tex->content.size > 0)
    {
        blobData = tex->content.data;
        blobSize = tex->content.size;
        return true;
    }

    if (tex->video && tex->video->content.data && tex->video->content.size > 0)
    {
        blobData = tex->video->content.data;
        blobSize = tex->video->content.size;
        return true;
    }

    return false;
}

bool HasTexturePathMetadata(ufbx_texture* tex)
{
    if (!tex)
        return false;

    if (!UfbxStringToStd(tex->filename).empty() || !UfbxStringToStd(tex->relative_filename).empty())
        return true;

    if (tex->video &&
        (!UfbxStringToStd(tex->video->filename).empty() || !UfbxStringToStd(tex->video->relative_filename).empty()))
    {
        return true;
    }

    return false;
}

bool HasUsableTextureSource(ufbx_texture* tex)
{
    const void* blobData = nullptr;
    size_t blobSize = 0;
    return TryGetEmbeddedTextureBlob(tex, blobData, blobSize) || HasTexturePathMetadata(tex);
}

std::string GuessEmbeddedTextureExtension(const void* data, size_t size)
{
    if (!data || size < 4)
        return ".bin";

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47)
        return ".png";
    if (bytes[0] == 0xFF && bytes[1] == 0xD8)
        return ".jpg";
    if (size >= 4 && bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ')
        return ".dds";
    if (size >= 4 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F')
        return ".webp";
    if (size >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
        return ".bmp";
    return ".bin";
}

std::string SanitizeMaterialName(std::string name, size_t fallbackIndex)
{
    if (name.empty())
        name = "Material" + std::to_string(fallbackIndex);

    std::replace(name.begin(), name.end(), ' ', '_');
    std::replace(name.begin(), name.end(), '/', '_');
    std::replace(name.begin(), name.end(), '\\', '_');
    return name;
}

int ScoreMaterialTextureBinding(const ufbx_material_texture& materialTexture)
{
    if (!materialTexture.texture || !HasUsableTextureSource(materialTexture.texture))
        return -1;

    int score = 0;
    const std::string shaderProp = ToLowerAscii(UfbxStringToStd(materialTexture.shader_prop));
    const std::string materialProp = ToLowerAscii(UfbxStringToStd(materialTexture.material_prop));

    if (ContainsAnyToken(shaderProp, {"base_color", "basecolor", "diffuse", "albedo"}))
        score += 100;
    if (ContainsAnyToken(materialProp, {"basecolor", "base_color", "diffuse", "albedo", "color", "colour"}))
        score += 80;

    const void* blobData = nullptr;
    size_t blobSize = 0;
    if (TryGetEmbeddedTextureBlob(materialTexture.texture, blobData, blobSize))
        score += 40;
    if (HasTexturePathMetadata(materialTexture.texture))
        score += 20;
    if (materialTexture.texture->type == UFBX_TEXTURE_FILE)
        score += 10;
    if (materialTexture.texture->has_file)
        score += 5;

    return score;
}

ufbx_texture* FindBestMaterialTexture(ufbx_material* mat)
{
    if (!mat)
        return nullptr;

    if (mat->pbr.base_color.texture && mat->pbr.base_color.texture_enabled &&
        HasUsableTextureSource(mat->pbr.base_color.texture))
    {
        return mat->pbr.base_color.texture;
    }
    if (mat->fbx.diffuse_color.texture && mat->fbx.diffuse_color.texture_enabled &&
        HasUsableTextureSource(mat->fbx.diffuse_color.texture))
    {
        return mat->fbx.diffuse_color.texture;
    }

    ufbx_texture* fallbackTexture = nullptr;
    int bestScore = -1;
    for (size_t i = 0; i < mat->textures.count; ++i)
    {
        const ufbx_material_texture& materialTexture = mat->textures.data[i];
        const int score = ScoreMaterialTextureBinding(materialTexture);
        if (score > bestScore)
        {
            bestScore = score;
            fallbackTexture = materialTexture.texture;
        }
    }

    return fallbackTexture;
}

std::string ExtractEmbeddedTextureToProject(ufbx_texture* tex, const std::string& materialName)
{
    if (!tex)
        return "";

    const void* blobData = nullptr;
    size_t blobSize = 0;
    if (!TryGetEmbeddedTextureBlob(tex, blobData, blobSize))
        return "";

    std::string texFilename = std::filesystem::path(UfbxStringToStd(tex->filename)).filename().string();
    if (texFilename.empty())
        texFilename = std::filesystem::path(UfbxStringToStd(tex->relative_filename)).filename().string();
    if (texFilename.empty() && tex->video)
        texFilename = std::filesystem::path(UfbxStringToStd(tex->video->filename)).filename().string();
    if (texFilename.empty() && tex->video)
        texFilename = std::filesystem::path(UfbxStringToStd(tex->video->relative_filename)).filename().string();
    if (texFilename.empty())
        texFilename = std::filesystem::path(UfbxStringToStd(tex->name)).filename().string();
    if (texFilename.empty() && tex->video)
        texFilename = std::filesystem::path(UfbxStringToStd(tex->video->name)).filename().string();

    if (texFilename.empty())
        texFilename = "embedded_" + materialName + GuessEmbeddedTextureExtension(blobData, blobSize);
    else if (!std::filesystem::path(texFilename).has_extension())
        texFilename += GuessEmbeddedTextureExtension(blobData, blobSize);

    std::string texFullPath = AssetManager::Get().GetFullPath("Textures/" + texFilename);
    std::filesystem::path texDir = std::filesystem::path(texFullPath).parent_path();
    if (!std::filesystem::exists(texDir))
        std::filesystem::create_directories(texDir);

    if (!std::filesystem::exists(texFullPath))
    {
        std::ofstream outFile(texFullPath, std::ios::binary);
        if (!outFile.is_open())
            return "";

        outFile.write(static_cast<const char*>(blobData), static_cast<std::streamsize>(blobSize));
        outFile.close();
        DOT_LOG_INFO("MaterialImporter: Extracted embedded texture: %s (%zu bytes)", texFilename.c_str(), blobSize);
    }

    return "Textures/" + texFilename;
}

} // namespace

std::vector<std::string> MaterialImporter::ImportAllFromFbx(const std::string& fbxPath)
{
    std::vector<std::string> createdMaterials;
    std::string fullFbxPath = AssetManager::Get().GetFullPath(fbxPath);

    ufbx_load_opts opts = {0};
    opts.ignore_geometry = true; // We only need metadata/materials

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(fullFbxPath.c_str(), &opts, &error);
    if (!scene)
    {
        DOT_LOG_ERROR("MaterialImporter: Failed to load FBX metadata for %s: %s", fbxPath.c_str(),
                      error.description.data);
        return createdMaterials;
    }

    std::filesystem::path fbxP(fbxPath);
    std::string meshName = fbxP.stem().string();

    // Create Materials directory
    std::string matDirRelative = "Materials/";
    std::string matDirFull = AssetManager::Get().GetFullPath(matDirRelative);
    if (!std::filesystem::exists(matDirFull))
    {
        std::filesystem::create_directories(matDirFull);
    }

    // Process ALL materials in the scene
    for (size_t i = 0; i < scene->materials.count; ++i)
    {
        ufbx_material* mat = scene->materials.data[i];

        std::string matName = SanitizeMaterialName(mat->name.data, i);

        ufbx_texture* tex = FindBestMaterialTexture(mat);

        const void* embeddedData = nullptr;
        size_t embeddedSize = 0;
        const bool hasEmbeddedContent = tex && TryGetEmbeddedTextureBlob(tex, embeddedData, embeddedSize);
        const bool hasFilename = tex && HasTexturePathMetadata(tex);
        if (!tex || (!hasFilename && !hasEmbeddedContent))
        {
            DOT_LOG_INFO("MaterialImporter: Material '%s' has no texture, skipping", matName.c_str());
            continue;
        }

        // Material output path
        std::string matRelativePath = matDirRelative + meshName + "_" + matName + ".dotmat";
        std::string matFullPath = AssetManager::Get().GetFullPath(matRelativePath);

        // Skip if already exists
        if (std::filesystem::exists(matFullPath))
        {
            DOT_LOG_INFO("MaterialImporter: Material already exists: %s", matRelativePath.c_str());
            createdMaterials.push_back(matRelativePath);
            continue;
        }

        // Extract texture - check for embedded content first
        std::string texRelativePath;
        if (hasEmbeddedContent)
        {
            texRelativePath = ExtractEmbeddedTextureToProject(tex, matName);
        }
        else
        {
            // External texture - search filesystem
            std::string rawTexturePath = UfbxStringToStd(tex->filename);
            if (rawTexturePath.empty())
                rawTexturePath = UfbxStringToStd(tex->relative_filename);
            if (rawTexturePath.empty() && tex->video)
                rawTexturePath = UfbxStringToStd(tex->video->filename);
            if (rawTexturePath.empty() && tex->video)
                rawTexturePath = UfbxStringToStd(tex->video->relative_filename);
            texRelativePath = ExtractTexture(fullFbxPath, rawTexturePath);
        }

        if (texRelativePath.empty())
        {
            DOT_LOG_WARN("MaterialImporter: Texture extraction failed for material '%s'", matName.c_str());
            continue;
        }

        // Create material graph
        if (CreateMaterialGraph(matFullPath, texRelativePath))
        {
            DOT_LOG_INFO("MaterialImporter: Created material: %s", matRelativePath.c_str());
            createdMaterials.push_back(matRelativePath);
        }
    }

    ufbx_free_scene(scene);

    DOT_LOG_INFO("MaterialImporter: Imported %zu materials from %s", createdMaterials.size(), fbxPath.c_str());
    return createdMaterials;
}

std::string MaterialImporter::ImportFromFbx(const std::string& fbxPath)
{
    auto materials = ImportAllFromFbx(fbxPath);
    return materials.empty() ? "" : materials[0];
}

std::filesystem::path MaterialImporter::FindTextureRecursive(const std::filesystem::path& searchRoot,
                                                             const std::string& filename, int maxDepth)
{
    if (maxDepth <= 0 || !std::filesystem::exists(searchRoot))
        return {};

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(searchRoot))
        {
            if (entry.is_regular_file())
            {
                if (entry.path().filename().string() == filename)
                    return entry.path();
            }
            else if (entry.is_directory())
            {
                auto result = FindTextureRecursive(entry.path(), filename, maxDepth - 1);
                if (!result.empty())
                    return result;
            }
        }
    }
    catch (const std::exception&)
    {
        // Permission denied or other errors - ignore and continue
    }

    return {};
}

std::string MaterialImporter::ExtractTexture(const std::string& fbxFullPath, const std::string& rawTexturePath)
{
    std::filesystem::path fbxDir = std::filesystem::path(fbxFullPath).parent_path();
    std::filesystem::path rawP(rawTexturePath);
    std::string texFilename = rawP.filename().string();

    // Potential source paths (ordered by priority):
    std::vector<std::filesystem::path> candidates = {
        rawP,                              // 1. Absolute as provided
        fbxDir / texFilename,              // 2. Same folder as FBX
        fbxDir / "textures" / texFilename, // 3. textures/ subfolder
        fbxDir / "Textures" / texFilename, // 4. Textures/ subfolder
        fbxDir / "source" / texFilename,   // 5. source/ subfolder
    };

    std::filesystem::path foundSource;

    // Try explicit candidates first
    for (const auto& p : candidates)
    {
        if (std::filesystem::exists(p))
        {
            foundSource = p;
            break;
        }
    }

    // If not found, do recursive search from FBX directory
    if (foundSource.empty())
    {
        DOT_LOG_INFO("MaterialImporter: Searching recursively for %s...", texFilename.c_str());
        foundSource = FindTextureRecursive(fbxDir, texFilename, 4);
    }

    if (foundSource.empty())
    {
        DOT_LOG_WARN("MaterialImporter: Could not find texture file: %s", rawTexturePath.c_str());
        return "";
    }

    DOT_LOG_INFO("MaterialImporter: Found texture at: %s", foundSource.string().c_str());

    // Target path in project
    std::string texRelativePath = "Textures/" + texFilename;
    std::string texFullPath = AssetManager::Get().GetFullPath(texRelativePath);

    std::filesystem::path texDir = std::filesystem::path(texFullPath).parent_path();
    if (!std::filesystem::exists(texDir))
    {
        std::filesystem::create_directories(texDir);
    }

    try
    {
        if (!std::filesystem::exists(texFullPath))
        {
            std::filesystem::copy_file(foundSource, texFullPath);
            DOT_LOG_INFO("MaterialImporter: Copied texture to project: %s", texRelativePath.c_str());
        }
        return texRelativePath;
    }
    catch (const std::exception& e)
    {
        DOT_LOG_ERROR("MaterialImporter: Failed to copy texture: %s", e.what());
        return "";
    }
}

bool MaterialImporter::CreateMaterialGraph(const std::string& matFullPath, const std::string& texRelativePath)
{
    MaterialGraph graph;

    // Add Texture2D node if we have a texture
    if (!texRelativePath.empty())
    {
        MaterialNode* texNode = graph.AddNode(MaterialNodeType::Texture2D);
        if (texNode)
        {
            auto* pathProp = texNode->GetProperty<TexturePathProperty>();
            auto* sampleTypeProp = texNode->GetProperty<TextureSampleTypeProperty>();
            if (pathProp)
            {
                pathProp->path = texRelativePath;
            }
            if (sampleTypeProp)
            {
                sampleTypeProp->value = static_cast<int>(GuessTextureSampleTypeFromPath(texRelativePath));
            }
            texNode->posX = -300.0f;
            texNode->posY = 0.0f;

            // Connect Texture Node to PBR Output
            PBROutputNode* outputNode = graph.GetOutputNode();
            if (outputNode)
            {
                int outputPinId = -1;
                for (const auto& pin : texNode->GetOutputs())
                {
                    if (pin.name == "RGB")
                    {
                        outputPinId = pin.id;
                        break;
                    }
                }

                int inputPinId = -1;
                int roughnessInputPinId = -1;
                int metallicInputPinId = -1;
                int aoInputPinId = -1;
                int rOutputPinId = -1;
                int gOutputPinId = -1;
                int bOutputPinId = -1;
                for (const auto& pin : outputNode->GetInputs())
                {
                    const bool isNormalTexture =
                        sampleTypeProp &&
                        sampleTypeProp->value == static_cast<int>(TextureSampleType::Normal);
                    const bool isMaskTexture =
                        sampleTypeProp && sampleTypeProp->value == static_cast<int>(TextureSampleType::Mask);
                    if ((!isNormalTexture && !isMaskTexture && pin.name == "Albedo") ||
                        (isNormalTexture && pin.name == "Normal"))
                    {
                        inputPinId = pin.id;
                        break;
                    }
                    if (pin.name == "Roughness")
                        roughnessInputPinId = pin.id;
                    else if (pin.name == "Metallic")
                        metallicInputPinId = pin.id;
                    else if (pin.name == "Ambient Occlusion")
                        aoInputPinId = pin.id;
                }

                for (const auto& pin : texNode->GetOutputs())
                {
                    if (pin.name == "R")
                        rOutputPinId = pin.id;
                    else if (pin.name == "G")
                        gOutputPinId = pin.id;
                    else if (pin.name == "B")
                        bOutputPinId = pin.id;
                }

                if (outputPinId != -1 && inputPinId != -1)
                {
                    graph.Connect(outputPinId, inputPinId);
                }
                else if (sampleTypeProp &&
                         sampleTypeProp->value == static_cast<int>(TextureSampleType::Mask))
                {
                    if (aoInputPinId != -1 && rOutputPinId != -1)
                        graph.Connect(rOutputPinId, aoInputPinId);
                    if (roughnessInputPinId != -1 && gOutputPinId != -1)
                        graph.Connect(gOutputPinId, roughnessInputPinId);
                    if (metallicInputPinId != -1 && bOutputPinId != -1)
                        graph.Connect(bOutputPinId, metallicInputPinId);
                }
            }
        }
    }

    return graph.SaveToFile(matFullPath);
}

} // namespace Dot
