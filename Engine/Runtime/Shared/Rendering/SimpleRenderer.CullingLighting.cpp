// =============================================================================
// Dot Engine - Simple Renderer Culling + Lighting Implementation
// =============================================================================

#include "SimpleRenderer.h"
#include "SimpleRendererGraphPasses.h"
#include "ShaderCompiler.h"
#include "D3D12/D3D12SwapChain.h"

#include <Core/Math/Mat4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);

namespace Dot
{

namespace
{

template <typename T>
void ReleaseIfSet(T*& ptr)
{
    if (ptr)
    {
        ptr->Release();
        ptr = nullptr;
    }
}

float ReadHZBDepthSample(const OcclusionState& occlusion, uint32_t mipLevel, float u, float v)
{
    if (!occlusion.readbackValid || mipLevel >= occlusion.mipReadback.size() || occlusion.readbackData.empty())
        return 1.0f;

    const HZBMipReadbackInfo& info = occlusion.mipReadback[mipLevel];
    if (info.width == 0 || info.height == 0)
        return 1.0f;

    const uint32_t x =
        std::min<uint32_t>(static_cast<uint32_t>(std::clamp(u, 0.0f, 0.999999f) * static_cast<float>(info.width)),
                           info.width - 1);
    const uint32_t y =
        std::min<uint32_t>(static_cast<uint32_t>(std::clamp(v, 0.0f, 0.999999f) * static_cast<float>(info.height)),
                           info.height - 1);

    const size_t byteOffset = static_cast<size_t>(info.offset) + static_cast<size_t>(y) * info.rowPitch +
                              static_cast<size_t>(x) * sizeof(float);
    if (byteOffset + sizeof(float) > occlusion.readbackData.size())
        return 1.0f;

    float value = 1.0f;
    std::memcpy(&value, occlusion.readbackData.data() + byteOffset, sizeof(float));
    return value;
}

float ReadHZBDepthRegionMax(const OcclusionState& occlusion, uint32_t mipLevel, float minU, float minV, float maxU,
                            float maxV)
{
    if (!occlusion.readbackValid || mipLevel >= occlusion.mipReadback.size() || occlusion.readbackData.empty())
        return 1.0f;

    const HZBMipReadbackInfo& info = occlusion.mipReadback[mipLevel];
    if (info.width == 0 || info.height == 0)
        return 1.0f;

    const float clampedMinU = std::clamp(minU, 0.0f, 0.999999f);
    const float clampedMinV = std::clamp(minV, 0.0f, 0.999999f);
    const float clampedMaxU = std::clamp(maxU, 0.0f, 0.999999f);
    const float clampedMaxV = std::clamp(maxV, 0.0f, 0.999999f);

    const uint32_t minX =
        std::min<uint32_t>(static_cast<uint32_t>(clampedMinU * static_cast<float>(info.width)), info.width - 1);
    const uint32_t minY =
        std::min<uint32_t>(static_cast<uint32_t>(clampedMinV * static_cast<float>(info.height)), info.height - 1);
    const uint32_t maxX =
        std::min<uint32_t>(static_cast<uint32_t>(clampedMaxU * static_cast<float>(info.width)), info.width - 1);
    const uint32_t maxY =
        std::min<uint32_t>(static_cast<uint32_t>(clampedMaxV * static_cast<float>(info.height)), info.height - 1);

    float maxDepth = 0.0f;
    for (uint32_t y = minY; y <= maxY; ++y)
    {
        for (uint32_t x = minX; x <= maxX; ++x)
        {
            const size_t byteOffset = static_cast<size_t>(info.offset) + static_cast<size_t>(y) * info.rowPitch +
                                      static_cast<size_t>(x) * sizeof(float);
            if (byteOffset + sizeof(float) > occlusion.readbackData.size())
                continue;

            float value = 1.0f;
            std::memcpy(&value, occlusion.readbackData.data() + byteOffset, sizeof(float));
            maxDepth = std::max(maxDepth, value);
        }
    }

    return maxDepth;
}

float ComputePerspectiveDepth01(const Camera& camera, float viewZ)
{
    const float nearZ = std::max(camera.GetNearZ(), 0.0001f);
    const float farZ = std::max(camera.GetFarZ(), nearZ + 0.0001f);
    const float clampedViewZ = std::clamp(viewZ, nearZ, farZ);
    const float depth = (farZ / (farZ - nearZ)) - ((nearZ * farZ) / ((farZ - nearZ) * clampedViewZ));
    return std::clamp(depth, 0.0f, 1.0f);
}

} // namespace

bool SimpleRenderer::CreateHZBResources(uint32_t width, uint32_t height)
{
    if (m_Occlusion.hzbTexture && m_Occlusion.width == width && m_Occlusion.height == height)
    {
        RefreshGraphInteropTextures();
        return true;
    }

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice || width == 0 || height == 0)
        return false;

