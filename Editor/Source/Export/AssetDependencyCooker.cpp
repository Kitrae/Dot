#include "AssetDependencyCooker.h"

#include <Core/ECS/World.h>
#include <Core/Log.h>
#include <Core/Map/MapSerializer.h>
#include <Core/Material/MaterialLoader.h>
#include <Core/Scene/ComponentReflection.h>
#include <Core/Scene/Components.h>
#include <Core/Scene/LightComponent.h>
#include <Core/Scene/MapComponent.h>
#include <Core/Scene/MaterialComponent.h>
#include <Core/Scene/MeshComponent.h>
#include <Core/Scene/Prefab.h>
#include <Core/Scene/PrefabComponent.h>
#include <Core/Scene/SceneSerializer.h>
#include <Core/Scene/SceneSettingsSerializer.h>
#include <Core/Scene/ScriptComponent.h>
#include <Core/Scene/SkyboxComponent.h>

#include "../Rendering/FbxLoader.h"
#include "../Rendering/ObjLoader.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace Dot
{

namespace
{

std::string NormalizeRelativeAssetPath(const std::filesystem::path& path)
{
    return path.lexically_normal().generic_string();
}

const char* ToString(CookedAssetKind kind)
{
    switch (kind)
    {
        case CookedAssetKind::Scene:
            return "Scene";
        case CookedAssetKind::Map:
            return "Map";
        case CookedAssetKind::Material:
            return "Material";
        case CookedAssetKind::Mesh:
            return "Mesh";
        case CookedAssetKind::Script:
            return "Script";
        case CookedAssetKind::Cubemap:
            return "Cubemap";
        case CookedAssetKind::Texture:
            return "Texture";
        case CookedAssetKind::Prefab:
            return "Prefab";
        case CookedAssetKind::Generic:
        default:
            return "Generic";
    }
}

CookedAssetKind MergeAssetKind(CookedAssetKind existing, CookedAssetKind incoming)
{
    if (existing == CookedAssetKind::Generic && incoming != CookedAssetKind::Generic)
        return incoming;
    return existing;
}

std::string EscapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value)
    {
        switch (ch)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string BuildUtcTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &nowTime);
#else
    gmtime_r(&nowTime, &utcTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

} // namespace

bool AssetDependencyCooker::Collect(const std::filesystem::path& assetsRoot, const std::string& startupScene)
{
    m_LastError.clear();
    m_CookedAssets.clear();
    m_CookedAssetKinds.clear();
    m_ExternalSourcePaths.clear();
    m_AssetsRoot = assetsRoot;

    if (m_AssetsRoot.empty() || !std::filesystem::exists(m_AssetsRoot))
    {
        m_LastError = "Assets root does not exist.";
        return false;
    }

    RegisterSceneComponents();

    std::string resolvedScene;
    if (!AddResolvedAsset(startupScene, CookedAssetKind::Scene, &resolvedScene))
        return false;

    return CollectScene(resolvedScene);
}

bool AssetDependencyCooker::StageTo(const std::filesystem::path& destinationAssetsRoot) const
{
    std::error_code ec;
    std::filesystem::create_directories(destinationAssetsRoot, ec);
    if (ec)
        return false;

    for (const std::string& assetPath : m_CookedAssets)
    {
        if (!StageSingleAsset(destinationAssetsRoot, assetPath))
            return false;
    }
    return true;
}

bool AssetDependencyCooker::WriteManifest(const std::filesystem::path& path, const std::string& startupScene) const
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream file(path);
    if (!file.is_open())
        return false;

    const std::vector<CookedAssetRecord> records = GetCookedAssetRecords();

    file << "{\n";
    file << "  \"cook\": {\n";
    file << "    \"version\": 1,\n";
    file << "    \"generatedAtUtc\": \"" << EscapeJson(BuildUtcTimestamp()) << "\",\n";
    file << "    \"startupScene\": \"" << EscapeJson(startupScene) << "\",\n";
    file << "    \"assetCount\": " << records.size() << ",\n";
    file << "    \"totalSourceBytes\": " << GetTotalSourceBytes() << ",\n";
    file << "    \"assets\": [\n";
    for (size_t i = 0; i < records.size(); ++i)
    {
        const CookedAssetRecord& record = records[i];
        file << "      {\n";
        file << "        \"path\": \"" << EscapeJson(record.path) << "\",\n";
        file << "        \"kind\": \"" << ToString(record.kind) << "\",\n";
        file << "        \"sourceBytes\": " << record.sourceBytes << "\n";
        file << "      }";
        if (i + 1 < records.size())
            file << ",";
        file << "\n";
    }
    file << "    ]\n";
    file << "  }\n";
    file << "}\n";
    return true;
}

