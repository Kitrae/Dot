// =============================================================================
// Dot Engine - D3D12 GUI (ImGui) Implementation
// =============================================================================

#include "D3D12GUI.h"

#include "D3D12Device.h"
#include "D3D12SwapChain.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#ifdef DOT_PLATFORM_WINDOWS

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Dot
{

namespace
{

void ApplyGodotInspiredTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ColorButtonPosition = ImGuiDir_Right;

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.53f, 0.58f, 0.65f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.14f, 0.17f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.24f, 0.28f, 0.35f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.21f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.29f, 0.36f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.34f, 0.43f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.12f, 0.15f, 0.85f);

    colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.33f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.42f, 0.51f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.49f, 0.61f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.68f, 0.97f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.37f, 0.59f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.49f, 0.73f, 1.00f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.27f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.36f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.43f, 0.54f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.21f, 0.27f, 0.34f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.37f, 0.48f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.45f, 0.58f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.23f, 0.27f, 0.34f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.46f, 0.59f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.41f, 0.55f, 0.71f, 1.00f);

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.29f, 0.39f, 0.50f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.39f, 0.53f, 0.70f, 0.78f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.63f, 0.83f, 0.95f);

    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.33f, 0.42f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.21f, 0.31f, 0.41f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.46f, 0.68f, 0.96f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.18f, 0.23f, 0.29f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.33f, 0.49f, 0.72f, 1.00f);

    colors[ImGuiCol_DockingPreview] = ImVec4(0.32f, 0.54f, 0.89f, 0.60f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);

    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.24f, 0.28f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.19f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.27f, 0.46f, 0.75f, 0.45f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.49f, 0.75f, 1.00f, 0.90f);
    colors[ImGuiCol_NavCursor] = ImVec4(0.49f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.04f, 0.72f);
}

void ConfigureFonts(ImGuiIO& io)
{
    io.Fonts->Clear();

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = false;

    bool loadedBaseFont = false;
    const char* uiFontPaths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/seguiemj.ttf",
    };

    for (const char* path : uiFontPaths)
    {
        if (FILE* f = fopen(path, "rb"))
        {
            fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, 16.0f, &fontConfig))
            {
                loadedBaseFont = true;
                std::printf("Loaded UI font from %s\n", path);
                break;
            }
        }
    }

    if (!loadedBaseFont)
        io.Fonts->AddFontDefault(&fontConfig);

    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = 13.0f;

    static const ImWchar iconsRanges[] = {0xf000, 0xf8ff, 0};
    const char* iconFontPaths[] = {"Editor/Resources/Fonts/fa-solid-900.ttf", "../Editor/Resources/Fonts/fa-solid-900.ttf",
                                   "../../Editor/Resources/Fonts/fa-solid-900.ttf",
                                   "../../../Editor/Resources/Fonts/fa-solid-900.ttf"};

    bool loadedIcons = false;
    for (const char* path : iconFontPaths)
    {
        if (FILE* f = fopen(path, "rb"))
        {
            fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, 13.0f, &iconConfig, iconsRanges))
            {
                loadedIcons = true;
                std::printf("Loaded icons from %s\n", path);
                break;
            }
        }
    }

    if (!loadedIcons)
        std::printf("WARNING: Could not find FontAwesome font. Icons will not display.\n");
}

} // namespace

D3D12GUI::D3D12GUI(D3D12Device* device, D3D12SwapChain* swapChain, const RHIGUIDesc& desc)
    : m_Device(device), m_SwapChain(swapChain)
{
    CreateDescriptorHeap();
    InitImGui(static_cast<HWND>(desc.WindowHandle));
}

D3D12GUI::~D3D12GUI()
{
    ShutdownImGui();
}

void D3D12GUI::CreateDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = MAX_TEXTURE_SLOTS; // Support multiple textures
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = m_Device->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_SRVHeap));
    CheckHR(hr, "CreateDescriptorHeap for ImGui");

    m_DescriptorSize = m_Device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_UsedSlots.resize(MAX_TEXTURE_SLOTS, false);
    m_UsedSlots[0] = true; // Slot 0 reserved for ImGui fonts
}