    ReleaseIfSet(reinterpret_cast<ID3D12Resource*&>(m_Occlusion.hzbTexture));
    ReleaseIfSet(reinterpret_cast<ID3D12DescriptorHeap*&>(m_Occlusion.hzbSrvHeap));
    ReleaseIfSet(reinterpret_cast<ID3D12DescriptorHeap*&>(m_Occlusion.hzbUavHeap));
    ReleaseIfSet(reinterpret_cast<ID3D12Resource*&>(m_Occlusion.hzbReadbackBuffer));
    ReleaseIfSet(reinterpret_cast<ID3D12PipelineState*&>(m_Occlusion.hzbDownsamplePSO));
    ReleaseIfSet(reinterpret_cast<ID3D12RootSignature*&>(m_Occlusion.hzbRootSignature));
    m_Occlusion.mipReadback.clear();
    m_Occlusion.readbackData.clear();
    m_Occlusion.readbackValid = false;

    m_Occlusion.width = width;
    m_Occlusion.height = height;
    m_Occlusion.mipLevels = 1;
    for (uint32_t w = width, h = height; w > 1 || h > 1;)
    {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        ++m_Occlusion.mipLevels;
    }
    m_Occlusion.mipLevels = std::min<uint32_t>(m_Occlusion.mipLevels, 12u);

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = static_cast<UINT16>(m_Occlusion.mipLevels);
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ID3D12Resource* hzbTexture = nullptr;
    HRESULT hr = d3dDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&hzbTexture));
    if (FAILED(hr))
        return false;
    m_Occlusion.hzbTexture = hzbTexture;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = m_Occlusion.mipLevels * 2;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ID3D12DescriptorHeap* combinedHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&combinedHeap));
    if (FAILED(hr))
        return false;
    m_Occlusion.hzbSrvHeap = combinedHeap;
    m_Occlusion.hzbUavHeap = nullptr;

    const UINT descriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = combinedHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t mip = 0; mip < m_Occlusion.mipLevels; ++mip)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = mip;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE mipSrvHandle = cpuHandle;
        mipSrvHandle.ptr += static_cast<SIZE_T>(mip) * descriptorSize;
        d3dDevice->CreateShaderResourceView(hzbTexture, &srvDesc, mipSrvHandle);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mip;

        D3D12_CPU_DESCRIPTOR_HANDLE mipUavHandle = cpuHandle;
        mipUavHandle.ptr += static_cast<SIZE_T>(m_Occlusion.mipLevels + mip) * descriptorSize;
        d3dDevice->CreateUnorderedAccessView(hzbTexture, nullptr, &uavDesc, mipUavHandle);
    }

    UINT64 totalBytes = 0;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(m_Occlusion.mipLevels);
    std::vector<UINT> numRows(m_Occlusion.mipLevels);
    std::vector<UINT64> rowSizes(m_Occlusion.mipLevels);
    d3dDevice->GetCopyableFootprints(&texDesc, 0, m_Occlusion.mipLevels, 0, footprints.data(), numRows.data(),
                                     rowSizes.data(), &totalBytes);

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* readbackBuffer = nullptr;
    hr = d3dDevice->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuffer));
    if (FAILED(hr))
        return false;
    m_Occlusion.hzbReadbackBuffer = readbackBuffer;

    m_Occlusion.mipReadback.resize(m_Occlusion.mipLevels);
    m_Occlusion.readbackData.resize(static_cast<size_t>(totalBytes));
    for (uint32_t mip = 0; mip < m_Occlusion.mipLevels; ++mip)
    {
        m_Occlusion.mipReadback[mip].offset = footprints[mip].Offset;
        m_Occlusion.mipReadback[mip].rowPitch = footprints[mip].Footprint.RowPitch;
        m_Occlusion.mipReadback[mip].width = footprints[mip].Footprint.Width;
        m_Occlusion.mipReadback[mip].height = footprints[mip].Footprint.Height;
    }

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.Num32BitValues = 4;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> sigBlob;
    ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    if (FAILED(hr))
        return false;

    ID3D12RootSignature* rootSignature = nullptr;
    hr = d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                        IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr))
        return false;
    m_Occlusion.hzbRootSignature = rootSignature;

    std::vector<uint8_t> csBytecode;
    auto& compiler = ShaderCompiler::Get();
    if (const CompiledShader* shader = compiler.CompileFromFile("Culling/HZBDownsampleCS.hlsl", "CSMain",
                                                                ShaderType::Compute))
    {
        csBytecode = shader->bytecode;
    }
    else
    {
        const char* fallbackHzbCS = R"(
            cbuffer Constants : register(b0) { uint srcMip; uint dstWidth; uint dstHeight; uint pad; };
            Texture2D<float> srcTex : register(t0);
            RWTexture2D<float> dstTex : register(u0);
            SamplerState pointSampler : register(s0);
            [numthreads(8, 8, 1)]
            void CSMain(uint3 id : SV_DispatchThreadID) {
                if (id.x >= dstWidth || id.y >= dstHeight) return;
                float2 srcSize = float2(dstWidth * 2, dstHeight * 2);
                float2 uv = (float2(id.xy) * 2.0 + 0.5) / srcSize;
                float2 texelSize = 1.0 / srcSize;
                float d00 = srcTex.SampleLevel(pointSampler, uv, 0);
                float d10 = srcTex.SampleLevel(pointSampler, uv + float2(texelSize.x, 0), 0);
                float d01 = srcTex.SampleLevel(pointSampler, uv + float2(0, texelSize.y), 0);
                float d11 = srcTex.SampleLevel(pointSampler, uv + texelSize, 0);
                dstTex[id.xy] = max(max(d00, d10), max(d01, d11));
            }
        )";
        ComPtr<ID3DBlob> csBlob;
        hr = D3DCompile(fallbackHzbCS, std::strlen(fallbackHzbCS), "HZBDownsampleCS", nullptr, nullptr, "CSMain",
                        "cs_5_0", 0, 0, &csBlob, &errorBlob);
        if (FAILED(hr))
            return false;
        csBytecode.assign(static_cast<uint8_t*>(csBlob->GetBufferPointer()),
                          static_cast<uint8_t*>(csBlob->GetBufferPointer()) + csBlob->GetBufferSize());
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.CS.pShaderBytecode = csBytecode.data();
    psoDesc.CS.BytecodeLength = csBytecode.size();

    ID3D12PipelineState* downsamplePSO = nullptr;
    hr = d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&downsamplePSO));
    if (FAILED(hr))
        return false;
    m_Occlusion.hzbDownsamplePSO = downsamplePSO;

    RefreshGraphInteropTextures();

    return true;
}

