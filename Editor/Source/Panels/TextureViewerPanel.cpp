// =============================================================================
// Dot Engine - Texture Viewer Panel Implementation
// =============================================================================

#include "TextureViewerPanel.h"
#include "PanelChrome.h"

#include "RHI/RHIDevice.h"
#include "RHI/RHIGUI.h"
#include "RHI/RHITexture.h"
#ifdef DOT_PLATFORM_WINDOWS
    #include "../../../Engine/RHI/Private/D3D12/D3D12Device.h"
    #include "../../../Engine/RHI/Private/D3D12/D3D12Texture.h"
#endif

#include <imgui.h>

// stb_image for loading textures (implementation is in CubemapLoader.cpp)
#include "../../../ThirdParty/stb/stb_image.h"

#include <algorithm>
#include <cstring>

namespace Dot
{

namespace
{

size_t EstimateTextureBytes(RHIFormat format, int width, int height)
{
    size_t bytesPerPixel = 4;
    switch (format)
    {
        case RHIFormat::R8_UNORM:
            bytesPerPixel = 1;
            break;
        case RHIFormat::R8G8_UNORM:
        case RHIFormat::R16_FLOAT:
        case RHIFormat::R16_UINT:
        case RHIFormat::D16_UNORM:
            bytesPerPixel = 2;
            break;
        case RHIFormat::R16G16B16A16_FLOAT:
        case RHIFormat::R32G32_FLOAT:
        case RHIFormat::D32_FLOAT_S8_UINT:
            bytesPerPixel = 8;
            break;
        case RHIFormat::R32G32B32_FLOAT:
            bytesPerPixel = 12;
            break;
        case RHIFormat::R32G32B32A32_FLOAT:
            bytesPerPixel = 16;
            break;
        default:
            bytesPerPixel = 4;
            break;
    }
    return static_cast<size_t>((std::max)(1, width)) * static_cast<size_t>((std::max)(1, height)) * bytesPerPixel;
}

int InferPreviewChannelCount(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R8_UNORM:
        case RHIFormat::R16_FLOAT:
        case RHIFormat::R16_UINT:
        case RHIFormat::R32_FLOAT:
        case RHIFormat::D16_UNORM:
        case RHIFormat::D32_FLOAT:
            return 1;
        case RHIFormat::R8G8_UNORM:
            return 2;
        case RHIFormat::R8G8B8A8_UNORM:
        case RHIFormat::R8G8B8A8_SRGB:
        case RHIFormat::B8G8R8A8_UNORM:
        case RHIFormat::B8G8R8A8_SRGB:
        default:
            return 4;
    }
}

D3D12_RESOURCE_STATES ToPreviewCopyState(RHIResourceState state)
{
    switch (state)
    {
        case RHIResourceState::RenderTarget:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHIResourceState::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHIResourceState::DepthWrite:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHIResourceState::DepthRead:
            return D3D12_RESOURCE_STATE_DEPTH_READ;
        case RHIResourceState::ShaderResource:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RHIResourceState::CopySource:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHIResourceState::CopyDest:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHIResourceState::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
        case RHIResourceState::Common:
        case RHIResourceState::Unknown:
        default:
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

#ifdef DOT_PLATFORM_WINDOWS
bool ReadRuntimeTexturePreviewRGBA8(RHIDevice* device, RHITexture* texture, uint32 mipLevel, std::vector<uint8_t>& outData,
                                    int& outWidth, int& outHeight)
{
    if (!device || !texture)
        return false;

    auto* d3dDevice = static_cast<D3D12Device*>(device);
    auto* d3dTexture = static_cast<D3D12Texture*>(texture);
    ID3D12Device* nativeDevice = d3dDevice ? d3dDevice->GetDevice() : nullptr;
    ID3D12CommandQueue* queue = d3dDevice ? d3dDevice->GetCommandQueue() : nullptr;
    ID3D12Resource* source = static_cast<ID3D12Resource*>(device->GetNativeTextureResource(texture));
    if (!nativeDevice || !queue || !source || !d3dTexture)
        return false;

    const uint32 mipCount = (std::max)(1u, texture->GetMipLevels());
    mipLevel = (std::min)(mipLevel, mipCount - 1u);
    outWidth = static_cast<int>((std::max)(1u, texture->GetWidth() >> mipLevel));
    outHeight = static_cast<int>((std::max)(1u, texture->GetHeight() >> mipLevel));
    if (outWidth <= 0 || outHeight <= 0)
        return false;

    D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    nativeDevice->GetCopyableFootprints(&sourceDesc, mipLevel, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes);
    if (totalBytes == 0)
        return false;

    ComPtr<ID3D12Resource> readbackBuffer;
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(nativeDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&readbackBuffer))))
    {
        return false;
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(nativeDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(nativeDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                               IID_PPV_ARGS(&commandList))))
    {
        return false;
    }

