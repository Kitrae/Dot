// =============================================================================
// Dot Engine - D3D12 Pipeline Implementation
// =============================================================================

#include "D3D12Pipeline.h"

#include "D3D12Device.h"
#include "D3D12Shader.h"

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

static DXGI_FORMAT ConvertFormat(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::R32G32_FLOAT:
            return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::R32G32B32_FLOAT:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::R16_UINT:
            return DXGI_FORMAT_R16_UINT;
        case RHIFormat::R32_UINT:
            return DXGI_FORMAT_R32_UINT;
        case RHIFormat::D32_FLOAT:
            return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::D24_UNORM_S8_UINT:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

static D3D12_CULL_MODE ConvertCullMode(RHICullMode mode)
{
    switch (mode)
    {
        case RHICullMode::None:
            return D3D12_CULL_MODE_NONE;
        case RHICullMode::Front:
            return D3D12_CULL_MODE_FRONT;
        case RHICullMode::Back:
            return D3D12_CULL_MODE_BACK;
        default:
            return D3D12_CULL_MODE_NONE;
    }
}

static D3D12_COMPARISON_FUNC ConvertCompareOp(RHICompareOp op)
{
    switch (op)
    {
        case RHICompareOp::Never:
            return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareOp::Less:
            return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareOp::Equal:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareOp::LessOrEqual:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareOp::Greater:
            return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareOp::NotEqual:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareOp::GreaterOrEqual:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareOp::Always:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        default:
            return D3D12_COMPARISON_FUNC_LESS;
    }
}

D3D12Pipeline::D3D12Pipeline(D3D12Device* device, const RHIPipelineDesc& desc)
{
    m_VertexStride = desc.vertexBinding.stride;

    // Convert topology
    switch (desc.primitiveType)
    {
        case RHIPrimitiveType::Points:
            m_Topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            break;
        case RHIPrimitiveType::Lines:
            m_Topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            break;
        case RHIPrimitiveType::LineStrip:
            m_Topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            break;
        case RHIPrimitiveType::Triangles:
            m_Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case RHIPrimitiveType::TriangleStrip:
            m_Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            break;
        default:
            m_Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
    }

    CreateRootSignature(device);
    CreatePipelineState(device, desc);
}

bool D3D12Pipeline::CreateRootSignature(D3D12Device* device)
{
    // Simple root signature with one CBV at register b0
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rootParam;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature, error;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    device->GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                             IID_PPV_ARGS(&m_RootSignature));

    return m_RootSignature != nullptr;
}

bool D3D12Pipeline::CreatePipelineState(D3D12Device* device, const RHIPipelineDesc& desc)
{
    // Build input layout from vertex attributes
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    for (const auto& attr : desc.vertexAttributes)
    {
        D3D12_INPUT_ELEMENT_DESC element = {};
        element.SemanticName = attr.semantic;
        element.SemanticIndex = attr.semanticIndex;
        element.Format = ConvertFormat(attr.format);
        element.InputSlot = 0;
        element.AlignedByteOffset = attr.offset;
        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
        inputLayout.push_back(element);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_RootSignature.Get();

    // Set shaders
    if (auto* vs = dynamic_cast<D3D12Shader*>(desc.vertexShader.get()))
    {
        psoDesc.VS = {vs->GetBytecode(), vs->GetBytecodeSize()};
    }
    if (auto* ps = dynamic_cast<D3D12Shader*>(desc.pixelShader.get()))
    {
        psoDesc.PS = {ps->GetBytecode(), ps->GetBytecodeSize()};
    }

    // Blend state
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (desc.blendState.enabled)
    {
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        // TODO: Convert blend factors
    }

    // Rasterizer state
    psoDesc.RasterizerState.FillMode =
        desc.rasterizerState.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = ConvertCullMode(desc.rasterizerState.cullMode);
    psoDesc.RasterizerState.FrontCounterClockwise = (desc.rasterizerState.frontFace == RHIFrontFace::CounterClockwise);
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // Depth stencil state
    psoDesc.DepthStencilState.DepthEnable = desc.depthStencilState.depthTestEnabled;
    psoDesc.DepthStencilState.DepthWriteMask =
        desc.depthStencilState.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = ConvertCompareOp(desc.depthStencilState.depthCompareOp);

    // Input layout
    psoDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};

    // Misc
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = ConvertFormat(desc.renderTargetFormat);
    psoDesc.DSVFormat = ConvertFormat(desc.depthStencilFormat);
    psoDesc.SampleDesc = {1, 0};

    device->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState));

    return m_PipelineState != nullptr;
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
