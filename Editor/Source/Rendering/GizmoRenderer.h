// =============================================================================
// Dot Engine - Gizmo Renderer
// =============================================================================
// Low-level API for drawing gizmo primitives (lines, arrows, shapes).
// Designed to be reusable by any gizmo implementation.
// =============================================================================

#pragma once

#include <RHI/RHIBuffer.h>
#include <cstdint>
#include <vector>

namespace Dot
{

class RHIDevice;
class RHISwapChain;
class Camera;

/// Vertex for gizmo line primitives
struct GizmoVertex
{
    float x, y, z;    // Position
    float r, g, b, a; // Color
};

/// Gizmo Renderer - Draws lines, arrows, circles, and boxes for editor gizmos
class GizmoRenderer
{
public:
    explicit GizmoRenderer(bool alwaysOnTop = false) : m_AlwaysOnTop(alwaysOnTop) {}
    ~GizmoRenderer();

    /// Initialize D3D12 resources
    bool Initialize(RHIDevice* device);

    /// Shutdown and release resources
    void Shutdown();

    // =========================================================================
    // Drawing API - Call these between Begin() and End()
    // =========================================================================

    /// Begin a new frame of gizmo drawing
    void Begin();

    /// Draw a line segment
    void DrawLine(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g, float b,
                  float a = 1.0f);

    /// Draw an arrow (line with arrowhead)
    void DrawArrow(float originX, float originY, float originZ, float dirX, float dirY, float dirZ, float length,
                   float r, float g, float b, float a = 1.0f);

    /// Draw a circle/ring (for rotation gizmos)
    void DrawCircle(float centerX, float centerY, float centerZ, float normalX, float normalY, float normalZ,
                    float radius, float r, float g, float b, float a = 1.0f, int segments = 32);

    /// Draw a wireframe box
    void DrawBox(float centerX, float centerY, float centerZ, float sizeX, float sizeY, float sizeZ, float r, float g,
                 float b, float a = 1.0f);

    /// Draw XYZ axis indicator
    void DrawAxisIndicator(float x, float y, float z, float size = 1.0f);

    /// Draw simple camera-facing world-space text using line glyphs
    void DrawBillboardText(const Camera& camera, float x, float y, float z, const char* text, float height, float r,
                           float g, float b, float a = 1.0f, bool centered = true);

    /// End frame and submit draw calls
    void End(const Camera& camera, RHISwapChain* swapChain);

    // =========================================================================
    // Utility
    // =========================================================================

    /// Get current vertex count for this frame
    size_t GetVertexCount() const { return m_Vertices.size(); }

private:
    bool CreatePipeline();
    bool CreateBuffers();

    RHIDevice* m_Device = nullptr;
    bool m_Initialized = false;

    // D3D12 resources (stored as void* for RHI abstraction)
    void* m_RootSignature = nullptr;
    void* m_PipelineState = nullptr;
    RHIBufferPtr m_VertexBuffer;
    RHIBufferPtr m_ConstantBuffer;

    // Shader bytecode
    std::vector<uint8_t> m_VSBytecode;
    std::vector<uint8_t> m_PSBytecode;

    // Per-frame vertex data
    std::vector<GizmoVertex> m_Vertices;

    // Maximum vertices per frame
    static constexpr size_t MAX_VERTICES = 65536;

    bool m_AlwaysOnTop = false;
};

} // namespace Dot
