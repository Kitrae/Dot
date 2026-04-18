#pragma once

#include <Core/Core.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Dot
{

enum class CookedAssetKind
{
    Generic,
    Scene,
    Map,
    Material,
    Mesh,
    Script,
    Cubemap,
    Texture,
    Prefab,
};

struct CookedAssetRecord
{
    std::string path;
    CookedAssetKind kind = CookedAssetKind::Generic;
    uint64 sourceBytes = 0;
};

class AssetDependencyCooker
{
public:
    bool Collect(const std::filesystem::path& assetsRoot, const std::string& startupScene);
    bool StageTo(const std::filesystem::path& destinationAssetsRoot) const;
    bool WriteManifest(const std::filesystem::path& path, const std::string& startupScene) const;

    const std::unordered_set<std::string>& GetCookedAssets() const { return m_CookedAssets; }
    std::vector<CookedAssetRecord> GetCookedAssetRecords() const;
    uint64 GetTotalSourceBytes() const;
    const std::string& GetLastError() const { return m_LastError; }

private:
    bool CollectScene(const std::string& scenePath);
    bool CollectSceneSettingsSidecar(const std::string& scenePath);
    bool CollectMap(const std::string& mapPath);
    bool CollectMaterial(const std::string& materialPath);
    bool CollectMeshSidecarDependencies(const std::string& meshPath);
    bool CollectPrefab(const std::string& prefabPath);
    bool AddResolvedAsset(const std::string& path, CookedAssetKind kind, std::string* resolvedOut = nullptr);
    std::string ResolveAssetPath(const std::string& path, CookedAssetKind kind);
    bool StageSingleAsset(const std::filesystem::path& destinationAssetsRoot, const std::string& relativeAssetPath) const;
    bool StageMaterialAsset(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            const std::string& relativeAssetPath) const;
    std::filesystem::path GetAssetSourcePath(const std::string& relativeAssetPath) const;
    static std::string FindJsonStringValue(const std::string& json, const char* key);
    static bool FindJsonBoolValue(const std::string& json, const char* key, bool defaultValue);

    std::filesystem::path m_AssetsRoot;
    std::unordered_set<std::string> m_CookedAssets;
    std::unordered_map<std::string, CookedAssetKind> m_CookedAssetKinds;
    std::unordered_map<std::string, std::filesystem::path> m_ExternalSourcePaths;
    std::string m_LastError;
};

} // namespace Dot