    const D3D12_RESOURCE_STATES currentState = ToPreviewCopyState(d3dTexture->GetCurrentState());
    if (currentState != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        D3D12_RESOURCE_BARRIER toCopy = {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = source;
        toCopy.Transition.Subresource = 0;
        toCopy.Transition.StateBefore = currentState;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList->ResourceBarrier(1, &toCopy);
    }

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = source;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = mipLevel;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readbackBuffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    if (currentState != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        D3D12_RESOURCE_BARRIER backToOriginal = {};
        backToOriginal.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        backToOriginal.Transition.pResource = source;
        backToOriginal.Transition.Subresource = 0;
        backToOriginal.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        backToOriginal.Transition.StateAfter = currentState;
        commandList->ResourceBarrier(1, &backToOriginal);
    }

    if (FAILED(commandList->Close()))
        return false;

    ID3D12CommandList* lists[] = {commandList.Get()};
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    if (FAILED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        return false;

    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle)
        return false;

    const UINT64 fenceValue = 1;
    queue->Signal(fence.Get(), fenceValue);
    if (fence->GetCompletedValue() < fenceValue)
    {
        fence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
    }
    CloseHandle(eventHandle);

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, static_cast<SIZE_T>(totalBytes)};
    if (FAILED(readbackBuffer->Map(0, &readRange, &mapped)) || !mapped)
        return false;

