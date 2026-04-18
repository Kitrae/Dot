// =============================================================================
// Dot Engine - D3D12 GUI (ImGui)
// =============================================================================
// ImGui implementation using D3D12 and Win32 backends.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHIGUI.h"

#include <vector>

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12Device;
class D3D12SwapChain;

/// D3D12 + ImGui implementation of RHIGUI
class D3D12GUI : public RHIGUI
{
public:
    D3D12GUI(D3D12Device* device, D3D12SwapChain* swapChain, const RHIGUIDesc& desc);
    ~D3D12GUI() override;

    // Non-copyable
    D3D12GUI(const D3D12GUI&) = delete;
    D3D12GUI& operator=(const D3D12GUI&) = delete;

    // RHIGUI interface
    void BeginFrame() override;
    void EndFrame() override;
    void OnResize(uint32_t width, uint32_t height) override;
    bool WantCaptureMouse() const override;
    bool WantCaptureKeyboard() const override;
    void* RegisterTexture(void* nativeTexture) override;
    void UnregisterTexture(void* textureId) override;

private:
    void CreateDescriptorHeap();
    void InitImGui(HWND hwnd);
    void ShutdownImGui();

    D3D12Device* m_Device = nullptr;
    D3D12SwapChain* m_SwapChain = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_SRVHeap;
    bool m_Initialized = false;

    // Texture registration tracking
    static constexpr UINT MAX_TEXTURE_SLOTS = 128; // Slot 0 reserved for fonts
    UINT m_DescriptorSize = 0;
    std::vector<bool> m_UsedSlots; // Track which slots are in use
};

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