std::vector<CookedAssetRecord> AssetDependencyCooker::GetCookedAssetRecords() const
{
    std::vector<CookedAssetRecord> records;
    records.reserve(m_CookedAssets.size());

    for (const std::string& assetPath : m_CookedAssets)
    {
        CookedAssetRecord record;
        record.path = assetPath;

        const auto kindIt = m_CookedAssetKinds.find(assetPath);
        if (kindIt != m_CookedAssetKinds.end())
            record.kind = kindIt->second;

        std::error_code ec;
        const std::filesystem::path sourcePath = GetAssetSourcePath(assetPath);
        record.sourceBytes = static_cast<uint64>(std::filesystem::file_size(sourcePath, ec));
        if (ec)
            record.sourceBytes = 0;

        records.push_back(std::move(record));
    }

    std::sort(records.begin(), records.end(),
              [](const CookedAssetRecord& a, const CookedAssetRecord& b) { return a.path < b.path; });
    return records;
}

uint64 AssetDependencyCooker::GetTotalSourceBytes() const
{
    uint64 totalBytes = 0;
    for (const CookedAssetRecord& record : GetCookedAssetRecords())
        totalBytes += record.sourceBytes;
    return totalBytes;
}

bool AssetDependencyCooker::CollectScene(const std::string& scenePath)
{
    World world;
    SceneSerializer serializer;
    if (!serializer.Load(world, (m_AssetsRoot / scenePath).string()))
    {
        m_LastError = serializer.GetLastError();
        return false;
    }

    const std::string& settingsReference = serializer.GetSceneSettingsReference();
    if (!settingsReference.empty())
    {
        const std::filesystem::path explicitSettingsPath =
            std::filesystem::path(scenePath).parent_path() / settingsReference;
        if (!AddResolvedAsset(explicitSettingsPath.generic_string(), CookedAssetKind::Generic))
            return false;

        SceneSettingsAsset settings;
        SceneSettingsSerializer settingsSerializer;
        if (settingsSerializer.Load(settings, m_AssetsRoot / explicitSettingsPath))
        {
            if (!settings.mapPath.empty() && !AddResolvedAsset(settings.mapPath, CookedAssetKind::Map))
                return false;
            if (!settings.cubemapPath.empty() && !AddResolvedAsset(settings.cubemapPath, CookedAssetKind::Cubemap))
                return false;
        }
    }

    if (!CollectSceneSettingsSidecar(scenePath))
        return false;

    world.Each<MeshComponent>(
        [&](Entity, MeshComponent& mesh)
        {
            if (!mesh.meshPath.empty())
                AddResolvedAsset(mesh.meshPath, CookedAssetKind::Mesh);
        });

    world.Each<MaterialComponent>(
        [&](Entity, MaterialComponent& material)
        {
            if (material.useMaterialFile && !material.materialPath.empty())
                AddResolvedAsset(material.materialPath, CookedAssetKind::Material);
        });

    world.Each<ScriptComponent>(
        [&](Entity, ScriptComponent& script)
        {
            if (!script.scriptPath.empty())
                AddResolvedAsset(script.scriptPath, CookedAssetKind::Script);
        });

    world.Each<PrefabComponent>(
        [&](Entity, PrefabComponent& prefab)
        {
            if (!prefab.prefabPath.empty())
                AddResolvedAsset(prefab.prefabPath, CookedAssetKind::Prefab);
        });

    world.Each<MapComponent>(
        [&](Entity, MapComponent& map)
        {
            if (!map.mapPath.empty())
                AddResolvedAsset(map.mapPath, CookedAssetKind::Map);
        });

    world.Each<SkyboxComponent>(
        [&](Entity, SkyboxComponent& skybox)
        {
            if (!skybox.cubemapPath.empty())
                AddResolvedAsset(skybox.cubemapPath, CookedAssetKind::Cubemap);
        });

    world.Each<ReflectionProbeComponent>(
        [&](Entity, ReflectionProbeComponent& probe)
        {
            if (!probe.cubemapPath.empty())
                AddResolvedAsset(probe.cubemapPath, CookedAssetKind::Cubemap);
        });

    return m_LastError.empty();
}