    outData.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4);
    const uint8_t* srcBytes = static_cast<const uint8_t*>(mapped);
    uint8_t* dstBytes = outData.data();

    auto clampByte = [](float value)
    {
        value = (std::max)(0.0f, (std::min)(1.0f, value));
        return static_cast<uint8_t>(value * 255.0f + 0.5f);
    };

    const bool scalarPreview = texture->GetFormat() == RHIFormat::R8_UNORM || texture->GetFormat() == RHIFormat::D16_UNORM ||
                               texture->GetFormat() == RHIFormat::R32_FLOAT || texture->GetFormat() == RHIFormat::D32_FLOAT;
    float scalarMin = 1.0f;
    float scalarMax = 0.0f;
    if (scalarPreview)
    {
        for (int y = 0; y < outHeight; ++y)
        {
            const uint8_t* row = srcBytes + y * footprint.Footprint.RowPitch;
            for (int x = 0; x < outWidth; ++x)
            {
                float value = 0.0f;
                switch (texture->GetFormat())
                {
                    case RHIFormat::R8_UNORM:
                        value = static_cast<float>(row[x]) / 255.0f;
                        break;
                    case RHIFormat::D16_UNORM:
                        value = 1.0f - (static_cast<float>(reinterpret_cast<const uint16_t*>(row)[x]) / 65535.0f);
                        break;
                    case RHIFormat::R32_FLOAT:
                        value = reinterpret_cast<const float*>(row)[x];
                        break;
                    case RHIFormat::D32_FLOAT:
                        value = 1.0f - reinterpret_cast<const float*>(row)[x];
                        break;
                    default:
                        break;
                }
                scalarMin = (std::min)(scalarMin, value);
                scalarMax = (std::max)(scalarMax, value);
            }
        }
        if ((scalarMax - scalarMin) < 1e-5f)
        {
            scalarMin = 0.0f;
            scalarMax = 1.0f;
        }
    }

    for (int y = 0; y < outHeight; ++y)
    {
        const uint8_t* row = srcBytes + y * footprint.Footprint.RowPitch;
        for (int x = 0; x < outWidth; ++x)
        {
            uint8_t* pixel = dstBytes + (static_cast<size_t>(y) * outWidth + x) * 4;
            switch (texture->GetFormat())
            {
                case RHIFormat::R8_UNORM:
                {
                    const float value = static_cast<float>(row[x]) / 255.0f;
                    const uint8_t v = clampByte((value - scalarMin) / (scalarMax - scalarMin));
                    pixel[0] = v;
                    pixel[1] = v;
                    pixel[2] = v;
                    pixel[3] = 255;
                    break;
                }
                case RHIFormat::D16_UNORM:
                {
                    const uint16_t v = reinterpret_cast<const uint16_t*>(row)[x];
                    const float value = 1.0f - (static_cast<float>(v) / 65535.0f);
                    const uint8_t g = clampByte((value - scalarMin) / (scalarMax - scalarMin));
                    pixel[0] = g;
                    pixel[1] = g;
                    pixel[2] = g;
                    pixel[3] = 255;
                    break;
                }
                case RHIFormat::R32_FLOAT:
                case RHIFormat::D32_FLOAT:
                {
                    const float v = reinterpret_cast<const float*>(row)[x];
                    const bool isDepth = texture->GetFormat() == RHIFormat::D32_FLOAT;
                    const float value = isDepth ? (1.0f - v) : v;
                    const uint8_t g = clampByte((value - scalarMin) / (scalarMax - scalarMin));
                    pixel[0] = g;
                    pixel[1] = g;
                    pixel[2] = g;
                    pixel[3] = 255;
                    break;
                }
                case RHIFormat::B8G8R8A8_UNORM:
                case RHIFormat::B8G8R8A8_SRGB:
                {
                    const uint8_t* srcPixel = row + x * 4;
                    pixel[0] = srcPixel[2];
                    pixel[1] = srcPixel[1];
                    pixel[2] = srcPixel[0];
                    pixel[3] = srcPixel[3];
                    break;
                }
                case RHIFormat::R8G8B8A8_UNORM:
                case RHIFormat::R8G8B8A8_SRGB:
                default:
                {
                    const uint8_t* srcPixel = row + x * 4;
                    pixel[0] = srcPixel[0];
                    pixel[1] = srcPixel[1];
                    pixel[2] = srcPixel[2];
                    pixel[3] = srcPixel[3];
                    break;
                }
            }
        }
    }

    readbackBuffer->Unmap(0, nullptr);
    return true;
}
#endif

} // namespace

// Icons
#define ICON_IMAGE "\xef\x87\x85"        // fa-image
#define ICON_SEARCH_PLUS "\xef\x80\x8e"  // fa-search-plus
#define ICON_SEARCH_MINUS "\xef\x80\x90" // fa-search-minus
#define ICON_EXPAND "\xef\x81\xa5"       // fa-expand
#define ICON_INFO "\xef\x81\x9a"         // fa-info-circle

TextureViewerPanel::TextureViewerPanel() : EditorPanel("Texture Viewer")
{
    m_Open = false;
}

TextureViewerPanel::~TextureViewerPanel()
{
    CloseTexture();
}

void TextureViewerPanel::OpenTexture(const std::filesystem::path& path)
{
    // Close previous texture if any
    CloseTexture();

    m_FilePath = path;
    m_DisplayName = path.filename().string();
    m_SourceLabel = "Asset File";
    m_LoadError.clear();
    m_IsRuntimeTexture = false;

    // Get file size
    std::error_code ec;
    m_FileSize = std::filesystem::file_size(path, ec);
    if (ec)
        m_FileSize = 0;

    LoadTextureFromFile();

    // Reset view
    m_Zoom = 1.0f;
    m_PanX = 0.0f;
    m_PanY = 0.0f;
}

