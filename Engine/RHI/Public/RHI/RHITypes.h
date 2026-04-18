// =============================================================================
// Dot Engine - RHI Types
// =============================================================================
// Common types and enumerations for the Render Hardware Interface.
// =============================================================================

#pragma once

#include "Core/Core.h"

namespace Dot
{

// =============================================================================
// Pixel Formats
// =============================================================================

enum class RHIFormat : uint8
{
    Unknown = 0,

    // Color formats
    R8_UNORM,
    R8G8_UNORM,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_UNORM,
    B8G8R8A8_SRGB,

    // Float formats
    R16_FLOAT,
    R16G16_FLOAT,
    R16G16B16A16_FLOAT,
    R32_FLOAT,
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    R32G32B32A32_FLOAT,

    // Integer formats (for index buffers)
    R16_UINT,
    R32_UINT,

    // Depth/Stencil
    D16_UNORM,
    D24_UNORM_S8_UINT,
    D32_FLOAT,
    D32_FLOAT_S8_UINT,

    Count
};

// =============================================================================
// Buffer Types
// =============================================================================

enum class RHIBufferUsage : uint8
{
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    Indirect = 1 << 4,
};

inline RHIBufferUsage operator|(RHIBufferUsage a, RHIBufferUsage b)
{
    return static_cast<RHIBufferUsage>(static_cast<uint8>(a) | static_cast<uint8>(b));
}

enum class RHIMemoryUsage : uint8
{
    GPU_Only,   // Fast GPU memory, no CPU access
    CPU_To_GPU, // Upload heap, CPU write, GPU read
    GPU_To_CPU, // Readback heap, GPU write, CPU read
};

// =============================================================================
// Shader Types
// =============================================================================

enum class RHIShaderStage : uint8
{
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEval,
    Count
};

// =============================================================================
// Primitive Types
// =============================================================================

enum class RHIPrimitiveType : uint8
{
    Points,
    Lines,
    LineStrip,
    Triangles,
    TriangleStrip,
    TriangleFan,
};

// =============================================================================
// Texture Types
// =============================================================================

enum class RHITextureType : uint8
{
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Texture2DArray,
};

enum class RHITextureUsage : uint8
{
    Sampled = 1 << 0,      // Can be sampled in shaders
    Storage = 1 << 1,      // Can be written in compute
    RenderTarget = 1 << 2, // Can be used as color attachment
    DepthStencil = 1 << 3, // Can be used as depth/stencil
};

inline RHITextureUsage operator|(RHITextureUsage a, RHITextureUsage b)
{
    return static_cast<RHITextureUsage>(static_cast<uint8>(a) | static_cast<uint8>(b));
}

// =============================================================================
// Resource States
// =============================================================================

enum class RHIResourceState : uint8
{
    Unknown = 0,
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,
    UnorderedAccess,
    CopySource,
    CopyDest,
    Present,
};

// =============================================================================
// Sampler Types
// =============================================================================

enum class RHIFilter : uint8
{
    Nearest,
    Linear,
};

enum class RHISamplerAddressMode : uint8
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

// =============================================================================
// Pipeline State
// =============================================================================

enum class RHIBlendFactor : uint8
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class RHIBlendOp : uint8
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class RHICompareOp : uint8
{
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class RHICullMode : uint8
{
    None,
    Front,
    Back,
};

enum class RHIFrontFace : uint8
{
    CounterClockwise,
    Clockwise,
};

// =============================================================================
// Descriptors
// =============================================================================

struct RHIBufferDesc
{
    usize size = 0;
    RHIBufferUsage usage = RHIBufferUsage::Vertex;
    RHIMemoryUsage memory = RHIMemoryUsage::GPU_Only;
    const char* debugName = nullptr;
};

struct RHITextureDesc
{
    uint32 width = 1;
    uint32 height = 1;
    uint32 depth = 1;
    uint32 mipLevels = 1;
    uint32 arrayLayers = 1;
    RHIFormat format = RHIFormat::R8G8B8A8_UNORM;
    RHITextureType type = RHITextureType::Texture2D;
    RHITextureUsage usage = RHITextureUsage::Sampled;
    const char* debugName = nullptr;
};

struct RHISamplerDesc
{
    RHIFilter minFilter = RHIFilter::Linear;
    RHIFilter magFilter = RHIFilter::Linear;
    RHIFilter mipFilter = RHIFilter::Linear;
    RHISamplerAddressMode addressU = RHISamplerAddressMode::Repeat;
    RHISamplerAddressMode addressV = RHISamplerAddressMode::Repeat;
    RHISamplerAddressMode addressW = RHISamplerAddressMode::Repeat;
    float maxAnisotropy = 1.0f;
};

} // namespace Dot
