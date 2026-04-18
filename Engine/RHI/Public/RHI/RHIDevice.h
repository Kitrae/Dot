// =============================================================================
// Dot Engine - RHI Device
// =============================================================================
// Abstract GPU device interface - factory for GPU resources and commands.
// =============================================================================

#pragma once

#include "RHI/RHIBuffer.h"
#include "RHI/RHIPipeline.h"
#include "RHI/RHIShader.h"
#include "RHI/RHISwapChain.h"
#include "RHI/RHITexture.h"
#include "RHI/RHITypes.h"

namespace Dot
{

/// Abstract GPU device
class RHIDevice
{
public:
    virtual ~RHIDevice() = default;

    // =========================================================================
    // Device Info
    // =========================================================================
    virtual const char* GetName() const = 0;
    virtual const char* GetVendor() const = 0;

    // =========================================================================
    // Resource Creation
    // =========================================================================
    virtual RHIBufferPtr CreateBuffer(const RHIBufferDesc& desc) = 0;
    virtual RHITexturePtr CreateTexture(const RHITextureDesc& desc) = 0;
    virtual RHISamplerPtr CreateSampler(const RHISamplerDesc& desc) = 0;
    virtual RHIShaderPtr CreateShader(const RHIShaderBytecode& bytecode) = 0;
    virtual RHIShaderProgramPtr CreateShaderProgram(RHIShaderPtr vertex, RHIShaderPtr fragment) = 0;
    virtual RHISwapChainPtr CreateSwapChain(const RHISwapChainDesc& desc) = 0;
    virtual RHIPipelinePtr CreatePipeline(const RHIPipelineDesc& desc) = 0;
    virtual void UpdateTexture(RHITexturePtr texture, const void* data) = 0;

    // =========================================================================
    // Frame Operations
    // =========================================================================
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Submit() = 0;
    virtual void Present() = 0;
    virtual bool SupportsTimestamps() const { return false; }
    virtual uint32 BeginTimestamp(const char* label)
    {
        (void)label;
        return UINT32_MAX;
    }
    virtual void EndTimestamp(uint32 handle) { (void)handle; }
    virtual double GetLastTimestampMs(const char* label) const
    {
        (void)label;
        return 0.0;
    }
    virtual RHITexturePtr WrapNativeTexture(void* nativeResource, const RHITextureDesc& desc,
                                            RHIResourceState initialState = RHIResourceState::Common)
    {
        (void)nativeResource;
        (void)desc;
        (void)initialState;
        return nullptr;
    }

    // Native resource access (for ImGui texture rendering)
    virtual void* GetNativeTextureResource(RHITexture* texture) = 0;

    // =========================================================================
    // Render Commands
    // =========================================================================

    // State binding
    virtual void SetPipeline(RHIPipelinePtr pipeline) = 0;
    virtual void SetVertexBuffer(RHIBufferPtr buffer, uint32 slot = 0) = 0;
    virtual void SetIndexBuffer(RHIBufferPtr buffer, RHIFormat format = RHIFormat::R16_UINT) = 0;
    virtual void SetConstantBuffer(RHIBufferPtr buffer, uint32 slot = 0) = 0;

    // Render targets
    virtual void SetRenderTarget(RHITexturePtr color, RHITexturePtr depth = nullptr) = 0;
    virtual void SetSwapChainRenderTarget(RHISwapChainPtr swapChain, RHITexturePtr depth = nullptr) = 0;
    virtual void ClearRenderTarget(float r, float g, float b, float a) = 0;
    virtual void ClearDepthStencil(float depth = 1.0f, uint8 stencil = 0) = 0;
    virtual void TransitionTexture(RHITexturePtr texture, RHIResourceState before, RHIResourceState after) = 0;
    virtual void TransitionBuffer(RHIBufferPtr buffer, RHIResourceState before, RHIResourceState after) = 0;

    // Viewport and scissor
    virtual void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f,
                             float maxDepth = 1.0f) = 0;
    virtual void SetScissor(int32 x, int32 y, int32 width, int32 height) = 0;

    // Draw calls
    virtual void Draw(uint32 vertexCount, uint32 firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32 indexCount, uint32 firstIndex = 0, int32 vertexOffset = 0) = 0;
    virtual void DrawInstanced(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex = 0,
                               uint32 firstInstance = 0) = 0;

protected:
    RHIDevice() = default;
};

using RHIDevicePtr = std::shared_ptr<RHIDevice>;

/// Create the default RHI device for the current platform
RHIDevicePtr CreateRHIDevice();

} // namespace Dot