void TextureViewerPanel::OpenRuntimeTexture(const std::string& displayName, std::shared_ptr<RHITexture> texture,
                                            const std::string& sourceLabel)
{
    CloseTexture();

    m_DisplayName = displayName;
    m_SourceLabel = sourceLabel;
    m_LoadError.clear();
    m_IsRuntimeTexture = true;
    m_RuntimeSourceTexture = std::move(texture);
    m_Texture.reset();
    m_OriginalData.clear();
    m_FilePath.clear();
    m_FileSize = 0;
    m_IsRenderGraphTexture = (m_SourceLabel == "Render Graph Resource");
    m_RuntimeSelectedMip = 0;
    m_RuntimeMipLevels = m_RuntimeSourceTexture ? (std::max)(1u, m_RuntimeSourceTexture->GetMipLevels()) : 1u;

    RebuildRuntimePreview();

    m_Zoom = 1.0f;
    m_PanX = 0.0f;
    m_PanY = 0.0f;
}

void TextureViewerPanel::CloseTexture()
{
    // Unregister from ImGui
    if (m_TextureSRV && m_GUI)
    {
        m_GUI->UnregisterTexture(m_TextureSRV);
    }

    m_FilePath.clear();
    m_DisplayName.clear();
    m_SourceLabel.clear();
    m_LoadError.clear();
    m_IsRuntimeTexture = false;
    m_IsRenderGraphTexture = false;
    m_Texture.reset();
    m_RuntimeSourceTexture.reset();
    m_TextureSRV = nullptr;
    m_Width = 0;
    m_Height = 0;
    m_Channels = 0;
    m_FileSize = 0;
    m_RuntimeSelectedMip = 0;
    m_RuntimeMipLevels = 1;
}

void TextureViewerPanel::LoadTextureFromFile()
{
    if (!m_Device || !m_GUI || m_FilePath.empty())
        return;

    // Load image data with stb_image
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(m_FilePath.string().c_str(), &width, &height, &channels, 4);

    if (!data)
    {
        m_LoadError = "Failed to decode texture file.";
        return;
    }

    m_Width = width;
    m_Height = height;
    m_Channels = channels;

    // Store original data
    size_t dataSize = width * height * 4;
    m_OriginalData.resize(dataSize);
    std::memcpy(m_OriginalData.data(), data, dataSize);

    stbi_image_free(data);

    // Create RHI texture
    RHITextureDesc texDesc;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.depth = 1;
    texDesc.arrayLayers = 1;
    texDesc.mipLevels = 1;
    texDesc.format = RHIFormat::R8G8B8A8_UNORM;
    texDesc.type = RHITextureType::Texture2D;
    texDesc.usage = RHITextureUsage::Sampled;
    texDesc.debugName = "TextureViewerImage";

    m_Texture = m_Device->CreateTexture(texDesc);

    if (m_Texture)
    {
        // Apply initial filter (upload data)
        ApplyChannelFilter();

        // Get native resource and register with ImGui
        if (m_GUI)
        {
            void* nativeResource = m_Device->GetNativeTextureResource(m_Texture.get());
            if (nativeResource)
            {
                m_TextureSRV = m_GUI->RegisterTexture(nativeResource);
            }
            else
            {
                m_LoadError = "Failed to register texture with the GUI.";
            }
        }
    }
    else
    {
        m_LoadError = "Failed to create GPU texture for the viewer.";
    }
}

