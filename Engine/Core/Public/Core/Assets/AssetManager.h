// =============================================================================
// Dot Engine - Asset Manager
// =============================================================================

#pragma once

#include "Core/Assets/Asset.h"
#include "Core/Assets/ScriptAsset.h"
#include "Core/Assets/TextureAsset.h"
#include "Core/Core.h"
#include "Core/Material/MaterialTextureUtils.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace Dot
{

class AssetManager
{
public:
    static AssetManager& Get();

    bool Initialize();
    void Shutdown();

    void SetDevice(class RHIDevice* device) { m_Device = device; }

    /// Set the root path for asset resolution
    void SetRootPath(const std::string& path) { m_RootPath = path; }

    /// Get the root path
    const std::string& GetRootPath() const { return m_RootPath; }

    /// Resolve an asset path to a full absolute path
    std::string GetFullPath(const std::string& assetPath) const;

    /// Get or load a texture asset asynchronously
    AssetHandle<TextureAsset> LoadTexture(const std::string& path,
                                          TextureSemantic semantic = TextureSemantic::Auto);

    /// Get or load a script asset asynchronously
    AssetHandle<ScriptAsset> LoadScript(const std::string& path);

    /// Synchronously wait for an asset to load
    void Wait(AssetPtr asset);

    /// Clean up unused assets
    void GarbageCollect();

private:
    AssetManager() = default;
    ~AssetManager() = default;

    // Internal jobs
    static void LoadTextureJob(void* data);
    static void LoadScriptJob(void* data);

    std::unordered_map<std::string, AssetPtr> m_AssetCache;
    std::mutex m_CacheMutex;
    std::mutex m_TextureUploadMutex;
    class RHIDevice* m_Device = nullptr;
    std::string m_RootPath;
};

} // namespace Dot
