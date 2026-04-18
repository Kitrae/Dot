// =============================================================================
// Dot Engine - RHI Pipeline
// =============================================================================
// Abstract graphics pipeline interface.
// =============================================================================

#pragma once

#include "RHI/RHIShader.h"
#include "RHI/RHITypes.h"

#include <memory>
#include <vector>

namespace Dot
{

// =============================================================================
// Vertex Input Layout
// =============================================================================

/// Vertex attribute description
struct RHIVertexAttribute
{
    const char* semantic = "POSITION";
    uint32 semanticIndex = 0;
    RHIFormat format = RHIFormat::R32G32B32_FLOAT;
    uint32 offset = 0;
};

/// Vertex buffer binding
struct RHIVertexBinding
{
    uint32 stride = 0;
    bool perInstance = false;
};

// =============================================================================
// Pipeline States
// =============================================================================

struct RHIBlendState
{
    bool enabled = false;
    RHIBlendFactor srcColor = RHIBlendFactor::One;
    RHIBlendFactor dstColor = RHIBlendFactor::Zero;
    RHIBlendOp colorOp = RHIBlendOp::Add;
    RHIBlendFactor srcAlpha = RHIBlendFactor::One;
    RHIBlendFactor dstAlpha = RHIBlendFactor::Zero;
    RHIBlendOp alphaOp = RHIBlendOp::Add;
};

struct RHIDepthStencilState
{
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    RHICompareOp depthCompareOp = RHICompareOp::Less;
    bool stencilEnabled = false;
};

struct RHIRasterizerState
{
    RHICullMode cullMode = RHICullMode::Back;
    RHIFrontFace frontFace = RHIFrontFace::CounterClockwise;
    bool wireframe = false;
};

// =============================================================================
// Pipeline Descriptor
// =============================================================================

struct RHIPipelineDesc
{
    // Shaders
    RHIShaderPtr vertexShader;
    RHIShaderPtr pixelShader;

    // Vertex input
    std::vector<RHIVertexAttribute> vertexAttributes;
    RHIVertexBinding vertexBinding;

    // States
    RHIBlendState blendState;
    RHIDepthStencilState depthStencilState;
    RHIRasterizerState rasterizerState;

    // Output formats
    RHIFormat renderTargetFormat = RHIFormat::R8G8B8A8_UNORM;
    RHIFormat depthStencilFormat = RHIFormat::D32_FLOAT;

    // Topology
    RHIPrimitiveType primitiveType = RHIPrimitiveType::Triangles;

    const char* debugName = nullptr;
};

// =============================================================================
// Pipeline Object
// =============================================================================

/// Abstract immutable pipeline state object
class RHIPipeline
{
public:
    virtual ~RHIPipeline() = default;

protected:
    RHIPipeline() = default;
};

using RHIPipelinePtr = std::shared_ptr<RHIPipeline>;

} // namespace Dot
