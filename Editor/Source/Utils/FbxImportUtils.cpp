// =============================================================================
// Dot Engine - FBX Editor Import Utilities
// =============================================================================

#include "FbxImportUtils.h"

#include "MaterialImporter.h"

#include "Core/Assets/AssetManager.h"
#include "Core/ECS/World.h"
#include "Core/Log.h"
#include "Core/Scene/MaterialComponent.h"

#include "../Rendering/FbxLoader.h"

#include <algorithm>
#include <cctype>

namespace Dot
{

void ImportFbxMaterialsForAsset(const std::string& relativeMeshPath)
{
    if (relativeMeshPath.empty())
        return;

    std::string extension = std::filesystem::path(relativeMeshPath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".fbx")
        return;

    const std::vector<std::string> importedMaterials = MaterialImporter::ImportAllFromFbx(relativeMeshPath);
    if (!importedMaterials.empty())
        DOT_LOG_INFO("Imported %zu FBX materials for %s", importedMaterials.size(), relativeMeshPath.c_str());
}

void CopyFbxSidecarDirectoriesIfPresent(const std::filesystem::path& sourceFbxPath, const std::filesystem::path& destDir)
{
    if (sourceFbxPath.empty() || destDir.empty())
        return;

    std::vector<std::filesystem::path> sidecarSources;
    const std::filesystem::path exactSidecar = sourceFbxPath.parent_path() / (sourceFbxPath.stem().string() + ".fbm");
    if (std::filesystem::exists(exactSidecar) && std::filesystem::is_directory(exactSidecar))
        sidecarSources.push_back(exactSidecar);

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(sourceFbxPath.parent_path()))
        {
            if (!entry.is_directory())
                continue;

            std::filesystem::path candidate = entry.path();
            std::string extension = candidate.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".fbm")
                continue;

            if (std::find(sidecarSources.begin(), sidecarSources.end(), candidate) == sidecarSources.end())
                sidecarSources.push_back(candidate);
        }
    }
    catch (const std::exception& e)
    {
        DOT_LOG_WARN("Failed to scan FBX sidecar folders near '%s': %s", sourceFbxPath.string().c_str(), e.what());
    }

    for (const std::filesystem::path& sidecarSource : sidecarSources)
    {
        std::filesystem::path sidecarDest = destDir / sidecarSource.filename();
        try
        {
            std::filesystem::create_directories(sidecarDest);
            std::filesystem::copy(sidecarSource, sidecarDest,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing);
            DOT_LOG_INFO("Copied FBX sidecar texture folder: %s", sidecarSource.filename().string().c_str());
        }
        catch (const std::exception& e)
        {
            DOT_LOG_WARN("Failed to copy FBX sidecar folder '%s': %s", sidecarSource.string().c_str(), e.what());
        }
    }
}

size_t GetFbxSubmeshCountForAsset(const std::string& relativeMeshPath)
{
    if (relativeMeshPath.empty())
        return 0;

    std::string extension = std::filesystem::path(relativeMeshPath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".fbx")
        return 0;

    return GetFbxSubmeshCount(AssetManager::Get().GetFullPath(relativeMeshPath));
}

std::vector<std::string> GetFbxSubmeshMaterialPaths(const std::string& relativeMeshPath)
{
    std::vector<std::string> materialPaths;
    if (relativeMeshPath.empty())
        return materialPaths;

    std::string extension = std::filesystem::path(relativeMeshPath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".fbx")
        return materialPaths;

    std::vector<MeshData> meshesData;
    std::string unusedColormap;
    const std::string fullPath = AssetManager::Get().GetFullPath(relativeMeshPath);
    const std::string meshBaseName = std::filesystem::path(relativeMeshPath).stem().string();
    if (!LoadFbxFile(fullPath, meshesData, unusedColormap, meshBaseName))
        return materialPaths;

    materialPaths.reserve(meshesData.size());
    for (const MeshData& meshData : meshesData)
    {
        if (!meshData.submeshes.empty())
            materialPaths.push_back(meshData.submeshes[0].materialPath);
        else
            materialPaths.emplace_back();
    }

    return materialPaths;
}

void AutoAssignImportedMeshMaterial(World& world, Entity entity, const std::vector<std::string>& materialPaths,
                                    int32_t submeshIndex)
{
    if (!entity.IsValid() || !world.IsAlive(entity) || submeshIndex < 0 ||
        static_cast<size_t>(submeshIndex) >= materialPaths.size())
    {
        return;
    }

    const std::string& materialPath = materialPaths[static_cast<size_t>(submeshIndex)];
    if (materialPath.empty())
        return;

    MaterialComponent* material = world.GetComponent<MaterialComponent>(entity);
    if (!material)
        material = &world.AddComponent<MaterialComponent>(entity);

    material->useMaterialFile = true;
    material->materialPath = materialPath;
}

void ClearExplicitMeshMaterialOverride(World& world, Entity entity)
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return;
    if (world.HasComponent<MaterialComponent>(entity))
        world.RemoveComponent<MaterialComponent>(entity);
}

} // namespace Dot