void D3D12GUI::InitImGui(HWND hwnd)
{
    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Multi-viewport (optional)

    ApplyGodotInspiredTheme();
    ConfigureFonts(io);

    // Init Win32 backend
    ImGui_ImplWin32_Init(hwnd);

    // Init D3D12 backend with new API (requires CommandQueue for texture uploads)
    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = m_Device->GetDevice();
    initInfo.CommandQueue = m_Device->GetCommandQueue();
    initInfo.NumFramesInFlight = 2;
    initInfo.RTVFormat = m_SwapChain->GetFormat();
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = m_SRVHeap.Get();
    initInfo.LegacySingleSrvCpuDescriptor = m_SRVHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = m_SRVHeap->GetGPUDescriptorHandleForHeapStart();

    ImGui_ImplDX12_Init(&initInfo);

    m_Initialized = true;
}

void D3D12GUI::ShutdownImGui()
{
    if (m_Initialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_Initialized = false;
    }
}

void D3D12GUI::BeginFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void D3D12GUI::EndFrame()
{
    ImGui::Render();

    // Set descriptor heap and render target
    ID3D12GraphicsCommandList* cmdList = m_Device->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = {m_SRVHeap.Get()};
    cmdList->SetDescriptorHeaps(1, heaps);

    // Set render target for ImGui
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_SwapChain->GetCurrentRTV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

void D3D12GUI::OnResize(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    // ImGui handles resize automatically through Win32 backend
}

bool D3D12GUI::WantCaptureMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool D3D12GUI::WantCaptureKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

// Factory function
RHIGUIPtr CreateGUI(RHIDevice* device, RHISwapChain* swapChain, const RHIGUIDesc& desc)
{
    auto* d3d12Device = static_cast<D3D12Device*>(device);
    auto* d3d12SwapChain = static_cast<D3D12SwapChain*>(swapChain);
    return std::make_shared<D3D12GUI>(d3d12Device, d3d12SwapChain, desc);
}

void* D3D12GUI::RegisterTexture(void* nativeTexture)
{
    if (!nativeTexture || !m_SRVHeap || !m_Device || !m_Device->GetDevice())
        return nullptr;

    ID3D12Resource* resource = static_cast<ID3D12Resource*>(nativeTexture);

    // Find a free slot (skip slot 0 which is for fonts)
    UINT slotIndex = 0;
    for (UINT i = 1; i < MAX_TEXTURE_SLOTS; ++i)
    {
        if (!m_UsedSlots[i])
        {
            slotIndex = i;
            m_UsedSlots[i] = true;
            break;
        }
    }

    if (slotIndex == 0)
    {
        std::printf("WARNING: No free texture slots available for ImGui\n");
        return nullptr;
    }

    // Create SRV at the allocated slot
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_SRVHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += slotIndex * m_DescriptorSize;

    D3D12_RESOURCE_DESC resDesc = resource->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = resDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = resDesc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    m_Device->GetDevice()->CreateShaderResourceView(resource, &srvDesc, cpuHandle);

    // Return the GPU handle as ImTextureID
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_SRVHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += slotIndex * m_DescriptorSize;

    return reinterpret_cast<void*>(gpuHandle.ptr);
}

void D3D12GUI::UnregisterTexture(void* textureId)
{
    if (!textureId)
        return;

    // Calculate which slot this was from the GPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = m_SRVHeap->GetGPUDescriptorHandleForHeapStart();
    UINT64 offset = reinterpret_cast<UINT64>(textureId) - gpuBase.ptr;
    UINT slotIndex = static_cast<UINT>(offset / m_DescriptorSize);

    if (slotIndex > 0 && slotIndex < MAX_TEXTURE_SLOTS)
    {
        m_UsedSlots[slotIndex] = false;
    }
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