bool AssetDependencyCooker::CollectSceneSettingsSidecar(const std::string& scenePath)
{
    const std::string sidecar = scenePath + ".settings.json";
    const std::filesystem::path sidecarFullPath = m_AssetsRoot / sidecar;
    if (!std::filesystem::exists(sidecarFullPath))
        return true;

    if (!AddResolvedAsset(sidecar, CookedAssetKind::Generic))
        return false;

    SceneSettingsAsset settings;
    SceneSettingsSerializer serializer;
    if (!serializer.Load(settings, sidecarFullPath))
    {
        m_LastError = serializer.GetLastError();
        return false;
    }

    if (!settings.mapPath.empty() && !AddResolvedAsset(settings.mapPath, CookedAssetKind::Map))
        return false;
    if (!settings.cubemapPath.empty() && !AddResolvedAsset(settings.cubemapPath, CookedAssetKind::Cubemap))
        return false;
    return true;
}

bool AssetDependencyCooker::CollectMap(const std::string& mapPath)
{
    MapAsset mapAsset;
    MapSerializer serializer;
    if (!serializer.Load(mapAsset, (m_AssetsRoot / mapPath).string()))
    {
        m_LastError = serializer.GetLastError();
        return false;
    }

    for (const MapBrush& brush : mapAsset.brushes)
    {
        for (const MapFace& face : brush.faces)
        {
            if (!face.materialPath.empty() && !AddResolvedAsset(face.materialPath, CookedAssetKind::Material))
                return false;
            if (!face.bakedLighting.lightmapTexturePath.empty() &&
                !AddResolvedAsset(face.bakedLighting.lightmapTexturePath, CookedAssetKind::Texture))
            {
                return false;
            }
            if (!face.bakedLighting.lightmapSidecarPath.empty() &&
                !AddResolvedAsset(face.bakedLighting.lightmapSidecarPath, CookedAssetKind::Generic))
            {
                return false;
            }
        }
    }

    return true;
}

bool AssetDependencyCooker::CollectMaterial(const std::string& materialPath)
{
    const LoadedMaterial material = MaterialLoader::Load((m_AssetsRoot / materialPath).string());
    if (!material.valid)
        return true;

    for (const std::string& texturePath : material.texturePaths)
    {
        if (!texturePath.empty() && !AddResolvedAsset(texturePath, CookedAssetKind::Texture))
            return false;
    }
    return true;
}

