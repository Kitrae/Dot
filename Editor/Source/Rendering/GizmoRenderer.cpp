// =============================================================================
// Dot Engine - Gizmo Renderer Implementation
// =============================================================================

#include "GizmoRenderer.h"

#include "Camera.h"

#include <RHI/RHI.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

// D3D12 includes for direct access
#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12SwapChain.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Helper functions to get D3D12 internals
extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);
extern D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer);

namespace Dot
{

namespace
{

struct BitmapGlyph
{
    uint8_t rows[7];
};

BitmapGlyph GetBitmapGlyph(char c)
{
    switch (c)
    {
    case 'A':
        return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'B':
        return {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    case 'C':
        return {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
    case 'D':
        return {{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
    case 'E':
        return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    case 'F':
        return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    case 'G':
        return {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}};
    case 'H':
        return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'I':
        return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
    case 'J':
        return {{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}};
    case 'K':
        return {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
    case 'L':
        return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    case 'M':
        return {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    case 'N':
        return {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
    case 'O':
        return {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    case 'P':
        return {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
    case 'Q':
        return {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
    case 'R':
        return {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    case 'S':
        return {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    case 'T':
        return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    case 'U':
        return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    case 'V':
        return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    case 'W':
        return {{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    case 'X':
        return {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
    case 'Y':
        return {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
    case 'Z':
        return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
    case '0':
        return {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    case '1':
        return {{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    case '2':
        return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
    case '3':
        return {{0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}};
    case '4':
        return {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    case '5':
        return {{0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}};
    case '6':
        return {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    case '7':
        return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    case '8':
        return {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    case '9':
        return {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}};
    case '_':
        return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}};
    case '-':
        return {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
    case '.':
        return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}};
    case '/':
        return {{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}};
    case ':':
        return {{0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}};
    case ' ':
        return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    default:
        return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
    }
}

bool GlyphPixel(const BitmapGlyph& glyph, int row, int column)
{
    return (glyph.rows[row] & (1u << (4 - column))) != 0;
}

void Normalize3(float& x, float& y, float& z)
{
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001f)
        return;
    x /= length;
    y /= length;
    z /= length;
}

void BillboardPoint(float anchorX, float anchorY, float anchorZ, float rightX, float rightY, float rightZ, float upX,
                    float upY, float upZ, float localX, float localY, float& outX, float& outY, float& outZ)
{
    outX = anchorX + rightX * localX + upX * localY;
    outY = anchorY + rightY * localX + upY * localY;
    outZ = anchorZ + rightZ * localX + upZ * localY;
}

} // namespace

GizmoRenderer::~GizmoRenderer()
{
    Shutdown();
}

bool GizmoRenderer::Initialize(RHIDevice* device)
{
    m_Device = device;

    if (!CreatePipeline())
        return false;

    if (!CreateBuffers())
        return false;

    m_Initialized = true;
    return true;
}

void GizmoRenderer::Shutdown()
{
    // Release D3D12 resources
    if (m_RootSignature)
    {
        static_cast<ID3D12RootSignature*>(m_RootSignature)->Release();
        m_RootSignature = nullptr;
    }
    if (m_PipelineState)
    {
        static_cast<ID3D12PipelineState*>(m_PipelineState)->Release();
        m_PipelineState = nullptr;
    }

    m_VertexBuffer.reset();
    m_ConstantBuffer.reset();
    m_Initialized = false;
}

bool GizmoRenderer::CreatePipeline()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice)
        return false;

    // Simple line shader - transforms with MVP, outputs solid color
    const char* vsCode = R"(
        cbuffer ConstantBuffer : register(b0) { float4x4 MVP; };
        struct VSInput { float3 Position : POSITION; float4 Color : COLOR; };
        struct VSOutput { float4 Position : SV_POSITION; float4 Color : COLOR; };
        VSOutput VSMain(VSInput input) {
            VSOutput output;
            output.Position = mul(MVP, float4(input.Position, 1.0f));
            output.Color = input.Color;
            return output;
        }
    )";

    const char* psCode = R"(
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR; };
        float4 PSMain(PSInput input) : SV_TARGET { return input.Color; }
    )";

    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), "GizmoVS", nullptr, nullptr, "VSMain", "vs_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Gizmo VS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(psCode, strlen(psCode), "GizmoPS", nullptr, nullptr, "PSMain", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Gizmo PS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    // Store bytecode
    m_VSBytecode.assign((uint8_t*)vsBlob->GetBufferPointer(),
                        (uint8_t*)vsBlob->GetBufferPointer() + vsBlob->GetBufferSize());
    m_PSBytecode.assign((uint8_t*)psBlob->GetBufferPointer(),
                        (uint8_t*)psBlob->GetBufferPointer() + psBlob->GetBufferSize());

    // Root signature with one constant buffer
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParam;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    if (FAILED(hr))
        return false;

    ID3D12RootSignature* rootSig = nullptr;
    hr = d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                        IID_PPV_ARGS(&rootSig));
    if (FAILED(hr))
        return false;
    m_RootSignature = rootSig;

    // Input layout matching GizmoVertex
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // Pipeline state for LINE_LIST topology
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS = {m_VSBytecode.data(), m_VSBytecode.size()};
    psoDesc.PS = {m_PSBytecode.data(), m_PSBytecode.size()};
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // Lines are visible from both sides
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = m_AlwaysOnTop ? FALSE : TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Don't write to depth
    psoDesc.DepthStencilState.DepthFunc =
        m_AlwaysOnTop ? D3D12_COMPARISON_FUNC_ALWAYS : D3D12_COMPARISON_FUNC_LESS;
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; // Line primitives!
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

bool GizmoRenderer::CreateBuffers()
{
    // Create vertex buffer (dynamic, updated each frame)
    RHIBufferDesc vbDesc;
    vbDesc.size = MAX_VERTICES * sizeof(GizmoVertex);
    vbDesc.usage = RHIBufferUsage::Vertex;
    vbDesc.memory = RHIMemoryUsage::CPU_To_GPU;
    vbDesc.debugName = "GizmoVertexBuffer";

    m_VertexBuffer = m_Device->CreateBuffer(vbDesc);
    if (!m_VertexBuffer)
        return false;

    // Create constant buffer for MVP matrix
    RHIBufferDesc cbDesc;
    cbDesc.size = 256; // D3D12 requires 256-byte alignment
    cbDesc.usage = RHIBufferUsage::Uniform;
    cbDesc.memory = RHIMemoryUsage::CPU_To_GPU;
    cbDesc.debugName = "GizmoConstantBuffer";

    m_ConstantBuffer = m_Device->CreateBuffer(cbDesc);
    return m_ConstantBuffer != nullptr;
}

void GizmoRenderer::Begin()
{
    m_Vertices.clear();
}

void GizmoRenderer::DrawLine(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g, float b,
                             float a)
{
    if (m_Vertices.size() + 2 > MAX_VERTICES)
        return;

    m_Vertices.push_back({x1, y1, z1, r, g, b, a});
    m_Vertices.push_back({x2, y2, z2, r, g, b, a});
}

void GizmoRenderer::DrawArrow(float originX, float originY, float originZ, float dirX, float dirY, float dirZ,
                              float length, float r, float g, float b, float a)
{
    // Normalize direction
    float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (len < 0.0001f)
        return;
    dirX /= len;
    dirY /= len;
    dirZ /= len;

    // Arrow shaft
    float endX = originX + dirX * length;
    float endY = originY + dirY * length;
    float endZ = originZ + dirZ * length;
    DrawLine(originX, originY, originZ, endX, endY, endZ, r, g, b, a);

    // Arrowhead - create perpendicular vectors
    float headSize = length * 0.15f;

    // Find a perpendicular vector
    float perpX, perpY, perpZ;
    if (std::abs(dirY) < 0.9f)
    {
        // Cross with up vector
        perpX = dirZ;
        perpY = 0;
        perpZ = -dirX;
    }
    else
    {
        // Cross with right vector
        perpX = 0;
        perpY = -dirZ;
        perpZ = dirY;
    }

    // Normalize perpendicular
    float perpLen = std::sqrt(perpX * perpX + perpY * perpY + perpZ * perpZ);
    if (perpLen > 0.0001f)
    {
        perpX /= perpLen;
        perpY /= perpLen;
        perpZ /= perpLen;
    }

    // Arrowhead lines
    float backX = endX - dirX * headSize;
    float backY = endY - dirY * headSize;
    float backZ = endZ - dirZ * headSize;

    DrawLine(endX, endY, endZ, backX + perpX * headSize * 0.5f, backY + perpY * headSize * 0.5f,
             backZ + perpZ * headSize * 0.5f, r, g, b, a);
    DrawLine(endX, endY, endZ, backX - perpX * headSize * 0.5f, backY - perpY * headSize * 0.5f,
             backZ - perpZ * headSize * 0.5f, r, g, b, a);
}

void GizmoRenderer::DrawCircle(float centerX, float centerY, float centerZ, float normalX, float normalY, float normalZ,
                               float radius, float r, float g, float b, float a, int segments)
{
    // Find two perpendicular vectors on the plane
    float len = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    if (len < 0.0001f)
        return;
    normalX /= len;
    normalY /= len;
    normalZ /= len;

    // Find first tangent vector
    float tangent1X, tangent1Y, tangent1Z;
    if (std::abs(normalY) < 0.9f)
    {
        tangent1X = normalZ;
        tangent1Y = 0;
        tangent1Z = -normalX;
    }
    else
    {
        tangent1X = 0;
        tangent1Y = -normalZ;
        tangent1Z = normalY;
    }

    float t1Len = std::sqrt(tangent1X * tangent1X + tangent1Y * tangent1Y + tangent1Z * tangent1Z);
    tangent1X /= t1Len;
    tangent1Y /= t1Len;
    tangent1Z /= t1Len;

    // Second tangent = normal x tangent1
    float tangent2X = normalY * tangent1Z - normalZ * tangent1Y;
    float tangent2Y = normalZ * tangent1X - normalX * tangent1Z;
    float tangent2Z = normalX * tangent1Y - normalY * tangent1X;

    // Draw circle as line segments
    float angleStep = 2.0f * 3.14159265f / segments;
    for (int i = 0; i < segments; i++)
    {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;

        float x1 = centerX + radius * (std::cos(angle1) * tangent1X + std::sin(angle1) * tangent2X);
        float y1 = centerY + radius * (std::cos(angle1) * tangent1Y + std::sin(angle1) * tangent2Y);
        float z1 = centerZ + radius * (std::cos(angle1) * tangent1Z + std::sin(angle1) * tangent2Z);

        float x2 = centerX + radius * (std::cos(angle2) * tangent1X + std::sin(angle2) * tangent2X);
        float y2 = centerY + radius * (std::cos(angle2) * tangent1Y + std::sin(angle2) * tangent2Y);
        float z2 = centerZ + radius * (std::cos(angle2) * tangent1Z + std::sin(angle2) * tangent2Z);

        DrawLine(x1, y1, z1, x2, y2, z2, r, g, b, a);
    }
}

void GizmoRenderer::DrawBox(float centerX, float centerY, float centerZ, float sizeX, float sizeY, float sizeZ, float r,
                            float g, float b, float a)
{
    float hx = sizeX * 0.5f;
    float hy = sizeY * 0.5f;
    float hz = sizeZ * 0.5f;

    // 8 corners
    float corners[8][3] = {
        {centerX - hx, centerY - hy, centerZ - hz}, {centerX + hx, centerY - hy, centerZ - hz},
        {centerX + hx, centerY + hy, centerZ - hz}, {centerX - hx, centerY + hy, centerZ - hz},
        {centerX - hx, centerY - hy, centerZ + hz}, {centerX + hx, centerY - hy, centerZ + hz},
        {centerX + hx, centerY + hy, centerZ + hz}, {centerX - hx, centerY + hy, centerZ + hz},
    };

    // 12 edges
    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Bottom
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Top
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // Verticals
    };

    for (int i = 0; i < 12; i++)
    {
        int a_idx = edges[i][0];
        int b_idx = edges[i][1];
        DrawLine(corners[a_idx][0], corners[a_idx][1], corners[a_idx][2], corners[b_idx][0], corners[b_idx][1],
                 corners[b_idx][2], r, g, b, a);
    }
}

void GizmoRenderer::DrawAxisIndicator(float x, float y, float z, float size)
{
    // X axis - Red
    DrawArrow(x, y, z, 1, 0, 0, size, 1, 0, 0);
    // Y axis - Green
    DrawArrow(x, y, z, 0, 1, 0, size, 0, 1, 0);
    // Z axis - Blue
    DrawArrow(x, y, z, 0, 0, 1, size, 0, 0, 1);
}

void GizmoRenderer::DrawBillboardText(const Camera& camera, float x, float y, float z, const char* text, float height,
                                      float r, float g, float b, float a, bool centered)
{
    if (!text || !text[0] || height <= 0.0001f)
        return;

    float rightX = 0.0f, rightY = 0.0f, rightZ = 0.0f;
    float upX = 0.0f, upY = 0.0f, upZ = 0.0f;
    camera.GetRight(rightX, rightY, rightZ);
    camera.GetUp(upX, upY, upZ);
    Normalize3(rightX, rightY, rightZ);
    Normalize3(upX, upY, upZ);

    constexpr float glyphWidth = 5.0f;
    constexpr float glyphHeight = 7.0f;
    constexpr float glyphAdvance = 6.0f;
    const float cellSize = height / glyphHeight;

    const size_t textLength = std::strlen(text);
    if (textLength == 0)
        return;

    float originOffsetX = 0.0f;
    if (centered)
    {
        const float totalWidth = ((static_cast<float>(textLength) - 1.0f) * glyphAdvance + glyphWidth) * cellSize;
        originOffsetX = -totalWidth * 0.5f;
    }

    for (size_t charIndex = 0; charIndex < textLength; ++charIndex)
    {
        const unsigned char rawChar = static_cast<unsigned char>(text[charIndex]);
        const BitmapGlyph glyph = GetBitmapGlyph(static_cast<char>(std::toupper(rawChar)));
        const float glyphOriginX = originOffsetX + static_cast<float>(charIndex) * glyphAdvance * cellSize;

        for (int row = 0; row < 7; ++row)
        {
            int column = 0;
            while (column < 5)
            {
                if (!GlyphPixel(glyph, row, column))
                {
                    ++column;
                    continue;
                }

                const int runStart = column;
                while (column < 5 && GlyphPixel(glyph, row, column))
                    ++column;
                const int runEnd = column - 1;

                const float localY = (glyphHeight - (static_cast<float>(row) + 0.5f)) * cellSize;
                const float startLocalX = glyphOriginX + (static_cast<float>(runStart) + 0.15f) * cellSize;
                const float endLocalX = glyphOriginX + (static_cast<float>(runEnd) + 0.85f) * cellSize;

                float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
                float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
                BillboardPoint(x, y, z, rightX, rightY, rightZ, upX, upY, upZ, startLocalX, localY, x1, y1, z1);
                BillboardPoint(x, y, z, rightX, rightY, rightZ, upX, upY, upZ, endLocalX, localY, x2, y2, z2);
                DrawLine(x1, y1, z1, x2, y2, z2, r, g, b, a);
            }
        }

        for (int column = 0; column < 5; ++column)
        {
            int row = 0;
            while (row < 7)
            {
                if (!GlyphPixel(glyph, row, column))
                {
                    ++row;
                    continue;
                }

                const int runStart = row;
                while (row < 7 && GlyphPixel(glyph, row, column))
                    ++row;
                const int runEnd = row - 1;

                const float localX = glyphOriginX + (static_cast<float>(column) + 0.5f) * cellSize;
                const float startLocalY = (glyphHeight - (static_cast<float>(runStart) + 0.15f)) * cellSize;
                const float endLocalY = (glyphHeight - (static_cast<float>(runEnd) + 0.85f)) * cellSize;

                float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
                float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
                BillboardPoint(x, y, z, rightX, rightY, rightZ, upX, upY, upZ, localX, startLocalY, x1, y1, z1);
                BillboardPoint(x, y, z, rightX, rightY, rightZ, upX, upY, upZ, localX, endLocalY, x2, y2, z2);
                DrawLine(x1, y1, z1, x2, y2, z2, r, g, b, a);
            }
        }
    }
}

void GizmoRenderer::End(const Camera& camera, RHISwapChain* swapChain)
{
    if (!m_Initialized || m_Vertices.empty() || !swapChain)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);

    // Upload vertex data
    void* vbData = m_VertexBuffer->Map();
    if (vbData)
    {
        memcpy(vbData, m_Vertices.data(), m_Vertices.size() * sizeof(GizmoVertex));
        m_VertexBuffer->Unmap();
    }

    // Upload MVP matrix
    float* cbData = reinterpret_cast<float*>(m_ConstantBuffer->Map());
    if (cbData)
    {
        memcpy(cbData, camera.GetViewProjectionMatrix(), 16 * sizeof(float));
        m_ConstantBuffer->Unmap();
    }

    // Set viewport
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(swapChain->GetWidth());
    viewport.Height = static_cast<float>(swapChain->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    // Set scissor rect
    D3D12_RECT scissor = {};
    scissor.right = static_cast<LONG>(swapChain->GetWidth());
    scissor.bottom = static_cast<LONG>(swapChain->GetHeight());
    cmdList->RSSetScissorRects(1, &scissor);

    // Set render targets
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Set pipeline state
    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_RootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(m_PipelineState));

    // Set constant buffer
    cmdList->SetGraphicsRootConstantBufferView(0, GetD3D12BufferGPUAddress(m_ConstantBuffer.get()));

    // Set vertex buffer
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = GetD3D12BufferGPUAddress(m_VertexBuffer.get());
    vbView.SizeInBytes = static_cast<UINT>(m_Vertices.size() * sizeof(GizmoVertex));
    vbView.StrideInBytes = sizeof(GizmoVertex);
    cmdList->IASetVertexBuffers(0, 1, &vbView);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    // Draw all lines
    cmdList->DrawInstanced(static_cast<UINT>(m_Vertices.size()), 1, 0, 0);
}

} // namespace Dot