void SimpleRenderer::GenerateHZB(RHISwapChain* swapChain)
{
    HZBGraphPassExecutor::Execute(*this, swapChain);
}

bool SimpleRenderer::TestHZBOcclusion(const Camera& camera, float minX, float minY, float minZ, float maxX, float maxY,
                                      float maxZ)
{
    if (!m_Occlusion.enabled || !m_Occlusion.hzbTexture || !m_Occlusion.readbackValid)
        return false;

    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;
    const float extentX = std::max(0.0f, maxX - minX);
    const float extentY = std::max(0.0f, maxY - minY);
    const float extentZ = std::max(0.0f, maxZ - minZ);
    const float radius = 0.5f * std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);
    if (radius <= 0.0001f)
        return false;

    const float* view = camera.GetViewMatrix();
    const float viewCenterX = view[0] * centerX + view[4] * centerY + view[8] * centerZ + view[12];
    const float viewCenterY = view[1] * centerX + view[5] * centerY + view[9] * centerZ + view[13];
    const float viewCenterZ = view[2] * centerX + view[6] * centerY + view[10] * centerZ + view[14];
    const float nearZ = std::max(camera.GetNearZ(), 0.0001f);
    if (viewCenterZ <= nearZ)
        return false;

    const float* projection = camera.GetProjectionMatrix();
    const float invCenterZ = 1.0f / viewCenterZ;
    const float ndcCenterX = projection[0] * viewCenterX * invCenterZ;
    const float ndcCenterY = projection[5] * viewCenterY * invCenterZ;
    const float screenCenterX = (ndcCenterX + 1.0f) * 0.5f;
    const float screenCenterY = (1.0f - ndcCenterY) * 0.5f;

    const float radiusDepth = std::max(viewCenterZ - radius, nearZ);
    const float screenRadiusX = 0.5f * std::abs(projection[0]) * radius / radiusDepth;
    const float screenRadiusY = 0.5f * std::abs(projection[5]) * radius / radiusDepth;

    float screenMinX = screenCenterX - screenRadiusX;
    float screenMinY = screenCenterY - screenRadiusY;
    float screenMaxX = screenCenterX + screenRadiusX;
    float screenMaxY = screenCenterY + screenRadiusY;

    if (screenMaxX < 0.0f || screenMinX > 1.0f || screenMaxY < 0.0f || screenMinY > 1.0f)
        return false;

    screenMinX = std::clamp(screenMinX, 0.0f, 1.0f);
    screenMinY = std::clamp(screenMinY, 0.0f, 1.0f);
    screenMaxX = std::clamp(screenMaxX, 0.0f, 1.0f);
    screenMaxY = std::clamp(screenMaxY, 0.0f, 1.0f);

    const float normalizedWidth = screenMaxX - screenMinX;
    const float normalizedHeight = screenMaxY - screenMinY;
    if (normalizedWidth <= 0.0f || normalizedHeight <= 0.0f)
        return false;

    // Corner-projected depth becomes unreliable for very large boxes; fail open instead of risking whole-scene pops.
    if (normalizedWidth * normalizedHeight > 0.35f || normalizedWidth > 0.75f || normalizedHeight > 0.75f)
        return false;

    const float closestDepth = ComputePerspectiveDepth01(camera, radiusDepth);
    const float rectWidth = (screenMaxX - screenMinX) * static_cast<float>(m_Occlusion.width);
    const float rectHeight = (screenMaxY - screenMinY) * static_cast<float>(m_Occlusion.height);
    const float maxDim = std::max(rectWidth, rectHeight);
    if (maxDim <= 0.5f)
        return false;

    uint32_t mipLevel = 0;
    if (maxDim > 1.0f)
    {
        mipLevel = std::min<uint32_t>(static_cast<uint32_t>(std::floor(std::log2(maxDim))), m_Occlusion.mipLevels - 1);
    }
    if (mipLevel > 0)
        --mipLevel;

    auto computeCoveredTexelCount = [this, screenMinX, screenMinY, screenMaxX, screenMaxY](uint32_t mip) -> uint32_t
    {
        if (mip >= m_Occlusion.mipReadback.size())
            return 0;

        const HZBMipReadbackInfo& info = m_Occlusion.mipReadback[mip];
        if (info.width == 0 || info.height == 0)
            return 0;

        const uint32_t minTexX =
            std::min<uint32_t>(static_cast<uint32_t>(screenMinX * static_cast<float>(info.width)), info.width - 1);
        const uint32_t minTexY =
            std::min<uint32_t>(static_cast<uint32_t>(screenMinY * static_cast<float>(info.height)), info.height - 1);
        const uint32_t maxTexX =
            std::min<uint32_t>(static_cast<uint32_t>(screenMaxX * static_cast<float>(info.width)), info.width - 1);
        const uint32_t maxTexY =
            std::min<uint32_t>(static_cast<uint32_t>(screenMaxY * static_cast<float>(info.height)), info.height - 1);
        return (maxTexX - minTexX + 1u) * (maxTexY - minTexY + 1u);
    };

    while (mipLevel + 1 < m_Occlusion.mipLevels && computeCoveredTexelCount(mipLevel) > 64u)
        ++mipLevel;

    const float maxDepthSample =
        ReadHZBDepthRegionMax(m_Occlusion, mipLevel, screenMinX, screenMinY, screenMaxX, screenMaxY);

    // Use a small view-space slack converted into depth-space instead of a fixed depth epsilon.
    // A constant 0.01f is far too large once depth becomes non-linear near the far plane and
    // effectively disables occlusion at distance.
    const float worldDepthSlack = std::clamp(radius * 0.05f, 0.05f, 0.25f);
    const float depthEpsilon =
        ComputePerspectiveDepth01(camera, std::min(viewCenterZ, camera.GetFarZ())) -
        ComputePerspectiveDepth01(camera, std::min(viewCenterZ - worldDepthSlack, camera.GetFarZ()));

    return closestDepth > (maxDepthSample + std::max(depthEpsilon, 0.000001f));
}

