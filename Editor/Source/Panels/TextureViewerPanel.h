// =============================================================================
// Dot Engine - Texture Viewer Panel
// =============================================================================
// View texture files with channel isolation, zoom, and info display.
// =============================================================================

#pragma once

#include "EditorPanel.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Dot
{

class RHIDevice;
class RHIGUI;
class RHITexture;

/// Texture viewer panel - displays texture files with tools
class TextureViewerPanel : public EditorPanel
{
public:
    TextureViewerPanel();
    ~TextureViewerPanel() override;

    void OnImGui() override;

    /// Set the RHI device for texture creation
    void SetDevice(RHIDevice* device) { m_Device = device; }

    /// Set the GUI interface for texture registration
    void SetGUI(RHIGUI* gui) { m_GUI = gui; }

    /// Open a texture file for viewing
    void OpenTexture(const std::filesystem::path& path);
    void OpenRuntimeTexture(const std::string& displayName, std::shared_ptr<RHITexture> texture, const std::string& sourceLabel);

    /// Close the current texture
    void CloseTexture();

    /// Check if a texture is currently open
    bool HasOpenTexture() const { return m_Texture != nullptr && m_TextureSRV != nullptr; }

    /// Get the currently open file path
    const std::filesystem::path& GetFilePath() const { return m_FilePath; }

private:
    void LoadTextureFromFile();
    bool RebuildRuntimePreview();
    void DrawToolbar();
    void DrawTextureView();
    void DrawInfoPanel();

    // RHI
    RHIDevice* m_Device = nullptr;
    RHIGUI* m_GUI = nullptr;

    // File state
    std::filesystem::path m_FilePath;
    std::string m_DisplayName;
    std::string m_SourceLabel;
    std::string m_LoadError;
    bool m_IsRuntimeTexture = false;

    // Texture data
    std::shared_ptr<RHITexture> m_Texture;
    std::shared_ptr<RHITexture> m_RuntimeSourceTexture;
    void* m_TextureSRV = nullptr;        // D3D12 shader resource view for ImGui
    std::vector<uint8_t> m_OriginalData; // Keeping a copy for channel filtering
    int m_Width = 0;
    int m_Height = 0;
    int m_Channels = 0;
    size_t m_FileSize = 0;
    uint32_t m_RuntimeSelectedMip = 0;
    uint32_t m_RuntimeMipLevels = 1;
    bool m_IsRenderGraphTexture = false;

    void ApplyChannelFilter(); // Generates filtered data and updates texture

    // View settings
    float m_Zoom = 1.0f;
    float m_PanX = 0.0f;
    float m_PanY = 0.0f;
    bool m_ShowR = true;
    bool m_ShowG = true;
    bool m_ShowB = true;
    bool m_ShowA = false; // Alpha shown as grayscale when enabled
    bool m_ShowCheckerboard = true;
    bool m_ShowInfo = true;

    // Interaction state
    bool m_IsPanning = false;
    float m_LastMouseX = 0.0f;
    float m_LastMouseY = 0.0f;
};

} // namespace Dot
