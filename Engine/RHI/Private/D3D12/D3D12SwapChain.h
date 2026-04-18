// =============================================================================
// Dot Engine - D3D12 SwapChain
// =============================================================================
// Manages the swap chain for presenting to a window.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHISwapChain.h"

#include <vector>

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12Device;

/// D3D12 Swap Chain - manages back buffers and presentation
class D3D12SwapChain : public RHISwapChain
{
public:
    D3D12SwapChain(D3D12Device* device, const RHISwapChainDesc& desc);
    ~D3D12SwapChain() override;

    // Non-copyable
    D3D12SwapChain(const D3D12SwapChain&) = delete;
    D3D12SwapChain& operator=(const D3D12SwapChain&) = delete;

    // RHISwapChain interface
    void BeginFrame() override;
    void EndFrame() override;
    void SetClearColor(float r, float g, float b, float a = 1.0f) override;
    void GetClearColor(float& r, float& g, float& b, float& a) const override;
    void Present() override;
    void Resize(uint32_t width, uint32_t height) override;
    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uint32_t GetCurrentBackBufferIndex() const override { return m_CurrentBackBufferIndex; }
    void* GetDepthSRV() const override;

    // D3D12-specific: Get current back buffer for rendering
    ID3D12Resource* GetCurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
    ID3D12Resource* GetDepthBuffer() const { return m_DepthBuffer.Get(); }
    DXGI_FORMAT GetFormat() const { return m_Format; }
    DXGI_FORMAT GetDepthFormat() const { return m_DepthFormat; }

private:
    void CreateSwapChain(HWND hwnd);
    void CreateRTVs();
    void CreateDepthBuffer();
    void ReleaseBuffers();

    D3D12Device* m_Device = nullptr;
    ComPtr<IDXGISwapChain4> m_SwapChain;
    ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_DSVHeap;
    ComPtr<ID3D12DescriptorHeap> m_DepthSRVHeap;
    ComPtr<ID3D12Resource> m_DepthBuffer;
    std::vector<ComPtr<ID3D12Resource>> m_BackBuffers;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_BufferCount = 2;
    uint32_t m_CurrentBackBufferIndex = 0;
    uint32_t m_RTVDescriptorSize = 0;
    DXGI_FORMAT m_Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT m_DepthFormat = DXGI_FORMAT_D32_FLOAT;
    bool m_VSync = true;
    float m_ClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
