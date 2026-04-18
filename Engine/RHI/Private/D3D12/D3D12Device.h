// =============================================================================
// Dot Engine - D3D12 Device
// =============================================================================
// D3D12 implementation of RHIDevice.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHIDevice.h"

#include <string>
#include <unordered_map>
#include <vector>

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12SwapChain;
class D3D12Pipeline;

class D3D12Device : public RHIDevice
{
public:
    D3D12Device();
    ~D3D12Device() override;

    // =========================================================================
    // Device Info
    // =========================================================================
    const char* GetName() const override { return m_DeviceName.c_str(); }
    const char* GetVendor() const override { return m_VendorName.c_str(); }

    // =========================================================================
    // Resource Creation
    // =========================================================================
    RHIBufferPtr CreateBuffer(const RHIBufferDesc& desc) override;
    RHITexturePtr CreateTexture(const RHITextureDesc& desc) override;
    RHISamplerPtr CreateSampler(const RHISamplerDesc& desc) override;
    RHIShaderPtr CreateShader(const RHIShaderBytecode& bytecode) override;
    RHIShaderProgramPtr CreateShaderProgram(RHIShaderPtr vertex, RHIShaderPtr fragment) override;
    RHISwapChainPtr CreateSwapChain(const RHISwapChainDesc& desc) override;
    RHIPipelinePtr CreatePipeline(const RHIPipelineDesc& desc) override;
    void UpdateTexture(RHITexturePtr texture, const void* data) override;

    // =========================================================================
    // Frame Operations
    // =========================================================================
    void BeginFrame() override;
    void EndFrame() override;
    void Submit() override;
    void Present() override;
    bool SupportsTimestamps() const override { return m_TimestampQueryHeap != nullptr; }
    uint32 BeginTimestamp(const char* label) override;
    void EndTimestamp(uint32 handle) override;
    double GetLastTimestampMs(const char* label) const override;
    RHITexturePtr WrapNativeTexture(void* nativeResource, const RHITextureDesc& desc,
                                    RHIResourceState initialState = RHIResourceState::Common) override;
    void* GetNativeTextureResource(RHITexture* texture) override;

    // =========================================================================
    // Render Commands
    // =========================================================================
    void SetPipeline(RHIPipelinePtr pipeline) override;
    void SetVertexBuffer(RHIBufferPtr buffer, uint32 slot = 0) override;
    void SetIndexBuffer(RHIBufferPtr buffer, RHIFormat format = RHIFormat::R16_UINT) override;
    void SetConstantBuffer(RHIBufferPtr buffer, uint32 slot = 0) override;

    void SetRenderTarget(RHITexturePtr color, RHITexturePtr depth = nullptr) override;
    void SetSwapChainRenderTarget(RHISwapChainPtr swapChain, RHITexturePtr depth = nullptr) override;
    void ClearRenderTarget(float r, float g, float b, float a) override;
    void ClearDepthStencil(float depth = 1.0f, uint8 stencil = 0) override;
    void TransitionTexture(RHITexturePtr texture, RHIResourceState before, RHIResourceState after) override;
    void TransitionBuffer(RHIBufferPtr buffer, RHIResourceState before, RHIResourceState after) override;

    void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f,
                     float maxDepth = 1.0f) override;
    void SetScissor(int32 x, int32 y, int32 width, int32 height) override;

    void Draw(uint32 vertexCount, uint32 firstVertex = 0) override;
    void DrawIndexed(uint32 indexCount, uint32 firstIndex = 0, int32 vertexOffset = 0) override;
    void DrawInstanced(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex = 0,
                       uint32 firstInstance = 0) override;

    // =========================================================================
    // D3D12-specific
    // =========================================================================
    ID3D12Device* GetDevice() const { return m_Device.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }

    void KeepAlive(ComPtr<ID3D12Resource> resource) { m_Garbage.push_back(resource); }

private:
    std::vector<ComPtr<ID3D12Resource>> m_Garbage;
    bool CreateDeviceAndQueue();
    bool CreateCommandObjects();
    bool CreateTimestampResources();
    void ResolveTimestampResults();
    void WaitForGPU();

    struct TimestampSpan
    {
        std::string label;
        uint32 beginIndex = UINT32_MAX;
        uint32 endIndex = UINT32_MAX;
    };

    ComPtr<IDXGIFactory6> m_Factory;
    ComPtr<IDXGIAdapter4> m_Adapter;
    ComPtr<ID3D12Device> m_Device;
    ComPtr<ID3D12CommandQueue> m_CommandQueue;
    ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;
    ComPtr<ID3D12Fence> m_Fence;
    UINT64 m_FenceValue = 0;
    HANDLE m_FenceEvent = nullptr;
    ComPtr<ID3D12QueryHeap> m_TimestampQueryHeap;
    ComPtr<ID3D12Resource> m_TimestampReadbackBuffer;
    uint64 m_TimestampFrequency = 0;
    uint32 m_TimestampQueryCount = 0;
    std::vector<TimestampSpan> m_TimestampSpans;
    std::unordered_map<std::string, double> m_LastTimestampResults;

    // Depth buffer for default render target
    ComPtr<ID3D12Resource> m_DepthBuffer;
    ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

    // Current state
    D3D12SwapChain* m_CurrentSwapChain = nullptr;
    D3D12Pipeline* m_CurrentPipeline = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentDSV = {};

    std::string m_DeviceName;
    std::string m_VendorName;
};

/// Create D3D12 device
RHIDevicePtr CreateD3D12Device();

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