bool TextureViewerPanel::RebuildRuntimePreview()
{
    if (m_TextureSRV && m_GUI)
    {
        m_GUI->UnregisterTexture(m_TextureSRV);
        m_TextureSRV = nullptr;
    }

    m_Texture.reset();
    m_OriginalData.clear();

    if (!m_RuntimeSourceTexture)
        return false;

    const RHIFormat sourceFormat = m_RuntimeSourceTexture->GetFormat();
    m_Channels = InferPreviewChannelCount(sourceFormat);
    m_RuntimeMipLevels = (std::max)(1u, m_RuntimeSourceTexture->GetMipLevels());
    m_RuntimeSelectedMip = (std::min)(m_RuntimeSelectedMip, m_RuntimeMipLevels - 1u);

    bool previewReady = false;
#ifdef DOT_PLATFORM_WINDOWS
    std::vector<uint8_t> previewData;
    int previewWidth = 0;
    int previewHeight = 0;
    if (ReadRuntimeTexturePreviewRGBA8(m_Device, m_RuntimeSourceTexture.get(), m_RuntimeSelectedMip, previewData, previewWidth,
                                       previewHeight) &&
        previewWidth > 0 && previewHeight > 0)
    {
        RHITextureDesc previewDesc;
        previewDesc.width = static_cast<uint32_t>(previewWidth);
        previewDesc.height = static_cast<uint32_t>(previewHeight);
        previewDesc.depth = 1;
        previewDesc.arrayLayers = 1;
        previewDesc.mipLevels = 1;
        previewDesc.format = RHIFormat::R8G8B8A8_UNORM;
        previewDesc.type = RHITextureType::Texture2D;
        previewDesc.usage = RHITextureUsage::Sampled;
        previewDesc.debugName = "TextureViewerRuntimePreview";

        m_Texture = m_Device ? m_Device->CreateTexture(previewDesc) : nullptr;
        if (m_Texture)
        {
            m_OriginalData = previewData;
            m_Width = previewWidth;
            m_Height = previewHeight;
            ApplyChannelFilter();
            previewReady = true;
        }
    }
#endif

    if (!previewReady)
    {
        m_LoadError = "Runtime preview is unavailable for this live GPU texture.";
        m_Texture.reset();
        m_Width = static_cast<int>((std::max)(1u, m_RuntimeSourceTexture->GetWidth() >> m_RuntimeSelectedMip));
        m_Height = static_cast<int>((std::max)(1u, m_RuntimeSourceTexture->GetHeight() >> m_RuntimeSelectedMip));
        return false;
    }

    m_LoadError.clear();
    if (m_GUI && m_Device && m_Texture)
    {
        void* nativeResource = m_Device->GetNativeTextureResource(m_Texture.get());
        if (nativeResource)
            m_TextureSRV = m_GUI->RegisterTexture(nativeResource);
        else
            m_LoadError = "Failed to register runtime preview texture with the GUI.";
    }
    return m_TextureSRV != nullptr;
}

void TextureViewerPanel::ApplyChannelFilter()
{
    if (m_OriginalData.empty() || !m_Texture)
        return;

    int activeChannels = (m_ShowR ? 1 : 0) + (m_ShowG ? 1 : 0) + (m_ShowB ? 1 : 0) + (m_ShowA ? 1 : 0);

    // Optimization: If all channels are standard (R+G+B and/or A), handle efficiently?
    // Actually, just standard implementation is fine for now.

    std::vector<uint8_t> filteredData;
    filteredData.resize(m_OriginalData.size());

    size_t pixelCount = m_Width * m_Height;
    const uint8_t* src = m_OriginalData.data();
    uint8_t* dst = filteredData.data();

    bool singleChannel = (activeChannels == 1);

    for (size_t i = 0; i < pixelCount; ++i)
    {
        size_t offset = i * 4;
        uint8_t r = src[offset + 0];
        uint8_t g = src[offset + 1];
        uint8_t b = src[offset + 2];
        uint8_t a = src[offset + 3];

        if (singleChannel)
        {
            // Grayscale mode for single channel
            uint8_t val = 0;
            if (m_ShowR)
                val = r;
            else if (m_ShowG)
                val = g;
            else if (m_ShowB)
                val = b;
            else if (m_ShowA)
                val = a;

            dst[offset + 0] = val;
            dst[offset + 1] = val;
            dst[offset + 2] = val;
            dst[offset + 3] = 255;
        }
        else
        {
            // Masking mode
            dst[offset + 0] = m_ShowR ? r : 0;
            dst[offset + 1] = m_ShowG ? g : 0;
            dst[offset + 2] = m_ShowB ? b : 0;

            // If Alpha is enabled, show it. If disabled, force opaque (255)
            // UNLESS only Alpha is disabled but RGB enabled -> we want to see RGB opaque.
            // If everything is disabled -> Black opaque.
            dst[offset + 3] = m_ShowA ? a : 255;
        }
    }

    m_Texture->Update(filteredData.data(), 0, 0);
}

