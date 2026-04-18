// =============================================================================
// Dot Engine - RHI SwapChain
// =============================================================================
// Abstract swap chain interface for presenting to a window.
// =============================================================================

#pragma once

#include "RHI/RHITypes.h"

#include <cstdint>
#include <memory>

namespace Dot
{

class RHIDevice;

/// Configuration for swap chain creation
struct RHISwapChainDesc
{
    void* WindowHandle = nullptr; // Platform-specific window handle (HWND on Windows)
    uint32_t Width = 1280;
    uint32_t Height = 720;
    uint32_t BufferCount = 2;
    bool VSync = true;
};

/// Abstract swap chain for presenting to a window
class RHISwapChain
{
public:
    virtual ~RHISwapChain() = default;

    /// Begin rendering to the back buffer (transitions to render target, clears with set color)
    virtual void BeginFrame() = 0;

    /// End rendering to the back buffer (transitions to present state)
    virtual void EndFrame() = 0;

    /// Set the clear color for BeginFrame
    virtual void SetClearColor(float r, float g, float b, float a = 1.0f) = 0;

    /// Get the current clear color
    virtual void GetClearColor(float& r, float& g, float& b, float& a) const = 0;

    /// Present the current back buffer to the window
    virtual void Present() = 0;

    /// Resize the swap chain (call when window is resized)
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    /// Get current dimensions
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    /// Get current back buffer index
    virtual uint32_t GetCurrentBackBufferIndex() const = 0;

    /// Get depth buffer as SRV for sampling (SSAO/post-processing)
    virtual void* GetDepthSRV() const = 0;

protected:
    RHISwapChain() = default;
};

using RHISwapChainPtr = std::shared_ptr<RHISwapChain>;

/// Create a platform-specific swap chain
RHISwapChainPtr CreateSwapChain(RHIDevice* device, const RHISwapChainDesc& desc);

} // namespace Dot
