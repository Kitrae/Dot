// =============================================================================
// Dot Engine - Asset Manager Implementation
// =============================================================================

#include "Core/Assets/AssetManager.h"

#include "Core/Jobs/JobSystem.h"
#include "Core/Log.h"
#include "Core/Material/MaterialTextureUtils.h"

#include "RHI/RHIDevice.h"
#include "RHI/RHITexture.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stb/stb_image.h>

namespace Dot
{

// =============================================================================
// Job Data Structs
// =============================================================================

struct TextureLoadJobData
{
    std::string path;
    TextureSemantic semantic = TextureSemantic::Color;
    TextureAssetPtr asset;
};

struct ScriptLoadJobData
{
    std::string path;
    ScriptAssetPtr asset;
};

// =============================================================================
// AssetManager implementation
// =============================================================================

AssetManager& AssetManager::Get()
{
    static AssetManager instance;
    return instance;
}

bool AssetManager::Initialize()
{
    DOT_LOG_INFO("AssetManager: Initializing...");
    return true;
}

void AssetManager::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_CacheMutex);
    m_AssetCache.clear();
    DOT_LOG_INFO("AssetManager: Shutdown");
}

AssetHandle<TextureAsset> AssetManager::LoadTexture(const std::string& path, TextureSemantic semantic)
{
    DOT_LOG_INFO("AssetManager: Requesting texture load: %s", path.c_str());
    std::lock_guard<std::mutex> lock(m_CacheMutex);

    if (semantic == TextureSemantic::Auto)
        semantic = GuessTextureSemanticFromPath(path);

    const std::string cacheKey = BuildTextureSemanticCacheKey(path, semantic);

    auto it = m_AssetCache.find(cacheKey);
    if (it != m_AssetCache.end())
    {
        return AssetHandle<TextureAsset>(std::static_pointer_cast<TextureAsset>(it->second));
    }

    auto asset = std::make_shared<TextureAsset>(path);
    asset->SetSemantic(semantic);
    asset->SetState(AssetState::Loading);
    m_AssetCache[cacheKey] = asset;

    auto* jobData = new TextureLoadJobData();
    jobData->path = path;
    jobData->semantic = semantic;
    jobData->asset = asset;

    JobSystem::Get().Schedule(Job::Create(LoadTextureJob, jobData));

    return AssetHandle<TextureAsset>(asset);
}

AssetHandle<ScriptAsset> AssetManager::LoadScript(const std::string& path)
{
    std::lock_guard<std::mutex> lock(m_CacheMutex);

    auto it = m_AssetCache.find(path);
    if (it != m_AssetCache.end())
    {
        return AssetHandle<ScriptAsset>(std::static_pointer_cast<ScriptAsset>(it->second));
    }

    auto asset = std::make_shared<ScriptAsset>(path);
    asset->SetState(AssetState::Loading);
    m_AssetCache[path] = asset;

    auto* jobData = new ScriptLoadJobData();
    jobData->path = path;
    jobData->asset = asset;

    JobSystem::Get().Schedule(Job::Create(LoadScriptJob, jobData));

    return AssetHandle<ScriptAsset>(asset);
}

void AssetManager::Wait(AssetPtr asset)
{
    if (!asset)
        return;

    while (asset->GetState() == AssetState::Loading)
    {
        std::this_thread::yield();
    }
}

std::string AssetManager::GetFullPath(const std::string& assetPath) const
{
    if (std::filesystem::path(assetPath).is_absolute())
    {
        return assetPath;
    }

    if (m_RootPath.empty())
    {
        return assetPath;
    }

    return (std::filesystem::path(m_RootPath) / assetPath).string();
}

void AssetManager::GarbageCollect()
{
    std::lock_guard<std::mutex> lock(m_CacheMutex);

    for (auto it = m_AssetCache.begin(); it != m_AssetCache.end();)
    {
        if (it->second.use_count() == 1)
        {
            it = m_AssetCache.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void AssetManager::LoadTextureJob(void* data)
{
    auto* jobData = static_cast<TextureLoadJobData*>(data);
    auto& manager = AssetManager::Get();

    std::string fullPath = manager.GetFullPath(jobData->path);
    const TextureSemantic semantic = jobData->semantic == TextureSemantic::Auto ? GuessTextureSemanticFromPath(jobData->path)
                                                                                : jobData->semantic;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* bytePixels = nullptr;
    float* floatPixels = nullptr;

    stbi_set_flip_vertically_on_load(false);
    if (TextureSemanticIsHdr(semantic))
        floatPixels = stbi_loadf(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    else
        bytePixels = stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!bytePixels && !floatPixels)
    {
        DOT_LOG_ERROR("AssetManager: Failed to load texture at %s", jobData->path.c_str());
        jobData->asset->SetState(AssetState::Failed);
        delete jobData;
        return;
    }

    // Actual GPU upload
    if (manager.m_Device)
    {
        std::lock_guard<std::mutex> uploadLock(manager.m_TextureUploadMutex);

        RHITextureDesc desc;
        desc.width = static_cast<uint32>(width);
        desc.height = static_cast<uint32>(height);
        desc.format =
            TextureSemanticIsHdr(semantic)
                ? RHIFormat::R32G32B32A32_FLOAT
                : (TextureSemanticUsesSRGB(semantic) ? RHIFormat::R8G8B8A8_SRGB : RHIFormat::R8G8B8A8_UNORM);
        desc.usage = RHITextureUsage::Sampled;

        RHITexturePtr texture = manager.m_Device->CreateTexture(desc);
        if (texture)
        {
            manager.m_Device->UpdateTexture(texture, TextureSemanticIsHdr(semantic) ? static_cast<const void*>(floatPixels)
                                                                                   : static_cast<const void*>(bytePixels));
            jobData->asset->SetTexture(texture);
            jobData->asset->SetFormat(desc.format);
            jobData->asset->SetDimensions(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            jobData->asset->SetSemantic(semantic);
            jobData->asset->SetState(AssetState::Ready);
            DOT_LOG_INFO("AssetManager: Loaded and uploaded texture %s (%dx%d, %s)", jobData->path.c_str(), width, height,
                         GetTextureSemanticName(semantic));
        }
        else
        {
            DOT_LOG_ERROR("AssetManager: Failed to create RHITexture for %s", jobData->path.c_str());
            jobData->asset->SetState(AssetState::Failed);
        }
    }
    else
    {
        DOT_LOG_ERROR("AssetManager: No RHI device set for texture upload!");
        jobData->asset->SetState(AssetState::Failed);
    }

    if (bytePixels)
        stbi_image_free(bytePixels);
    if (floatPixels)
        stbi_image_free(floatPixels);
    delete jobData;
}

void AssetManager::LoadScriptJob(void* data)
{
    auto* jobData = static_cast<ScriptLoadJobData*>(data);
    auto& manager = AssetManager::Get();

    std::string fullPath = manager.GetFullPath(jobData->path);

    std::ifstream file(fullPath);
    if (!file.is_open())
    {
        DOT_LOG_ERROR("AssetManager: Failed to open script at %s", jobData->path.c_str());
        jobData->asset->SetState(AssetState::Failed);
        delete jobData;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    jobData->asset->SetSource(buffer.str());

    DOT_LOG_INFO("AssetManager: Loaded script file %s", jobData->path.c_str());

    jobData->asset->SetState(AssetState::Ready);
    delete jobData;
}

} // namespace Dot