bool SimpleRenderer::CreateForwardPlusResources(uint32_t screenWidth, uint32_t screenHeight)
{
    if (m_LightBuffer && m_LightIndexBuffer && m_LightGridBuffer && m_LightCullPSO)
        return true;

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice || screenWidth == 0 || screenHeight == 0)
        return false;

    m_TileCountX = (screenWidth + TILE_SIZE - 1) / TILE_SIZE;
    m_TileCountY = (screenHeight + TILE_SIZE - 1) / TILE_SIZE;
    const uint32_t totalTiles = m_TileCountX * m_TileCountY;

    if (!m_LightIndexBuffer)
    {
        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(totalTiles) * MAX_LIGHTS_PER_TILE * sizeof(uint32_t);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON,
                                           nullptr,
                                           IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_LightIndexBuffer)));
    }

    if (!m_LightGridBuffer)
    {
        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(totalTiles) * 2 * sizeof(uint32_t);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON,
                                           nullptr,
                                           IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_LightGridBuffer)));
    }

    if (!m_LightBuffer)
    {
        const uint32_t maxLights = SceneLightData::MAX_POINT_LIGHTS + SceneLightData::MAX_SPOT_LIGHTS;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(maxLights) * 16 * sizeof(float);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                           IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_LightBuffer)));
    }

    if (!m_LightCullRootSig)
    {
        D3D12_ROOT_PARAMETER rootParams[5] = {};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.Num32BitValues = 24;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE depthRange = {};
        depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        depthRange.NumDescriptors = 1;
        depthRange.BaseShaderRegister = 0;
        depthRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &depthRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE lightRange = {};
        lightRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        lightRange.NumDescriptors = 1;
        lightRange.BaseShaderRegister = 1;
        lightRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[2].DescriptorTable.pDescriptorRanges = &lightRange;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE gridRange = {};
        gridRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        gridRange.NumDescriptors = 1;
        gridRange.BaseShaderRegister = 0;
        gridRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[3].DescriptorTable.pDescriptorRanges = &gridRange;
        rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE indexRange = {};
        indexRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        indexRange.NumDescriptors = 1;
        indexRange.BaseShaderRegister = 1;
        indexRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[4].DescriptorTable.pDescriptorRanges = &indexRange;
        rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 5;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 1;
        rootSigDesc.pStaticSamplers = &sampler;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr))
            return false;

        ID3D12RootSignature* rootSignature = nullptr;
        hr = d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&rootSignature));
        if (FAILED(hr))
            return false;
        m_LightCullRootSig = rootSignature;
    }

    if (!m_LightCullPSO && m_LightCullRootSig)
    {
        std::vector<uint8_t> csBytecode;
        auto& compiler = ShaderCompiler::Get();
        if (const CompiledShader* shader = compiler.CompileFromFile("Culling/ForwardPlusLightCullCS.hlsl", "CSMain",
                                                                    ShaderType::Compute))
        {
            csBytecode = shader->bytecode;
        }
        else
        {
            const char* fallbackCS = R"(
                cbuffer Constants : register(b0) {
                    float4x4 invProj; uint tileCountX; uint tileCountY; uint screenWidth; uint screenHeight;
                    uint numLights; uint pad0; uint pad1; uint pad2;
                };
                struct Light {
                    float3 position; float range; float3 color; float intensity;
                    uint type; float3 direction; float spotAngle;
                    float shadowEnabled; float shadowBaseSlice; float shadowBias;
                };
                Texture2D<float> depthTex : register(t0);
                StructuredBuffer<Light> lights : register(t1);
                RWBuffer<uint> lightGrid : register(u0);
                RWBuffer<uint> lightIndices : register(u1);
                SamplerState pointSampler : register(s0);
                #define TILE_SIZE 16
                #define MAX_LIGHTS_PER_TILE 256
                groupshared uint minDepthInt;
                groupshared uint maxDepthInt;
                groupshared uint visibleLightCount;
                groupshared uint visibleLightIndices[MAX_LIGHTS_PER_TILE];
                [numthreads(TILE_SIZE, TILE_SIZE, 1)]
                void CSMain(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
                    if (groupIndex == 0) { minDepthInt = 0xFFFFFFFF; maxDepthInt = 0; visibleLightCount = 0; }
                    GroupMemoryBarrierWithGroupSync();
                    uint2 pixelPos = groupId.xy * TILE_SIZE + threadId.xy;
                    float2 uv = (float2(pixelPos) + 0.5) / float2(screenWidth, screenHeight);
                    float depth = depthTex.SampleLevel(pointSampler, uv, 0);
                    uint depthInt = asuint(depth);
                    InterlockedMin(minDepthInt, depthInt);
                    InterlockedMax(maxDepthInt, depthInt);
                    GroupMemoryBarrierWithGroupSync();
                    float minDepth = asfloat(minDepthInt);
                    float maxDepth = asfloat(maxDepthInt);
                    uint lightsPerThread = (numLights + 255) / 256;
                    uint startLight = groupIndex * lightsPerThread;
                    uint endLight = min(startLight + lightsPerThread, numLights);
                    for (uint i = startLight; i < endLight; ++i) {
                        Light light = lights[i];
                        float lightDepth = light.position.z;
                        if (lightDepth - light.range < maxDepth && lightDepth + light.range > minDepth) {
                            uint slot; InterlockedAdd(visibleLightCount, 1, slot);
                            if (slot < MAX_LIGHTS_PER_TILE) visibleLightIndices[slot] = i;
                        }
                    }
                    GroupMemoryBarrierWithGroupSync();
                    if (groupIndex == 0) {
                        uint tileIndex = groupId.y * tileCountX + groupId.x;
                        uint offset = tileIndex * MAX_LIGHTS_PER_TILE;
                        uint count = min(visibleLightCount, MAX_LIGHTS_PER_TILE);
                        lightGrid[tileIndex * 2] = offset;
                        lightGrid[tileIndex * 2 + 1] = count;
                        for (uint i = 0; i < count; ++i) lightIndices[offset + i] = visibleLightIndices[i];
                    }
                }
            )";
            ComPtr<ID3DBlob> csBlob;
            ComPtr<ID3DBlob> errorBlob;
            HRESULT hr =
                D3DCompile(fallbackCS, std::strlen(fallbackCS), "ForwardPlusLightCullCS", nullptr, nullptr, "CSMain",
                           "cs_5_0", 0, 0, &csBlob, &errorBlob);
            if (FAILED(hr))
                return false;
            csBytecode.assign(static_cast<uint8_t*>(csBlob->GetBufferPointer()),
                              static_cast<uint8_t*>(csBlob->GetBufferPointer()) + csBlob->GetBufferSize());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>(m_LightCullRootSig);
        psoDesc.CS.pShaderBytecode = csBytecode.data();
        psoDesc.CS.BytecodeLength = csBytecode.size();

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
            return false;
        m_LightCullPSO = pso;
    }

    return m_LightBuffer && m_LightIndexBuffer && m_LightGridBuffer && m_LightCullPSO;
}