void TextureViewerPanel::OnImGui()
{
    if (!m_Open)
        return;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);

    const std::string titleBase = m_DisplayName.empty() ? std::string("Texture Viewer") : m_DisplayName;
    std::string title = titleBase + " - Texture Viewer###TextureViewer";

    if (BeginChromeWindow(title.c_str(), &m_Open, ImGuiWindowFlags_MenuBar))
    {
        if (!HasOpenTexture())
        {
            if (!m_LoadError.empty())
            {
                ImGui::TextWrapped("%s", m_DisplayName.empty() ? "Texture Preview Unavailable" : m_DisplayName.c_str());
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", m_LoadError.c_str());
                if (!m_SourceLabel.empty())
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Source: %s", m_SourceLabel.c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("No texture loaded.");
                ImGui::TextDisabled("Double-click a texture file in the Asset Manager to view it.");
            }
            ImGui::End();
            return;
        }

        DrawToolbar();

        // Main content area - split between texture view and info panel
        float infoWidth = m_ShowInfo ? 200.0f : 0.0f;
        float viewWidth = ImGui::GetContentRegionAvail().x - infoWidth;

        // Texture view (left side)
        ImGui::BeginChild("TextureView", ImVec2(viewWidth, 0), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawTextureView();
        ImGui::EndChild();

        if (m_ShowInfo)
        {
            ImGui::SameLine();

            // Info panel (right side)
            ImGui::BeginChild("InfoPanel", ImVec2(0, 0), true);
            DrawInfoPanel();
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void TextureViewerPanel::DrawToolbar()
{
    if (ImGui::BeginMenuBar())
    {
        if (m_IsRuntimeTexture && m_IsRenderGraphTexture && m_RuntimeMipLevels > 1)
        {
            if (ImGui::SmallButton("Mip -") && m_RuntimeSelectedMip > 0)
            {
                --m_RuntimeSelectedMip;
                RebuildRuntimePreview();
            }

            ImGui::SameLine(0, 4);
            ImGui::Text("Mip %u / %u", m_RuntimeSelectedMip, m_RuntimeMipLevels - 1u);

            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton("Mip +") && (m_RuntimeSelectedMip + 1u) < m_RuntimeMipLevels)
            {
                ++m_RuntimeSelectedMip;
                RebuildRuntimePreview();
            }

            ImGui::SameLine(0, 6);
            ImGui::SetNextItemWidth(120.0f);
            int mipIndex = static_cast<int>(m_RuntimeSelectedMip);
            if (ImGui::SliderInt("##RuntimeMipLevel", &mipIndex, 0, static_cast<int>(m_RuntimeMipLevels - 1u)))
            {
                m_RuntimeSelectedMip = static_cast<uint32>(mipIndex);
                RebuildRuntimePreview();
            }

            ImGui::Separator();
        }

        // Channel toggles
        ImGui::Text("Channels:");
        const bool supportsChannelFilter = !m_OriginalData.empty();
        if (!supportsChannelFilter)
            ImGui::BeginDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button,
                              m_ShowR ? ImVec4(0.8f, 0.2f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton("R"))
        {
            m_ShowR = !m_ShowR;
            ApplyChannelFilter();
        }
        ImGui::PopStyleColor();
        if (!supportsChannelFilter)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Channel isolation is only available for file-backed textures right now.");
        }

        ImGui::SameLine(0, 2);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              m_ShowG ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton("G"))
        {
            m_ShowG = !m_ShowG;
            ApplyChannelFilter();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 2);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              m_ShowB ? ImVec4(0.2f, 0.2f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton("B"))
        {
            m_ShowB = !m_ShowB;
            ApplyChannelFilter();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 2);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              m_ShowA ? ImVec4(0.8f, 0.8f, 0.8f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton("A"))
        {
            m_ShowA = !m_ShowA;
            ApplyChannelFilter();
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Zoom controls
        if (ImGui::SmallButton(ICON_SEARCH_MINUS))
            m_Zoom = std::max(0.1f, m_Zoom * 0.8f);

        ImGui::SameLine(0, 2);
        ImGui::Text("%.0f%%", m_Zoom * 100.0f);

        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton(ICON_SEARCH_PLUS))
            m_Zoom = std::min(10.0f, m_Zoom * 1.25f);

        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("100%"))
        {
            m_Zoom = 1.0f;
            m_PanX = 0.0f;
            m_PanY = 0.0f;
        }

        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton(ICON_EXPAND))
        {
            // Fit to view
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float scaleX = avail.x / static_cast<float>(m_Width);
            float scaleY = avail.y / static_cast<float>(m_Height);
            m_Zoom = std::min(scaleX, scaleY) * 0.9f;
            m_PanX = 0.0f;
            m_PanY = 0.0f;
        }

        ImGui::Separator();

        // Checkerboard toggle
        ImGui::Checkbox("BG", &m_ShowCheckerboard);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show checkerboard background for transparency");

        // Info toggle
        ImGui::SameLine();
        ImGui::Checkbox(ICON_INFO, &m_ShowInfo);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show texture info panel");

        // Close button on right
        ImGui::SameLine(ImGui::GetWindowWidth() - 60);
        if (ImGui::SmallButton("Close"))
        {
            CloseTexture();
        }

        ImGui::EndMenuBar();
    }
}

