// =============================================================================
// Dot Engine - Material Preview Renderer
// =============================================================================
// Renders a sphere with material preview to an offscreen texture for ImGui.
// =============================================================================

#pragma once

#include "Camera.h"
#include "Core/Assets/Asset.h"
#include "Core/Assets/TextureAsset.h"
#include "Core/Material/MaterialTextureUtils.h"
#include "RHI/RHI.h"

#include <array>
#include <string>
#include <vector>

namespace Dot
{

class D3D12Device;
class RHIGUI;

/// Renders material preview to an offscreen texture for display in ImGui
class MaterialPreviewRenderer
{
public:
    MaterialPreviewRenderer();
    ~MaterialPreviewRenderer();

    /// Initialize with device and GUI interface
    /// @param size Preview texture resolution (square)
    bool Initialize(RHIDevice* device, RHIGUI* gui, uint32_t size = 256);

    /// Clean up resources
    void Shutdown();

    /// Set albedo texture path, UV transform, and sampling options (empty string clears texture)
    /// @param filterMode 0=Nearest, 1=Bilinear, 2=Trilinear
    /// @param wrapMode 0=Repeat, 1=Clamp, 2=Mirror
    /// @param pannerSpeedU/V UV animation speed (set to 0 for no animation)
    void SetAlbedoTexture(const std::string& path, float tilingU = 1.0f, float tilingV = 1.0f, float offsetU = 0.0f,
                          float offsetV = 0.0f, int filterMode = 1, int wrapMode = 0, float pannerSpeedU = 0.0f,
                          float pannerSpeedV = 0.0f, int channel = -1);
    void SetTextureSlot(int slot, const std::string& path, int filterMode = 1, int wrapMode = 0,
                        int sampleType = static_cast<int>(TextureSampleType::Color));
    void ClearTextureSlots();

    /// Update custom material logic from HLSL (injected into template)
    void UpdateCustomMaterial(const std::string& surfaceHLSL);

    /// Render the preview sphere with current material properties
    /// @param albedoR/G/B Base color (0-1), used if no texture is set
    /// @param metallic Metallic (0-1)
    /// @param roughness Roughness (0-1)
    /// @param rotation Horizontal rotation in degrees
    void Render(float albedoR, float albedoG, float albedoB, float metallic, float roughness, float rotation = 0.0f);

    /// Get ImGui texture ID for display with ImGui::Image()
    void* GetTextureId() const { return m_ImGuiTexId; }

    /// Get preview size
    uint32_t GetSize() const { return m_Size; }

    /// Check if initialized
    bool IsInitialized() const { return m_Initialized; }

private:
    bool CreateRenderTarget();
    bool CreateShaders();
    bool CreatePipelineState();
    bool CreateSphereMesh();
    bool CreateConstantBuffer();
    bool LoadTextureSlot(int slot, const std::string& path, TextureSemantic semantic);
    void ReleaseTextureSlot(int slot);
    void RefreshSamplerDescriptors();
    void RefreshHasTextureFlag();

    RHIDevice* m_Device = nullptr;
    D3D12Device* m_D3D12Device = nullptr;
    RHIGUI* m_GUI = nullptr;

    // Render target
    RHITexturePtr m_RenderTarget;
    void* m_RTVHeap = nullptr;     // ID3D12DescriptorHeap*
    void* m_DSVHeap = nullptr;     // ID3D12DescriptorHeap*
    void* m_DepthBuffer = nullptr; // ID3D12Resource* for depth
    void* m_ImGuiTexId = nullptr;  // Registered texture for ImGui

    // Pipeline
    void* m_RootSignature = nullptr;
    void* m_PipelineState = nullptr;

    // Sphere mesh
    RHIBufferPtr m_VertexBuffer;
    RHIBufferPtr m_IndexBuffer;
    uint32_t m_IndexCount = 0;

    // Constant buffer
    void* m_CBVHeap = nullptr;
    void* m_ConstantBuffer = nullptr;
    void* m_MappedCB = nullptr;

    struct PreviewTextureSlot
    {
        AssetHandle<TextureAsset> handle;
        std::string path;
        int filterMode = 1;
        int wrapMode = 0;
        int sampleType = static_cast<int>(TextureSampleType::Color);
    };

    void* m_SRVHeap = nullptr; // ID3D12DescriptorHeap* for SRVs
    std::array<PreviewTextureSlot, 4> m_TextureSlots;
    bool m_HasTexture = false;

    // UV transform
    float m_TilingU = 1.0f;
    float m_TilingV = 1.0f;
    float m_OffsetU = 0.0f;
    float m_OffsetV = 0.0f;

    // Sampling options (0=Nearest/Repeat, 1=Bilinear/Clamp, 2=Trilinear/Mirror)
    int m_FilterMode = 1; // Default Bilinear
    int m_WrapMode = 0;   // Default Repeat

    // Panner animation
    float m_PannerSpeedU = 0.0f;
    float m_PannerSpeedV = 0.0f;
    int m_Channel = -1;         // -1=RGB, 0=R, 1=G, 2=B
    float m_ElapsedTime = 0.0f; // Accumulated time for animation

    // Pre-created samplers for different modes
    void* m_SamplerHeap = nullptr; // ID3D12DescriptorHeap* with multiple samplers

    // Shader bytecode
    std::vector<uint8_t> m_VSBytecode;
    std::vector<uint8_t> m_PSBytecode;

    uint32_t m_Size = 256;
    bool m_Initialized = false;
    bool m_FirstRender = true; // Track initial resource state
};

} // namespace Dot