void SimpleRenderer::CullLights(const Camera& camera, RHISwapChain* swapChain)
{
    if (!m_ForwardPlusEnabled || !m_LightCullPSO || !m_LightCullRootSig || !swapChain || !m_LightBuffer)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!cmdList || !d3dDevice)
        return;

    struct GPULight
    {
        float posX, posY, posZ;
        float range;
        float colorR, colorG, colorB;
        float intensity;
        uint32_t type;
        float dirX, dirY, dirZ;
        float spotAngle;
        float shadowEnabled;
        float shadowBaseSlice;
        float shadowBias;
    };

    const uint32_t numLights = static_cast<uint32_t>(m_LightData.numPointLights + m_LightData.numSpotLights);
    if (numLights == 0)
        return;

    ID3D12Resource* lightBuffer = reinterpret_cast<ID3D12Resource*>(m_LightBuffer);
    void* mappedData = nullptr;
    if (SUCCEEDED(lightBuffer->Map(0, nullptr, &mappedData)) && mappedData)
    {
        GPULight* gpuLights = static_cast<GPULight*>(mappedData);
        uint32_t lightIndex = 0;
        for (int i = 0; i < m_LightData.numPointLights && i < SceneLightData::MAX_POINT_LIGHTS; ++i)
        {
            const auto& light = m_LightData.pointLights[i];
            gpuLights[lightIndex] = {light.posX, light.posY, light.posZ, light.range, light.colorR, light.colorG,
                                     light.colorB, light.intensity, 0, 0.0f, 0.0f, 0.0f, light.shadowEnabled,
                                     light.shadowBaseSlice, light.shadowBias};
            ++lightIndex;
        }
        for (int i = 0; i < m_LightData.numSpotLights && i < SceneLightData::MAX_SPOT_LIGHTS; ++i)
        {
            const auto& light = m_LightData.spotLights[i];
            gpuLights[lightIndex] = {light.posX,  light.posY,  light.posZ,  light.range, light.colorR,
                                     light.colorG, light.colorB, light.intensity, 1, light.dirX,
                                     light.dirY,  light.dirZ,  light.outerCos, light.shadowEnabled,
                                     light.shadowBaseSlice, light.shadowBias};
            ++lightIndex;
        }
        lightBuffer->Unmap(0, nullptr);
    }

    if (!m_ForwardPlusSRVHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 4;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ID3D12DescriptorHeap* heap = nullptr;
        d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
        m_ForwardPlusSRVHeap = heap;
        if (!m_ForwardPlusSRVHeap)
            return;

        const UINT descriptorSize =
            d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            reinterpret_cast<ID3D12DescriptorHeap*>(m_ForwardPlusSRVHeap)->GetCPUDescriptorHandleForHeapStart();

        if (swapChain->GetDepthSRV())
        {
            D3D12_CPU_DESCRIPTOR_HANDLE depthSrc =
                reinterpret_cast<ID3D12DescriptorHeap*>(swapChain->GetDepthSRV())->GetCPUDescriptorHandleForHeapStart();
            d3dDevice->CopyDescriptorsSimple(1, handle, depthSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        handle.ptr += descriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC lightSrvDesc = {};
        lightSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        lightSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        lightSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        lightSrvDesc.Buffer.NumElements = SceneLightData::MAX_POINT_LIGHTS + SceneLightData::MAX_SPOT_LIGHTS;
        lightSrvDesc.Buffer.StructureByteStride = sizeof(GPULight);
        d3dDevice->CreateShaderResourceView(lightBuffer, &lightSrvDesc, handle);
        handle.ptr += descriptorSize;

        D3D12_UNORDERED_ACCESS_VIEW_DESC gridUavDesc = {};
        gridUavDesc.Format = DXGI_FORMAT_R32_UINT;
        gridUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        gridUavDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * 2;
        d3dDevice->CreateUnorderedAccessView(reinterpret_cast<ID3D12Resource*>(m_LightGridBuffer), nullptr,
                                             &gridUavDesc, handle);
        handle.ptr += descriptorSize;

        D3D12_UNORDERED_ACCESS_VIEW_DESC indexUavDesc = {};
        indexUavDesc.Format = DXGI_FORMAT_R32_UINT;
        indexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        indexUavDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * MAX_LIGHTS_PER_TILE;
        d3dDevice->CreateUnorderedAccessView(reinterpret_cast<ID3D12Resource*>(m_LightIndexBuffer), nullptr,
                                             &indexUavDesc, handle);
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = reinterpret_cast<ID3D12Resource*>(m_LightGridBuffer);
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = reinterpret_cast<ID3D12Resource*>(m_LightIndexBuffer);
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(2, barriers);

    struct CullConstants
    {
        float invProj[16];
        uint32_t tileCountX;
        uint32_t tileCountY;
        uint32_t screenWidth;
        uint32_t screenHeight;
        uint32_t numLights;
        uint32_t pad[3];
    } constants = {};

    Mat4 projection;
    std::memcpy(projection.Data(), camera.GetProjectionMatrix(), sizeof(float) * 16);
    Mat4 inverseProjection = projection.Inverted();
    std::memcpy(constants.invProj, inverseProjection.Data(), sizeof(float) * 16);
    constants.tileCountX = m_TileCountX;
    constants.tileCountY = m_TileCountY;
    constants.screenWidth = swapChain->GetWidth();
    constants.screenHeight = swapChain->GetHeight();
    constants.numLights = numLights;

    cmdList->SetComputeRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_LightCullRootSig));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(m_LightCullPSO));
    cmdList->SetComputeRoot32BitConstants(0, 24, &constants, 0);

    ID3D12DescriptorHeap* heaps[] = {reinterpret_cast<ID3D12DescriptorHeap*>(m_ForwardPlusSRVHeap)};
    cmdList->SetDescriptorHeaps(1, heaps);

    const UINT descriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heaps[0]->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetComputeRootDescriptorTable(1, gpuHandle);
    gpuHandle.ptr += descriptorSize;
    cmdList->SetComputeRootDescriptorTable(2, gpuHandle);
    gpuHandle.ptr += descriptorSize;
    cmdList->SetComputeRootDescriptorTable(3, gpuHandle);
    gpuHandle.ptr += descriptorSize;
    cmdList->SetComputeRootDescriptorTable(4, gpuHandle);

    cmdList->Dispatch(m_TileCountX, m_TileCountY, 1);

    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(2, barriers);
}

} // namespace Dot