void TextureViewerPanel::DrawTextureView()
{
    ImVec2 viewSize = ImGui::GetContentRegionAvail();
    ImVec2 viewPos = ImGui::GetCursorScreenPos();

    // Draw checkerboard background if enabled
    if (m_ShowCheckerboard)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float checkSize = 16.0f;
        ImU32 col1 = IM_COL32(100, 100, 100, 255);
        ImU32 col2 = IM_COL32(150, 150, 150, 255);

        for (float y = 0; y < viewSize.y; y += checkSize)
        {
            for (float x = 0; x < viewSize.x; x += checkSize)
            {
                int ix = static_cast<int>(x / checkSize);
                int iy = static_cast<int>(y / checkSize);
                ImU32 col = ((ix + iy) % 2 == 0) ? col1 : col2;

                float x1 = viewPos.x + x;
                float y1 = viewPos.y + y;
                float x2 = std::min(x1 + checkSize, viewPos.x + viewSize.x);
                float y2 = std::min(y1 + checkSize, viewPos.y + viewSize.y);

                drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col);
            }
        }
    }

    // Handle mouse input for pan/zoom
    if (ImGui::IsWindowHovered())
    {
        // Zoom with scroll wheel
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            float zoomFactor = (wheel > 0) ? 1.1f : 0.9f;
            m_Zoom = std::clamp(m_Zoom * zoomFactor, 0.05f, 20.0f);
        }

        // Pan with middle mouse button or right mouse button
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_PanX += delta.x;
            m_PanY += delta.y;
        }
    }

    // Calculate texture position (centered + pan)
    float displayW = m_Width * m_Zoom;
    float displayH = m_Height * m_Zoom;
    float offsetX = (viewSize.x - displayW) * 0.5f + m_PanX;
    float offsetY = (viewSize.y - displayH) * 0.5f + m_PanY;

    // Draw texture if we have an SRV
    if (m_TextureSRV)
    {
        ImVec2 texPos(viewPos.x + offsetX, viewPos.y + offsetY);
        ImVec2 texSize(displayW, displayH);

        // Apply channel tinting
        // Note: Full channel isolation requires a custom shader, but we can do basic tinting
        ImVec4 tint(m_ShowR ? 1.0f : 0.0f, m_ShowG ? 1.0f : 0.0f, m_ShowB ? 1.0f : 0.0f, 1.0f);

        // If showing alpha only, display as grayscale
        if (m_ShowA && !m_ShowR && !m_ShowG && !m_ShowB)
        {
            tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        ImGui::SetCursorScreenPos(texPos);
        // Note: Channel tinting would require custom shader, displaying full texture for now
        ImGui::Image(reinterpret_cast<ImTextureID>(m_TextureSRV), texSize);
    }

    // Show coordinates under cursor
    if (ImGui::IsWindowHovered())
    {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float texX = (mousePos.x - viewPos.x - offsetX) / m_Zoom;
        float texY = (mousePos.y - viewPos.y - offsetY) / m_Zoom;

        if (texX >= 0 && texX < m_Width && texY >= 0 && texY < m_Height)
        {
            ImGui::BeginTooltip();
            ImGui::Text("Pixel: (%d, %d)", static_cast<int>(texX), static_cast<int>(texY));
            ImGui::EndTooltip();
        }
    }
}

