// =============================================================================
// Dot Engine - RHI GUI
// =============================================================================
// Abstract GUI rendering interface. Backend handles ImGui initialization
// and rendering without exposing it to the application.
// =============================================================================

#pragma once

#include <cstdint>
#include <memory>

namespace Dot
{

class RHIDevice;
class RHISwapChain;

/// Abstract GUI renderer - wraps ImGui or other UI frameworks
class RHIGUI
{
public:
    virtual ~RHIGUI() = default;

    /// Start a new UI frame (call at beginning of frame)
    virtual void BeginFrame() = 0;

    /// End UI frame and render (call after all UI code, before swap chain EndFrame)
    virtual void EndFrame() = 0;

    /// Handle window resize
    virtual void OnResize(uint32_t width, uint32_t height) = 0;

    /// Check if GUI wants to capture mouse input
    virtual bool WantCaptureMouse() const = 0;

    /// Check if GUI wants to capture keyboard input
    virtual bool WantCaptureKeyboard() const = 0;

    /// Register a texture for ImGui rendering, returns an ImTextureID (void*)
    /// The texture must be in D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE state
    virtual void* RegisterTexture(void* nativeTexture) = 0;

    /// Unregister a previously registered texture
    virtual void UnregisterTexture(void* textureId) = 0;

protected:
    RHIGUI() = default;
};

using RHIGUIPtr = std::shared_ptr<RHIGUI>;

/// Configuration for GUI creation
struct RHIGUIDesc
{
    void* WindowHandle = nullptr; // Platform window handle
    uint32_t Width = 1280;
    uint32_t Height = 720;
};

/// Create a GUI renderer for the given device and swap chain
RHIGUIPtr CreateGUI(RHIDevice* device, RHISwapChain* swapChain, const RHIGUIDesc& desc);

} // namespace Dot
