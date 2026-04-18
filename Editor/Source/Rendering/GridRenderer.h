// =============================================================================
// Dot Engine - Grid Renderer
// =============================================================================
// Renders a ground plane grid and axis indicators for spatial reference.
// =============================================================================

#pragma once

#include <memory>

namespace Dot
{

class RHIDevice;
class RHISwapChain;
class Camera;

/// Renders a grid on the XZ plane and axis indicators at origin
class GridRenderer
{
public:
    GridRenderer() = default;
    ~GridRenderer();

    /// Initialize D3D12 resources
    bool Initialize(RHIDevice* device);

    /// Shutdown and release resources
    void Shutdown();

    /// Render the grid and axis indicators
    void Render(const Camera& camera, RHISwapChain* swapChain);

private:
    bool CreatePipelineState();
    bool CreateBuffers();

    RHIDevice* m_Device = nullptr;
    void* m_RootSignature = nullptr;
    void* m_PipelineState = nullptr;

    std::shared_ptr<class RHIBuffer> m_VertexBuffer;
    std::shared_ptr<class RHIBuffer> m_ConstantBuffer;

    bool m_Initialized = false;
};

} // namespace Dot
