// =============================================================================
// Dot Engine - Grid Renderer Implementation
// =============================================================================

#include "GridRenderer.h"

#include "../Settings/EditorSettings.h"
#include "Camera.h"

#include <cstdio>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Include D3D12 implementations for direct access
#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12SwapChain.h"

// Helper to get D3D12 device/cmdlist
extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);
extern D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer);

namespace Dot
{

struct GridVertex
{
    float x, y, z;
    float r, g, b, a;
};

// Shader code
static const char* g_GridVS = R"(
    cbuffer ConstantBuffer : register(b0) { float4x4 VP; };
    struct VSInput { float3 Position : POSITION; float4 Color : COLOR; };
    struct VSOutput { float4 Position : SV_POSITION; float4 Color : COLOR; };
    VSOutput VSMain(VSInput input) {
        VSOutput output;
        output.Position = mul(VP, float4(input.Position, 1.0f));
        output.Color = input.Color;
        return output;
    }
)";

static const char* g_GridPS = R"(
    struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR; };
    float4 PSMain(PSInput input) : SV_TARGET { return input.Color; }
)";

GridRenderer::~GridRenderer()
{
    Shutdown();
}

bool GridRenderer::Initialize(RHIDevice* device)
{
    m_Device = device;

    if (!CreatePipelineState())
        return false;

    m_Initialized = true;
    return true;
}

void GridRenderer::Shutdown()
{
    if (m_PipelineState)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_PipelineState)->Release();
        m_PipelineState = nullptr;
    }
    if (m_RootSignature)
    {
        reinterpret_cast<ID3D12RootSignature*>(m_RootSignature)->Release();
        m_RootSignature = nullptr;
    }
    m_VertexBuffer.reset();
    m_Initialized = false;
}

bool GridRenderer::CreatePipelineState()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);

    // Root signature with root constants (16 floats for VP matrix)
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace = 0;
    rootParam.Constants.Num32BitValues = 16;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParam;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    if (FAILED(hr))
        return false;

    ID3D12RootSignature* rootSig = nullptr;
    hr = d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                        IID_PPV_ARGS(&rootSig));
    if (FAILED(hr))
        return false;
    m_RootSignature = rootSig;

    // Compile shaders
    ComPtr<ID3DBlob> vsBlob, psBlob;
    hr = D3DCompile(g_GridVS, strlen(g_GridVS), "GridVS", nullptr, nullptr, "VSMain", "vs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Grid VS error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(g_GridPS, strlen(g_GridPS), "GridPS", nullptr, nullptr, "PSMain", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Grid PS error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // Pipeline state for LINE rendering
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr))
        return false;
    m_PipelineState = pso;

    return true;
}

void GridRenderer::Render(const Camera& camera, RHISwapChain* swapChain)
{
    if (!m_Initialized || !swapChain)
        return;

    auto& settings = EditorSettings::Get();
    if (!settings.showGrid && !settings.showAxisIndicator)
        return;

    // Build vertex data dynamically based on settings
    std::vector<GridVertex> vertices;

    if (settings.showGrid)
    {
        float halfSize = settings.gridSize * 0.5f;
        float spacing = settings.gridSpacing;
        float r = settings.gridColorR;
        float g = settings.gridColorG;
        float b = settings.gridColorB;
        float a = settings.gridColorA;

        // Lines along X axis
        for (float z = -halfSize; z <= halfSize; z += spacing)
        {
            vertices.push_back({-halfSize, 0, z, r, g, b, a});
            vertices.push_back({halfSize, 0, z, r, g, b, a});
        }

        // Lines along Z axis
        for (float x = -halfSize; x <= halfSize; x += spacing)
        {
            vertices.push_back({x, 0, -halfSize, r, g, b, a});
            vertices.push_back({x, 0, halfSize, r, g, b, a});
        }
    }

    if (settings.showAxisIndicator)
    {
        float len = settings.axisLength;
        // X axis - Red
        vertices.push_back({0, 0, 0, 1, 0, 0, 1});
        vertices.push_back({len, 0, 0, 1, 0, 0, 1});
        // Y axis - Green
        vertices.push_back({0, 0, 0, 0, 1, 0, 1});
        vertices.push_back({0, len, 0, 0, 1, 0, 1});
        // Z axis - Blue
        vertices.push_back({0, 0, 0, 0, 0, 1, 1});
        vertices.push_back({0, 0, len, 0, 0, 1, 1});
    }

    if (vertices.empty())
        return;

    // Create a dynamic vertex buffer (CPU_To_GPU for Map/Unmap support)
    RHIBufferDesc vbDesc;
    vbDesc.size = vertices.size() * sizeof(GridVertex);
    vbDesc.usage = RHIBufferUsage::Vertex;
    vbDesc.memory = RHIMemoryUsage::CPU_To_GPU;
    m_VertexBuffer = m_Device->CreateBuffer(vbDesc);
    if (!m_VertexBuffer)
        return;

    // Upload vertex data
    m_VertexBuffer->Update(vertices.data(), vbDesc.size);

    // Get VP matrix from camera
    const float* vp = camera.GetViewProjectionMatrix();

    auto* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);

    // Set viewport
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(swapChain->GetWidth());
    viewport.Height = static_cast<float>(swapChain->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    // Set scissor
    D3D12_RECT scissor = {};
    scissor.right = static_cast<LONG>(swapChain->GetWidth());
    scissor.bottom = static_cast<LONG>(swapChain->GetHeight());
    cmdList->RSSetScissorRects(1, &scissor);

    // Set render targets
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Set pipeline
    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_RootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(m_PipelineState));

    // Set VP matrix
    cmdList->SetGraphicsRoot32BitConstants(0, 16, vp, 0);

    // Set vertex buffer
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = GetD3D12BufferGPUAddress(m_VertexBuffer.get());
    vbView.SizeInBytes = static_cast<UINT>(m_VertexBuffer->GetSize());
    vbView.StrideInBytes = sizeof(GridVertex);
    cmdList->IASetVertexBuffers(0, 1, &vbView);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    // Draw
    cmdList->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
}

} // namespace Dot
