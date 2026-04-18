// =============================================================================
// Dot Engine - Asset Manager Panel
// =============================================================================
// File browser for managing project assets with thumbnail previews.
// =============================================================================

#pragma once

#include "EditorPanel.h"

#include <filesystem>
#include <functional>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace Dot
{

class RHIDevice;

/// Represents a cached asset entry
struct AssetEntry
{
    std::filesystem::path path;
    std::string name;
    std::string extension;
    bool isDirectory = false;
    uintmax_t fileSize = 0;

    // Thumbnail (for textures)
    void* thumbnailSRV = nullptr; // D3D12 shader resource view
    bool thumbnailLoaded = false;
    bool thumbnailFailed = false;
};

/// Asset type classification
enum class AssetType
{
    Unknown,
    Folder,
    Scene,
    Map,
    UI,
    Texture,
    Model,
    Material,
    Shader,
    Audio
};

/// Get asset type from extension
inline AssetType GetAssetType(const std::string& ext)
{
    if (ext == ".dotscene")
        return AssetType::Scene;
    if (ext == ".dotmap")
        return AssetType::Map;
    if (ext == ".dotui")
        return AssetType::UI;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
        return AssetType::Texture;
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
        return AssetType::Model;
    if (ext == ".dotmat")
        return AssetType::Material;
    if (ext == ".hlsl" || ext == ".glsl")
        return AssetType::Shader;
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
        return AssetType::Audio;
    return AssetType::Unknown;
}

/// Get icon for asset type (FontAwesome)
inline const char* GetAssetIcon(AssetType type)
{
    switch (type)
    {
        case AssetType::Folder:
            return "\xef\x81\xbb"; // folder
        case AssetType::Scene:
            return "\xef\x80\x88"; // film
        case AssetType::Texture:
            return "\xef\x87\x85"; // image
        case AssetType::Map:
            return "\xef\x89\xb9"; // map
        case AssetType::UI:
            return "UI";
        case AssetType::Model:
            return "\xef\x86\xb2"; // cube
        case AssetType::Material:
            return "\xef\x95\xbf"; // palette
        case AssetType::Shader:
            return "\xef\x84\xa1"; // code
        case AssetType::Audio:
            return "\xef\x80\x81"; // music
        default:
            return "\xef\x85\x9b"; // file
    }
}

/// View mode for asset display
enum class AssetViewMode
{
    Grid,
    List
};

/// Asset Manager Panel
class AssetManagerPanel : public EditorPanel
{
public:
    AssetManagerPanel();
    ~AssetManagerPanel() override;

    void OnImGui() override;

    /// Set the RHI device for texture loading
    void SetDevice(RHIDevice* device) { m_Device = device; }

    /// Set root assets directory
    void SetRootPath(const std::filesystem::path& root);

    /// Navigate to a specific path
    void NavigateTo(const std::filesystem::path& path);

    /// Refresh current directory listing
    void Refresh();

    /// Callback type for opening text files
    using OpenTextFileCallback = std::function<void(const std::filesystem::path&)>;

    /// Callback type for opening prefab files
    using OpenPrefabFileCallback = std::function<void(const std::filesystem::path&)>;

    /// Callback type for opening texture files
    using OpenTextureFileCallback = std::function<void(const std::filesystem::path&)>;

    /// Callback type for opening scene files
    using OpenSceneFileCallback = std::function<void(const std::filesystem::path&)>;
    using OpenMapFileCallback = std::function<void(const std::filesystem::path&)>;
    using OpenUiFileCallback = std::function<void(const std::filesystem::path&)>;

    /// Set callback for opening text files
    void SetOnOpenTextFile(OpenTextFileCallback callback) { m_OnOpenTextFile = std::move(callback); }

    /// Set callback for opening prefab files
    void SetOnOpenPrefabFile(OpenPrefabFileCallback callback) { m_OnOpenPrefabFile = std::move(callback); }

    /// Set callback for opening texture files
    void SetOnOpenTextureFile(OpenTextureFileCallback callback) { m_OnOpenTextureFile = std::move(callback); }

    /// Set callback for opening scene files
    void SetOnOpenSceneFile(OpenSceneFileCallback callback) { m_OnOpenSceneFile = std::move(callback); }
    void SetOnOpenMapFile(OpenMapFileCallback callback) { m_OnOpenMapFile = std::move(callback); }
    void SetOnOpenUiFile(OpenUiFileCallback callback) { m_OnOpenUiFile = std::move(callback); }

private:
    void DrawToolbar();
    void DrawBreadcrumbs();
    void DrawAssetGrid();
    void DrawAssetList();
    void DrawAssetItem(AssetEntry& entry, float size);
    void DrawContextMenu();
    void DrawStatusBar();

    void LoadThumbnail(AssetEntry& entry);
    void CreateFolder(const std::string& name);
    void DeleteAsset(const AssetEntry& entry);
    void RenameAsset(AssetEntry& entry, const std::string& newName);
    void OpenAssetEntry(const AssetEntry& entry);

    // State
    std::filesystem::path m_RootPath;
    std::filesystem::path m_CurrentPath;
    std::vector<AssetEntry> m_Entries;
    int m_SelectedIndex = -1;
    AssetViewMode m_ViewMode = AssetViewMode::Grid;

    // Grid settings
    float m_ThumbnailSize = 80.0f;
    float m_Padding = 8.0f;

    // Filter & Sort
    ImGuiTextFilter m_Filter;
    enum class SortOption
    {
        Name,
        Size,
        Type,
        Date
    };
    SortOption m_SortOption = SortOption::Type; // Default to grouping directories (Type)
    bool m_SortAscending = true;

    // Rename state
    bool m_IsRenaming = false;
    int m_RenamingIndex = -1;
    char m_RenameBuffer[256] = {};

    // Create folder dialog
    bool m_ShowCreateFolder = false;
    char m_NewFolderName[256] = {};

    // Thumbnail cache
    std::unordered_map<std::string, void*> m_ThumbnailCache;
    RHIDevice* m_Device = nullptr;

    // Callbacks
    OpenTextFileCallback m_OnOpenTextFile;
    OpenPrefabFileCallback m_OnOpenPrefabFile;
    OpenTextureFileCallback m_OnOpenTextureFile;
    OpenSceneFileCallback m_OnOpenSceneFile;
    OpenMapFileCallback m_OnOpenMapFile;
    OpenUiFileCallback m_OnOpenUiFile;
};

} // namespace Dot