bool AssetDependencyCooker::CollectMeshSidecarDependencies(const std::string& meshPath)
{
    const std::filesystem::path fullPath = m_AssetsRoot / meshPath;
    std::string extension = fullPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (extension == ".fbx")
    {
        std::vector<MeshData> meshes;
        std::string colormapPath;
        if (!LoadFbxFile(fullPath.string(), meshes, colormapPath, fullPath.stem().string()))
            return true;

        for (const MeshData& meshData : meshes)
        {
            for (const Submesh& submesh : meshData.submeshes)
            {
                if (!submesh.materialPath.empty())
                {
                    const std::string resolvedMaterial = ResolveAssetPath(submesh.materialPath, CookedAssetKind::Material);
                    if (!resolvedMaterial.empty() &&
                        !AddResolvedAsset(resolvedMaterial, CookedAssetKind::Material))
                    {
                        return false;
                    }
                }
            }
        }
    }
    else if (extension == ".obj")
    {
        MeshData meshData;
        if (!LoadObjFile(fullPath, meshData))
            return true;

        for (const Submesh& submesh : meshData.submeshes)
        {
            if (!submesh.materialPath.empty())
            {
                const std::string resolvedMaterial = ResolveAssetPath(submesh.materialPath, CookedAssetKind::Material);
                if (!resolvedMaterial.empty() &&
                    !AddResolvedAsset(resolvedMaterial, CookedAssetKind::Material))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

bool AssetDependencyCooker::CollectPrefab(const std::string& prefabPath)
{
    Prefab prefab;
    if (!prefab.LoadFromFile((m_AssetsRoot / prefabPath).string()))
    {
        m_LastError = "Failed to load prefab: " + prefabPath;
        return false;
    }

    for (const PrefabEntity& entity : prefab.GetEntities())
    {
        for (const auto& [typeName, componentJson] : entity.components)
        {
            if (typeName == "MeshComponent")
            {
                const std::string meshPath = FindJsonStringValue(componentJson, "meshPath");
                if (!meshPath.empty() && !AddResolvedAsset(meshPath, CookedAssetKind::Mesh))
                    return false;
            }
            else if (typeName == "MaterialComponent")
            {
                if (FindJsonBoolValue(componentJson, "useMaterialFile", false))
                {
                    const std::string materialPath = FindJsonStringValue(componentJson, "materialPath");
                    if (!materialPath.empty() && !AddResolvedAsset(materialPath, CookedAssetKind::Material))
                        return false;
                }
            }
            else if (typeName == "ScriptComponent")
            {
                const std::string scriptPath = FindJsonStringValue(componentJson, "scriptPath");
                if (!scriptPath.empty() && !AddResolvedAsset(scriptPath, CookedAssetKind::Script))
                    return false;
            }
            else if (typeName == "SkyboxComponent")
            {
                const std::string cubemapPath = FindJsonStringValue(componentJson, "cubemapPath");
                if (!cubemapPath.empty() && !AddResolvedAsset(cubemapPath, CookedAssetKind::Cubemap))
                    return false;
            }
        }
    }

    return true;
}

bool AssetDependencyCooker::AddResolvedAsset(const std::string& path, CookedAssetKind kind, std::string* resolvedOut)
{
    if (path.empty())
        return true;

    const std::string resolved = ResolveAssetPath(path, kind);
    if (resolved.empty())
    {
        m_LastError = "Could not resolve asset dependency: " + path;
        return false;
    }

    if (resolvedOut)
        *resolvedOut = resolved;

    const bool inserted = m_CookedAssets.insert(resolved).second;
    auto kindIt = m_CookedAssetKinds.find(resolved);
    if (kindIt == m_CookedAssetKinds.end())
        m_CookedAssetKinds.emplace(resolved, kind);
    else
        kindIt->second = MergeAssetKind(kindIt->second, kind);

    if (!inserted)
        return true;

    switch (kind)
    {
        case CookedAssetKind::Scene:
            return CollectScene(resolved);
        case CookedAssetKind::Map:
            return CollectMap(resolved);
        case CookedAssetKind::Material:
            return CollectMaterial(resolved);
        case CookedAssetKind::Mesh:
            return CollectMeshSidecarDependencies(resolved);
        case CookedAssetKind::Prefab:
            return CollectPrefab(resolved);
        default:
            return true;
    }
}

std::string AssetDependencyCooker::ResolveAssetPath(const std::string& path, CookedAssetKind kind)
{
    std::filesystem::path input(path);
    std::error_code ec;

    auto kindPrefix = [kind]() -> std::filesystem::path
    {
        switch (kind)
        {
            case CookedAssetKind::Scene:
                return "Scenes";
            case CookedAssetKind::Map:
                return "Maps";
            case CookedAssetKind::Material:
                return "Materials";
            case CookedAssetKind::Mesh:
                return "Models";
            case CookedAssetKind::Script:
                return "Scripts";
            case CookedAssetKind::Cubemap:
                return "Cubemaps";
            case CookedAssetKind::Texture:
                return "Textures";
            case CookedAssetKind::Prefab:
                return "Prefabs";
            case CookedAssetKind::Generic:
            default:
                return {};
        }
    };

    if (input.is_absolute())
    {
        const std::filesystem::path relative = std::filesystem::relative(input, m_AssetsRoot, ec);
        if (!ec && !relative.empty() && !relative.generic_string().starts_with(".."))
            return NormalizeRelativeAssetPath(relative);
    }

    auto tryCandidate = [this](const std::filesystem::path& candidate) -> std::string
    {
        const std::filesystem::path full = m_AssetsRoot / candidate;
        if (std::filesystem::exists(full))
            return NormalizeRelativeAssetPath(candidate);
        return {};
    };

    auto tryFilenameFallback = [&](const std::filesystem::path& filenameOnly) -> std::string
    {
        if (filenameOnly.empty())
            return {};

        const std::filesystem::path prefix = kindPrefix();
        if (!prefix.empty())
        {
            const std::string directCandidate = tryCandidate(prefix / filenameOnly);
            if (!directCandidate.empty())
                return directCandidate;
        }

        std::error_code searchEc;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_AssetsRoot, searchEc))
        {
            if (searchEc)
                break;
            if (!entry.is_regular_file())
                continue;
            if (entry.path().filename() != filenameOnly)
                continue;

            const std::filesystem::path foundRelative = std::filesystem::relative(entry.path(), m_AssetsRoot, searchEc);
            if (!searchEc && !foundRelative.empty() && !foundRelative.generic_string().starts_with(".."))
                return NormalizeRelativeAssetPath(foundRelative);
        }
        return {};
    };

    if (input.is_absolute())
    {
        const std::string fallback = tryFilenameFallback(input.filename());
        if (!fallback.empty())
            return fallback;

        if (std::filesystem::exists(input))
        {
            std::filesystem::path stagedRelative = kindPrefix();
            if (!stagedRelative.empty())
                stagedRelative /= input.filename();
            else
                stagedRelative = input.filename();

            const std::string stagedPath = NormalizeRelativeAssetPath(stagedRelative);
            m_ExternalSourcePaths[stagedPath] = input;
            return stagedPath;
        }
    }

    const std::string genericCandidate = tryCandidate(input);
    if (!genericCandidate.empty())
        return genericCandidate;

    std::vector<std::filesystem::path> prefixes;
    const std::filesystem::path kindPrefixPath = kindPrefix();
    if (!kindPrefixPath.empty())
        prefixes.push_back(kindPrefixPath);

    for (const std::filesystem::path& prefix : prefixes)
    {
        const std::string candidate = tryCandidate(prefix / input);
        if (!candidate.empty())
            return candidate;
    }

    return {};
}

bool AssetDependencyCooker::StageSingleAsset(const std::filesystem::path& destinationAssetsRoot,
                                             const std::string& relativeAssetPath) const
{
    const std::filesystem::path source = GetAssetSourcePath(relativeAssetPath);
    const std::filesystem::path destination = destinationAssetsRoot / relativeAssetPath;
    std::error_code ec;

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
        return false;

    const auto kindIt = m_CookedAssetKinds.find(relativeAssetPath);
    if (kindIt != m_CookedAssetKinds.end() && kindIt->second == CookedAssetKind::Material)
        return StageMaterialAsset(source, destination, relativeAssetPath);

    if (std::filesystem::is_directory(source, ec) && !ec)
    {
        ec.clear();
        std::filesystem::create_directories(destination, ec);
        if (ec)
            return false;

        ec.clear();
        std::filesystem::copy(source, destination,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
        return !ec;
    }

    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

bool AssetDependencyCooker::StageMaterialAsset(const std::filesystem::path& source,
                                               const std::filesystem::path& destination,
                                               const std::string& /*relativeAssetPath*/) const
{
    std::ifstream inputFile(source);
    if (!inputFile.is_open())
        return false;

    std::ofstream outputFile(destination, std::ios::trunc);
    if (!outputFile.is_open())
        return false;

    const std::string prefix = "PROP Texture ";
    std::string line;
    while (std::getline(inputFile, line))
    {
        if (line.rfind(prefix, 0) == 0)
        {
            const std::string rawTexturePath = line.substr(prefix.size());
            AssetDependencyCooker resolver = *this;
            const std::string resolvedTexturePath =
                resolver.ResolveAssetPath(rawTexturePath, CookedAssetKind::Texture);
            if (!resolvedTexturePath.empty())
                line = prefix + resolvedTexturePath;
        }

        outputFile << line;
        if (!inputFile.eof())
            outputFile << '\n';
    }

    return outputFile.good();
}

std::filesystem::path AssetDependencyCooker::GetAssetSourcePath(const std::string& relativeAssetPath) const
{
    const auto externalIt = m_ExternalSourcePaths.find(relativeAssetPath);
    if (externalIt != m_ExternalSourcePaths.end())
        return externalIt->second;
    return m_AssetsRoot / relativeAssetPath;
}

std::string AssetDependencyCooker::FindJsonStringValue(const std::string& json, const char* key)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t keyPos = json.find(token);
    if (keyPos == std::string::npos)
        return {};

    const size_t colonPos = json.find(':', keyPos + token.size());
    if (colonPos == std::string::npos)
        return {};

    size_t valueStart = json.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos || json[valueStart] != '"')
        return {};

    const size_t valueEnd = json.find('"', valueStart + 1);
    if (valueEnd == std::string::npos)
        return {};
    return json.substr(valueStart + 1, valueEnd - valueStart - 1);
}

bool AssetDependencyCooker::FindJsonBoolValue(const std::string& json, const char* key, bool defaultValue)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t keyPos = json.find(token);
    if (keyPos == std::string::npos)
        return defaultValue;

    const size_t colonPos = json.find(':', keyPos + token.size());
    if (colonPos == std::string::npos)
        return defaultValue;

    size_t valueStart = json.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos)
        return defaultValue;

    if (json.compare(valueStart, 4, "true") == 0)
        return true;
    if (json.compare(valueStart, 5, "false") == 0)
        return false;
    return defaultValue;
}

} // namespace Dot