void TextureViewerPanel::DrawInfoPanel()
{
    ImGui::Text(ICON_IMAGE " Texture Info");
    ImGui::Separator();

    // Filename
    ImGui::TextWrapped("%s", m_DisplayName.empty() ? "<unnamed>" : m_DisplayName.c_str());
    ImGui::Spacing();

    ImGui::Text("Source:");
    ImGui::SameLine(100);
    ImGui::TextWrapped("%s", m_SourceLabel.empty() ? "Unknown" : m_SourceLabel.c_str());

    // Dimensions
    ImGui::Text("Dimensions:");
    ImGui::SameLine(100);
    ImGui::Text("%d x %d", m_Width, m_Height);

    // Channels
    ImGui::Text("Channels:");
    ImGui::SameLine(100);
    const char* channelStr = "Unknown";
    switch (m_Channels)
    {
        case 1:
            channelStr = "Grayscale";
            break;
        case 2:
            channelStr = "Gray+Alpha";
            break;
        case 3:
            channelStr = "RGB";
            break;
        case 4:
            channelStr = "RGBA";
            break;
    }
    ImGui::Text("%s (%d)", channelStr, m_Channels);

    // Total pixels
    ImGui::Text("Pixels:");
    ImGui::SameLine(100);
    int totalPixels = m_Width * m_Height;
    if (totalPixels >= 1000000)
        ImGui::Text("%.2f MP", totalPixels / 1000000.0f);
    else
        ImGui::Text("%d", totalPixels);

    // File size
    ImGui::Text("File Size:");
    ImGui::SameLine(100);
    if (m_IsRuntimeTexture)
        ImGui::TextUnformatted("N/A (runtime)");
    else if (m_FileSize >= 1024 * 1024)
        ImGui::Text("%.2f MB", m_FileSize / (1024.0f * 1024.0f));
    else if (m_FileSize >= 1024)
        ImGui::Text("%.2f KB", m_FileSize / 1024.0f);
    else
        ImGui::Text("%zu B", m_FileSize);

    // Memory size (uncompressed)
    ImGui::Text("Memory:");
    ImGui::SameLine(100);
    size_t memSize = m_Texture ? EstimateTextureBytes(m_Texture->GetFormat(), m_Width, m_Height)
                               : static_cast<size_t>(m_Width) * m_Height * 4;
    if (memSize >= 1024 * 1024)
        ImGui::Text("%.2f MB", memSize / (1024.0f * 1024.0f));
    else
        ImGui::Text("%.2f KB", memSize / 1024.0f);

    ImGui::Separator();

    if (m_IsRuntimeTexture)
    {
        ImGui::Text("Mip:");
        ImGui::SameLine(100);
        ImGui::Text("%u / %u", m_RuntimeSelectedMip, m_RuntimeMipLevels > 0 ? (m_RuntimeMipLevels - 1u) : 0u);
        ImGui::Separator();
    }

    // View info
    ImGui::Text("View:");
    ImGui::Text("  Zoom: %.0f%%", m_Zoom * 100.0f);
    ImGui::Text("  Pan: (%.0f, %.0f)", m_PanX, m_PanY);

    ImGui::Separator();

    // File path
    if (!m_IsRuntimeTexture)
    {
        ImGui::Text("Path:");
        ImGui::TextWrapped("%s", m_FilePath.string().c_str());
    }
    else
    {
        ImGui::Text("Path:");
        ImGui::TextDisabled("Live runtime texture");
    }
}

} // namespace Dot
