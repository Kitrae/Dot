// =============================================================================
// Dot Engine - Simple Renderer Post-Process Implementation
// =============================================================================

#include "SimpleRenderer.h"
#include "SimpleRendererGraphPasses.h"
#include "ShaderCompiler.h"
#include "D3D12/D3D12SwapChain.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using Microsoft::WRL::ComPtr;

extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);
extern ID3D12Resource* GetD3D12Buffer(Dot::RHIBuffer* buffer);
extern D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer);

namespace Dot
{
bool SimpleRenderer::CreateFXAAResources(uint32_t width, uint32_t height)
{
    if (m_FXAAWidth == width && m_FXAAHeight == height && m_FXAAIntermediateRT)
        return true; // Already created at this size

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice)
        return false;

    // Release old resources
    if (m_FXAAIntermediateRT)
    {
        reinterpret_cast<ID3D12Resource*>(m_FXAAIntermediateRT)->Release();
        m_FXAAIntermediateRT = nullptr;
    }
    if (m_FXAARTVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAARTVHeap)->Release();
        m_FXAARTVHeap = nullptr;
    }
    if (m_FXAASRVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAASRVHeap)->Release();
        m_FXAASRVHeap = nullptr;
    }

    m_FXAAWidth = width;
    m_FXAAHeight = height;

    // Create intermediate render target
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rtDesc = {};
    rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width = width;
    rtDesc.Height = height;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels = 1;
    rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    ID3D12Resource* rt = nullptr;
    HRESULT hr =
        d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
                                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&rt));
    if (FAILED(hr))
        return false;
    m_FXAAIntermediateRT = rt;

    // Create RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr))
        return false;
    m_FXAARTVHeap = rtvHeap;
    d3dDevice->CreateRenderTargetView(rt, nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create SRV heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
    if (FAILED(hr))
        return false;
    m_FXAASRVHeap = srvHeap;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    d3dDevice->CreateShaderResourceView(rt, &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart());

    auto& compiler = ShaderCompiler::Get();

    // Create FXAA shaders if not already done
    if (m_FXAAVSBytecode.empty())
    {
        const CompiledShader* fileVS =
            compiler.CompileFromFile("PostProcess/FullscreenVS.hlsl", "main", ShaderType::Vertex);
        if (fileVS)
        {
            m_FXAAVSBytecode = fileVS->bytecode;
        }
        else
        {
            const char* fxaaVS = R"(
            struct VSOutput {
                float4 Position : SV_POSITION;
                float2 UV : TEXCOORD0;
            };
            VSOutput main(uint vertexID : SV_VertexID) {
                VSOutput output;
                // Full-screen triangle
                output.UV = float2((vertexID << 1) & 2, vertexID & 2);
                output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
                return output;
            }
        )";

            ComPtr<ID3DBlob> vsBlob, errorBlob;
            hr = D3DCompile(fxaaVS, strlen(fxaaVS), "FXAAVS", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob,
                            &errorBlob);
            if (FAILED(hr))
            {
                std::printf("FXAA VS compile error: %s\n", errorBlob ? (char*)errorBlob->GetBufferPointer() : "Unknown");
                return false;
            }
            m_FXAAVSBytecode.assign((uint8_t*)vsBlob->GetBufferPointer(),
                                    (uint8_t*)vsBlob->GetBufferPointer() + vsBlob->GetBufferSize());
        }
    }

    if (m_FXAAPSBytecode.empty())
    {
        const CompiledShader* filePS = compiler.CompileFromFile("PostProcess/FXAA.hlsl", "main", ShaderType::Pixel);
        if (filePS)
        {
            m_FXAAPSBytecode = filePS->bytecode;
        }
        else
        {
            const char* fxaaPS = R"(
            Texture2D SceneTexture : register(t0);
            SamplerState LinearSampler : register(s0);
            
            cbuffer FXAAParams : register(b0) {
                float2 TexelSize;
                float SubpixelQuality;
                float EdgeThreshold;
            };
            
            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
                float3 rgbNW = SceneTexture.Sample(LinearSampler, uv + float2(-1, -1) * TexelSize).rgb;
                float3 rgbNE = SceneTexture.Sample(LinearSampler, uv + float2(1, -1) * TexelSize).rgb;
                float3 rgbSW = SceneTexture.Sample(LinearSampler, uv + float2(-1, 1) * TexelSize).rgb;
                float3 rgbSE = SceneTexture.Sample(LinearSampler, uv + float2(1, 1) * TexelSize).rgb;
                float3 rgbM = SceneTexture.Sample(LinearSampler, uv).rgb;
                
                float3 luma = float3(0.299, 0.587, 0.114);
                float lumaNW = dot(rgbNW, luma);
                float lumaNE = dot(rgbNE, luma);
                float lumaSW = dot(rgbSW, luma);
                float lumaSE = dot(rgbSE, luma);
                float lumaM = dot(rgbM, luma);
                
                float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
                float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
                float lumaRange = lumaMax - lumaMin;
                
                if (lumaRange < EdgeThreshold) {
                    return float4(rgbM, 1.0);
                }
                
                float2 dir;
                dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
                dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
                
                float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * SubpixelQuality, 0.001);
                float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
                dir = clamp(dir * rcpDirMin, -8.0, 8.0) * TexelSize;
                
                float3 rgbA = 0.5 * (
                    SceneTexture.Sample(LinearSampler, uv + dir * (1.0/3.0 - 0.5)).rgb +
                    SceneTexture.Sample(LinearSampler, uv + dir * (2.0/3.0 - 0.5)).rgb);
                float3 rgbB = rgbA * 0.5 + 0.25 * (
                    SceneTexture.Sample(LinearSampler, uv + dir * -0.5).rgb +
                    SceneTexture.Sample(LinearSampler, uv + dir * 0.5).rgb);
                
                float lumaB = dot(rgbB, luma);
                if (lumaB < lumaMin || lumaB > lumaMax) {
                    return float4(rgbA, 1.0);
                }
                return float4(rgbB, 1.0);
            }
        )";

            ComPtr<ID3DBlob> psBlob, errorBlob;
            hr = D3DCompile(fxaaPS, strlen(fxaaPS), "FXAAPS", nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob,
                            &errorBlob);
            if (FAILED(hr))
            {
                std::printf("FXAA PS compile error: %s\n", errorBlob ? (char*)errorBlob->GetBufferPointer() : "Unknown");
                return false;
            }
            m_FXAAPSBytecode.assign((uint8_t*)psBlob->GetBufferPointer(),
                                    (uint8_t*)psBlob->GetBufferPointer() + psBlob->GetBufferSize());
        }
    }

    // Create FXAA root signature if needed
    if (!m_FXAARootSignature)
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER rootParams[2] = {};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.Num32BitValues = 4;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 2;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 1;
        rootSigDesc.pStaticSamplers = &sampler;

        ComPtr<ID3DBlob> sigBlob, errorBlob;
        hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr))
            return false;

        ID3D12RootSignature* rootSig = nullptr;
        hr = d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&rootSig));
        if (FAILED(hr))
            return false;
        m_FXAARootSignature = rootSig;
    }

    // Create FXAA PSO if needed
    if (!m_FXAAPSO)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>(m_FXAARootSignature);
        psoDesc.VS = {m_FXAAVSBytecode.data(), m_FXAAVSBytecode.size()};
        psoDesc.PS = {m_FXAAPSBytecode.data(), m_FXAAPSBytecode.size()};
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
            return false;
        m_FXAAPSO = pso;
    }

    return true;
}

void SimpleRenderer::ApplyFXAA(RHISwapChain* swapChain)
{
    if (!m_AntiAliasingSettings.enabled || m_AntiAliasingSettings.mode != AntiAliasingMode::FXAA)
        return;

    if (!m_FXAAIntermediateRT || !m_FXAAPSO || !m_FXAARootSignature)
        return;

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList || !d3dDevice)
        return;

    // Get swap chain render target
    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = d3dSwapChain->GetCurrentRTV();

    // Transition intermediate RT from render target to shader resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = reinterpret_cast<ID3D12Resource*>(m_FXAAIntermediateRT);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // Set FXAA pipeline
    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_FXAARootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(m_FXAAPSO));

    // Set render target to swap chain back buffer
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (float)m_FXAAWidth, (float)m_FXAAHeight, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)m_FXAAWidth, (LONG)m_FXAAHeight};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set FXAA parameters as root constants
    struct FXAAParams
    {
        float texelSizeX, texelSizeY;
        float subpixelQuality;
        float edgeThreshold;
    } params;
    params.texelSizeX = 1.0f / m_FXAAWidth;
    params.texelSizeY = 1.0f / m_FXAAHeight;
    params.subpixelQuality = m_AntiAliasingSettings.fxaaSubpixelQuality;
    params.edgeThreshold = m_AntiAliasingSettings.fxaaEdgeThreshold;
    cmdList->SetGraphicsRoot32BitConstants(0, 4, &params, 0);

    // Bind intermediate RT as SRV
    ID3D12DescriptorHeap* srvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAASRVHeap);
    cmdList->SetDescriptorHeaps(1, &srvHeap);
    cmdList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Draw full-screen triangle
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // Transition intermediate RT back to render target for next frame
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmdList->ResourceBarrier(1, &barrier);
}

bool SimpleRenderer::BeginFXAAPass(RHISwapChain* swapChain)
{
    if (!m_AntiAliasingSettings.enabled || m_AntiAliasingSettings.mode != AntiAliasingMode::FXAA)
    {
        m_IsInFXAAPass = false;
        return false;
    }

    m_IsInFXAAPass = true;

    // Create/resize FXAA resources if needed
    uint32_t width = swapChain->GetWidth();
    uint32_t height = swapChain->GetHeight();
    if (!CreateFXAAResources(width, height))
        return false;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return false;

    // Transition intermediate RT to render target state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = reinterpret_cast<ID3D12Resource*>(m_FXAAIntermediateRT);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // Clear the intermediate RT with the same color as the swap chain
    ID3D12DescriptorHeap* rtvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAARTVHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();

    float cr, cg, cb, ca;
    swapChain->GetClearColor(cr, cg, cb, ca);
    float clearColor[4] = {cr, cg, cb, ca};
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Get depth buffer from swap chain
    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = d3dSwapChain->GetDSV();

    // Set the intermediate RT as render target WITH depth buffer
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)width, (LONG)height};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    return true;
}

void SimpleRenderer::EndFXAAPass(RHISwapChain* swapChain)
{
    if (!m_IsInFXAAPass)
        return;

    ApplyFXAA(swapChain);
    m_IsInFXAAPass = false;
}



bool SimpleRenderer::CreateSSAOResources(RHISwapChain* swapChain, uint32_t width, uint32_t height)
{
    // Safety: check for null device and swapChain first
    if (!swapChain || !m_Device)
        return false;

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice)
        return false;

    const bool halfResolution = m_SSAOSettings.halfResolution;
    const bool preferExternalShaders = m_SSAOSettings.preferExternalShaders;
    const uint32_t aoWidth = halfResolution ? std::max(1u, width / 2) : std::max(1u, width);
    const uint32_t aoHeight = halfResolution ? std::max(1u, height / 2) : std::max(1u, height);
    const bool sizeChanged =
        (m_SSAOWidth != width || m_SSAOHeight != height || m_SSAOBufferWidth != aoWidth || m_SSAOBufferHeight != aoHeight);
    const bool shaderModeChanged = (m_SSAOUsingExternalShaders != preferExternalShaders);

    auto releaseObject = [](void*& object)
    {
        if (object)
        {
            reinterpret_cast<IUnknown*>(object)->Release();
            object = nullptr;
        }
    };

    if (sizeChanged)
    {
        m_SSAOOcclusionGraphTexture.reset();
        m_SSAOBlurredGraphTexture.reset();
        releaseObject(m_SSAOOcclusionRT);
        releaseObject(m_SSAOBlurredRT);
        releaseObject(m_SSAORTVHeap);
        releaseObject(m_SSAOSRVHeap);
    }

    if (shaderModeChanged)
    {
        releaseObject(m_SSAOPSO);
        releaseObject(m_SSAOBlurPSO);
        releaseObject(m_SSAODebugPSO);
        releaseObject(m_SSAOCompositePSO);
    }

    // Early out if all resources already exist with matching settings
    if (m_SSAOOcclusionRT && m_SSAOWidth == width && m_SSAOHeight == height && m_SSAOBufferWidth == aoWidth &&
        m_SSAOBufferHeight == aoHeight && m_SSAORootSignature && m_SSAOPSO && m_SSAODebugPSO && m_SSAOBlurPSO)
    {
        RefreshGraphInteropTextures();
        return true;
    }

    m_SSAOWidth = width;
    m_SSAOHeight = height;
    m_SSAOBufferWidth = aoWidth;
    m_SSAOBufferHeight = aoHeight;
    m_SSAOUsingExternalShaders = preferExternalShaders;

    D3D12_RESOURCE_DESC rtDesc = {};
    rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width = aoWidth;
    rtDesc.Height = aoHeight;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels = 1;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8_UNORM;
    clearValue.Color[0] = 1.0f; // Default white (no occlusion)

    HRESULT hr = d3dDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_SSAOOcclusionRT)));
    if (FAILED(hr) || !m_SSAOOcclusionRT)
        return false;

    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                            IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_SSAOBlurredRT)));
    if (FAILED(hr) || !m_SSAOBlurredRT)
        return false;

    // 2. Create Noise Texture (4x4 tiled)
    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
    std::default_random_engine generator;
    std::vector<float> ssaoNoise;
    for (unsigned int i = 0; i < 16; i++)
    {
        ssaoNoise.push_back(randomFloats(generator) * 2.0f - 1.0f);
        ssaoNoise.push_back(randomFloats(generator) * 2.0f - 1.0f);
        ssaoNoise.push_back(0.0f);
        ssaoNoise.push_back(1.0f);
    }

    D3D12_RESOURCE_DESC noiseDesc = {};
    noiseDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    noiseDesc.Width = 4;
    noiseDesc.Height = 4;
    noiseDesc.DepthOrArraySize = 1;
    noiseDesc.MipLevels = 1;
    noiseDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    noiseDesc.SampleDesc.Count = 1;

    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &noiseDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_SSAONoiseTexture)));
    if (FAILED(hr) || !m_SSAONoiseTexture)
        return false;

    // 3. Generate Sample Kernel
    m_SSAOKernel.clear();
    for (unsigned int i = 0; i < 64; ++i)
    {
        float x = randomFloats(generator) * 2.0f - 1.0f;
        float y = randomFloats(generator) * 2.0f - 1.0f;
        float z = randomFloats(generator);
        float mag = std::sqrt(x * x + y * y + z * z);
        x /= mag;
        y /= mag;
        z /= mag;
        float scale = (float)i / 64.0f;
        scale = 0.1f + scale * scale * (1.0f - 0.1f);
        m_SSAOKernel.push_back(x * scale);
        m_SSAOKernel.push_back(y * scale);
        m_SSAOKernel.push_back(z * scale);
        m_SSAOKernel.push_back(0.0f);
    }

    // 4. Create Descriptor Heaps
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = d3dDevice->CreateDescriptorHeap(&rtvHeapDesc,
                                         IID_PPV_ARGS(reinterpret_cast<ID3D12DescriptorHeap**>(&m_SSAORTVHeap)));
    if (FAILED(hr) || !m_SSAORTVHeap)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 5; // 0:Depth, 1:Noise, 2:Occ, 3:Blur, 4:Kernel
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc,
                                         IID_PPV_ARGS(reinterpret_cast<ID3D12DescriptorHeap**>(&m_SSAOSRVHeap)));
    if (FAILED(hr) || !m_SSAOSRVHeap)
        return false;

    // 5. Create Views
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        reinterpret_cast<ID3D12DescriptorHeap*>(m_SSAORTVHeap)->GetCPUDescriptorHandleForHeapStart();
    UINT rtvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    d3dDevice->CreateRenderTargetView(reinterpret_cast<ID3D12Resource*>(m_SSAOOcclusionRT), nullptr, rtvHandle);
    rtvHandle.ptr += rtvSize;
    d3dDevice->CreateRenderTargetView(reinterpret_cast<ID3D12Resource*>(m_SSAOBlurredRT), nullptr, rtvHandle);

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        reinterpret_cast<ID3D12DescriptorHeap*>(m_SSAOSRVHeap)->GetCPUDescriptorHandleForHeapStart();
    UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // SRV 0: Depth
    if (swapChain && swapChain->GetDepthSRV())
    {
        D3D12_CPU_DESCRIPTOR_HANDLE depthSrc =
            reinterpret_cast<ID3D12DescriptorHeap*>(swapChain->GetDepthSRV())->GetCPUDescriptorHandleForHeapStart();
        d3dDevice->CopyDescriptorsSimple(1, srvHandle, depthSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    srvHandle.ptr += srvSize;

    // SRV 1: Noise
    D3D12_SHADER_RESOURCE_VIEW_DESC noiseSrvDesc = {};
    noiseSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    noiseSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    noiseSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    noiseSrvDesc.Texture2D.MipLevels = 1;
    d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_SSAONoiseTexture), &noiseSrvDesc,
                                        srvHandle);
    srvHandle.ptr += srvSize;

    // SRV 2: Occlusion (for blur pass)
    D3D12_SHADER_RESOURCE_VIEW_DESC aoSrvDesc = {};
    aoSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    aoSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    aoSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    aoSrvDesc.Texture2D.MipLevels = 1;
    d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_SSAOOcclusionRT), &aoSrvDesc, srvHandle);
    srvHandle.ptr += srvSize;

    // SRV 3: Blurred (for final composite)
    d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_SSAOBlurredRT), &aoSrvDesc, srvHandle);
    srvHandle.ptr += srvSize;

    // 5b. Create Kernel Buffer (StructuredBuffer)
    if (!m_SSAOKernelBuffer)
    {
        D3D12_RESOURCE_DESC kernelDesc = {};
        kernelDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        kernelDesc.Width = 64 * 4 * sizeof(float); // 64 float4s
        kernelDesc.Height = 1;
        kernelDesc.DepthOrArraySize = 1;
        kernelDesc.MipLevels = 1;
        kernelDesc.Format = DXGI_FORMAT_UNKNOWN;
        kernelDesc.SampleDesc.Count = 1;
        kernelDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        kernelDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        d3dDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &kernelDesc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                           IID_PPV_ARGS(reinterpret_cast<ID3D12Resource**>(&m_SSAOKernelBuffer)));

        // Copy kernel data
        void* mappedData = nullptr;
        reinterpret_cast<ID3D12Resource*>(m_SSAOKernelBuffer)->Map(0, nullptr, &mappedData);
        memcpy(mappedData, m_SSAOKernel.data(), m_SSAOKernel.size() * sizeof(float));
        reinterpret_cast<ID3D12Resource*>(m_SSAOKernelBuffer)->Unmap(0, nullptr);
    }

    // SRV 4: Kernel
    D3D12_SHADER_RESOURCE_VIEW_DESC kernelSrvDesc = {};
    kernelSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    kernelSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    kernelSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    kernelSrvDesc.Buffer.FirstElement = 0;
    kernelSrvDesc.Buffer.NumElements = 64;
    kernelSrvDesc.Buffer.StructureByteStride = 16;
    kernelSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_SSAOKernelBuffer), &kernelSrvDesc,
                                        srvHandle);

    // 6. Create SSAO Root Signature
    if (!m_SSAORootSignature)
    {
        D3D12_DESCRIPTOR_RANGE ranges[1] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 5; // Depth, Noise, Occ, Blur, Kernel
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParams[2] = {};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.Num32BitValues = 28;

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &ranges[0];

        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;

        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[1].ShaderRegister = 1;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 2;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 2;
        rootSigDesc.pStaticSamplers = samplers;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serializedSig, errorBlob;
        if (SUCCEEDED(
                D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedSig, &errorBlob)))
        {
            d3dDevice->CreateRootSignature(0, serializedSig->GetBufferPointer(), serializedSig->GetBufferSize(),
                                           IID_PPV_ARGS(reinterpret_cast<ID3D12RootSignature**>(&m_SSAORootSignature)));
        }
    }

    // 7. Create SSAO PSOs (also create if debug PSO is missing)
    if (!m_SSAOPSO || !m_SSAODebugPSO)
    {
        const char* fullscreenVS = R"(
            struct VSOutput {
                float4 Position : SV_POSITION;
                float2 UV : TEXCOORD0;
            };
            VSOutput main(uint vertexID : SV_VertexID) {
                VSOutput output;
                output.UV = float2((vertexID << 1) & 2, vertexID & 2);
                output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
                return output;
            }
        )";

        // Classic kernel-based SSAO.
        const char* ssaoPS = R"(
            Texture2D DepthTex : register(t0);
            Texture2D NoiseTex : register(t1);
            StructuredBuffer<float4> SampleKernel : register(t4);
            SamplerState PointClamp : register(s0);
            SamplerState WrapSampler : register(s1);

            cbuffer SSAOConstants : register(b0) {
                float4x4 matProj;
                float4 aoParams0;
                float4 aoParams1;
                float4 aoParams2;
            };

            struct PS_INPUT {
                float4 pos : SV_POSITION;
                float2 uv : TEXCOORD0;
            };

            static const int MAX_KERNEL_SAMPLES = 64;

            float LinearizeDepth(float d) {
                float near = 0.1;
                float far = 1000.0;
                return near * far / (far - d * (far - near));
            }

            float3 ReconstructViewPos(float2 uv, float linearDepth) {
                float2 ndc = uv * 2.0 - 1.0;
                ndc.y = -ndc.y;

                float tanHalfFovY = 1.0 / max(abs(matProj[1][1]), 0.001);
                float aspectRatio = abs(matProj[1][1]) / max(abs(matProj[0][0]), 0.001);

                float3 viewPos;
                viewPos.x = ndc.x * tanHalfFovY * aspectRatio * linearDepth;
                viewPos.y = ndc.y * tanHalfFovY * linearDepth;
                viewPos.z = linearDepth;
                return viewPos;
            }

            float3 ReconstructNormal(float2 uv, float2 texelSize) {
                float d0 = LinearizeDepth(DepthTex.Sample(PointClamp, uv).r);
                float d1 = LinearizeDepth(DepthTex.Sample(PointClamp, uv + float2(texelSize.x, 0)).r);
                float d2 = LinearizeDepth(DepthTex.Sample(PointClamp, uv + float2(0, texelSize.y)).r);

                float3 p0 = ReconstructViewPos(uv, d0);
                float3 p1 = ReconstructViewPos(uv + float2(texelSize.x, 0), d1);
                float3 p2 = ReconstructViewPos(uv + float2(0, texelSize.y), d2);

                return normalize(cross(p2 - p0, p1 - p0));
            }

            float2 ProjectViewPosToUV(float3 viewPos) {
                float4 clip = mul(matProj, float4(viewPos, 1.0));
                float invW = 1.0 / max(abs(clip.w), 0.0001);
                float2 ndc = clip.xy * invW;
                return float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
            }

            float Hash12(float2 p) {
                float3 p3 = frac(float3(p.xyx) * 0.1031);
                p3 += dot(p3, p3.yzx + 33.33);
                return frac((p3.x + p3.y) * p3.z);
            }

            float2 RandomUnit2(float2 seed) {
                float angle = Hash12(seed) * 6.28318530718;
                return float2(cos(angle), sin(angle));
            }

            float4 main(PS_INPUT input) : SV_TARGET {
                float rawDepth = DepthTex.Sample(PointClamp, input.uv).r;
                if (rawDepth >= 1.0) return float4(1.0, 1.0, 1.0, 1.0);

                float radius = aoParams0.x;
                float bias = aoParams0.y;
                float intensity = aoParams0.z;
                float power = aoParams0.w;
                float thickness = aoParams1.x;
                int sampleCount = clamp((int)round(aoParams1.y), 1, MAX_KERNEL_SAMPLES);
                float maxScreenRadius = aoParams1.z;
                float2 texelSize = aoParams2.xy;
                float2 noiseScale = aoParams2.zw;
                float centerDepth = LinearizeDepth(rawDepth);
                float3 centerPos = ReconstructViewPos(input.uv, centerDepth);
                float3 normal = ReconstructNormal(input.uv, texelSize);

                float2 randomVec2 = RandomUnit2(input.pos.xy + noiseScale);
                float3 randomVec = normalize(float3(randomVec2, 0.0));
                float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
                float3 bitangent = cross(normal, tangent);
                float3x3 tbn = float3x3(tangent, bitangent, normal);

                float projectedRadiusPixels =
                    radius * max(abs(matProj[1][1]), 0.001) / max(centerDepth, 0.001) /
                    max(max(texelSize.x, texelSize.y), 0.00001);
                float radiusScale = min(1.0, maxScreenRadius / max(projectedRadiusPixels, 1.0));
                float kernelRadius = radius * radiusScale;
                float depthBias = max(0.0005, bias * 0.02);

                float occlusionAccum = 0.0;
                float weightAccum = 0.0;

                for (int sampleIndex = 0; sampleIndex < MAX_KERNEL_SAMPLES; ++sampleIndex) {
                    if (sampleIndex >= sampleCount) {
                        break;
                    }

                    float3 kernelDir = mul(tbn, SampleKernel[sampleIndex].xyz);
                    float3 samplePos = centerPos + kernelDir * kernelRadius;
                    float2 sampleUV = ProjectViewPosToUV(samplePos);

                    if (sampleUV.x <= texelSize.x || sampleUV.x >= 1.0 - texelSize.x ||
                        sampleUV.y <= texelSize.y || sampleUV.y >= 1.0 - texelSize.y) {
                        continue;
                    }

                    float sampleRawDepth = DepthTex.Sample(PointClamp, sampleUV).r;
                    if (sampleRawDepth >= 1.0) {
                        continue;
                    }

                    float sampleDepth = LinearizeDepth(sampleRawDepth);
                    float depthDelta = abs(centerDepth - sampleDepth);
                    float rangeWeight = saturate(1.0 - depthDelta / max(thickness, 0.001));
                    float occluded = sampleDepth <= (samplePos.z - depthBias) ? 1.0 : 0.0;

                    occlusionAccum += occluded * rangeWeight;
                    weightAccum += rangeWeight;
                }

                float occlusion = (weightAccum > 0.0) ? (occlusionAccum / weightAccum) : 0.0;
                float ao = saturate(1.0 - occlusion * max(intensity, 0.0));
                ao = pow(ao, max(power, 0.001));
                ao = max(ao, 0.01);
                return float4(ao, ao, ao, 1.0);
            }
        )";

        // Bilateral depth-aware blur shader
        const char* ssaoBlurPS = R"(
            Texture2D AOInput : register(t2);
            Texture2D DepthTex : register(t0);
            SamplerState PointClamp : register(s0);

            cbuffer BlurConstants : register(b0) {
                float2 texelSize;
                float depthThreshold;
                float _pad;
            };

            struct PS_INPUT {
                float4 pos : SV_POSITION;
                float2 uv : TEXCOORD0;
            };

            float LinearizeDepth(float d) {
                float near = 0.1;
                float far = 1000.0;
                return near * far / (far - d * (far - near));
            }

            float4 main(PS_INPUT input) : SV_TARGET {
                float2 ts = texelSize;
                
                float centerAO = AOInput.Sample(PointClamp, input.uv).r;
                float centerDepth = LinearizeDepth(DepthTex.Sample(PointClamp, input.uv).r);
                
                float totalAO = 0.0;
                float totalWeight = 0.0;
                
                for (int y = -1; y <= 1; y++) {
                    for (int x = -1; x <= 1; x++) {
                        float2 offset = float2(x, y) * ts;
                        float2 sampleUV = input.uv + offset;
                        
                        float sampleAO = AOInput.Sample(PointClamp, sampleUV).r;
                        float sampleDepth = LinearizeDepth(DepthTex.Sample(PointClamp, sampleUV).r);
                        
                        float depthDiff = abs(sampleDepth - centerDepth);
                        float depthWeight = exp(-depthDiff * max(depthThreshold, 0.001));

                        float weight = depthWeight;
                        totalAO += sampleAO * weight;
                        totalWeight += weight;
                    }
                }
                
                float ao = totalAO / max(totalWeight, 0.001);
                return float4(ao, ao, ao, 1.0);
            }
        )";

        // Try file-based shaders first, fall back to embedded if not found
        auto& compiler = ShaderCompiler::Get();
        const CompiledShader* vsShader = nullptr;
        const CompiledShader* ssaoShader = nullptr;
        const CompiledShader* blurShader = nullptr;
        if (preferExternalShaders)
        {
            vsShader = compiler.CompileFromFile("PostProcess/FullscreenVS.hlsl", "main", ShaderType::Vertex);
            ssaoShader = compiler.CompileFromFile("PostProcess/SSAO.hlsl", "main", ShaderType::Pixel);
            blurShader = compiler.CompileFromFile("PostProcess/SSAOBlur.hlsl", "main", ShaderType::Pixel);
        }

        // Fall back to embedded shaders if file-based compilation fails
        ComPtr<ID3DBlob> ssaoVsBlob, ssaoPsBlob, blurPsBlob, errorBlob;
        bool useFileShaders = preferExternalShaders && (vsShader && ssaoShader && blurShader);

        if (!useFileShaders)
        {
            if (preferExternalShaders)
            {
                std::printf("ShaderCompiler: SSAO shader files unavailable, using embedded shaders\n");
                if (!vsShader)
                    std::printf("  - FullscreenVS failed: %s\n", compiler.GetLastError().c_str());
                if (!ssaoShader)
                    std::printf("  - SSAO failed: %s\n", compiler.GetLastError().c_str());
                if (!blurShader)
                    std::printf("  - SSAOBlur failed: %s\n", compiler.GetLastError().c_str());
            }

            D3DCompile(fullscreenVS, strlen(fullscreenVS), "SSAOVS", nullptr, nullptr, "main", "vs_5_0", 0, 0,
                       &ssaoVsBlob, &errorBlob);
            D3DCompile(ssaoPS, strlen(ssaoPS), "SSAOPS", nullptr, nullptr, "main", "ps_5_0", 0, 0, &ssaoPsBlob,
                       &errorBlob);
            D3DCompile(ssaoBlurPS, strlen(ssaoBlurPS), "SSAOBlurPS", nullptr, nullptr, "main", "ps_5_0", 0, 0,
                       &blurPsBlob, &errorBlob);
        }
        else
        {
            std::printf("ShaderCompiler: Using file-based SSAO shaders\n");
        }

        bool hasShaders = useFileShaders || (ssaoVsBlob && ssaoPsBlob && blurPsBlob);
        if (hasShaders && m_SSAORootSignature)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>(m_SSAORootSignature);

            if (useFileShaders)
            {
                psoDesc.VS = {vsShader->bytecode.data(), vsShader->bytecode.size()};
                psoDesc.PS = {ssaoShader->bytecode.data(), ssaoShader->bytecode.size()};
            }
            else
            {
                psoDesc.VS = {ssaoVsBlob->GetBufferPointer(), ssaoVsBlob->GetBufferSize()};
                psoDesc.PS = {ssaoPsBlob->GetBufferPointer(), ssaoPsBlob->GetBufferSize()};
            }

            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
            psoDesc.SampleDesc.Count = 1;

            d3dDevice->CreateGraphicsPipelineState(&psoDesc,
                                                   IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&m_SSAOPSO)));

            // Blur PSO
            if (useFileShaders)
                psoDesc.PS = {blurShader->bytecode.data(), blurShader->bytecode.size()};
            else
                psoDesc.PS = {blurPsBlob->GetBufferPointer(), blurPsBlob->GetBufferSize()};
            d3dDevice->CreateGraphicsPipelineState(
                &psoDesc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&m_SSAOBlurPSO)));

            // Create debug PSO for rendering to RGBA swapchain
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            d3dDevice->CreateGraphicsPipelineState(
                &psoDesc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&m_SSAODebugPSO)));

            // Create composite PSO with multiply blending for SSAO overlay
            D3D12_RENDER_TARGET_BLEND_DESC multiplyBlend = {};
            multiplyBlend.BlendEnable = TRUE;
            multiplyBlend.SrcBlend = D3D12_BLEND_ZERO;
            multiplyBlend.DestBlend = D3D12_BLEND_SRC_COLOR; // dst * src = multiply
            multiplyBlend.BlendOp = D3D12_BLEND_OP_ADD;
            multiplyBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
            multiplyBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
            multiplyBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            multiplyBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[0] = multiplyBlend;
            d3dDevice->CreateGraphicsPipelineState(
                &psoDesc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&m_SSAOCompositePSO)));
        }
    }

    RefreshGraphInteropTextures();

    return true;
}

void SimpleRenderer::ApplySSAO(RHISwapChain* swapChain, const Camera& camera)
{
    SSAOGraphPassExecutor::Execute(*this, swapChain, camera);
}

void SimpleRenderer::ApplySSAOBlur(RHISwapChain* swapChain)
{
    SSAOBlurGraphPassExecutor::Execute(*this, swapChain);
}

void SimpleRenderer::DebugDrawSSAO(RHISwapChain* swapChain)
{
    SSAODebugCompositeExecutor::Execute(*this, swapChain);
}

void SimpleRenderer::BeginDepthPrepass(RHISwapChain* swapChain)
{
    m_IsDepthPrepass = true;
    if (!swapChain)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return;

    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    ID3D12Resource* depthBuffer = d3dSwapChain->GetDepthBuffer();
    if (!depthBuffer)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void SimpleRenderer::EndDepthPrepass(RHISwapChain* swapChain)
{
    m_IsDepthPrepass = false;
    if (!swapChain)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return;

    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    ID3D12Resource* depthBuffer = d3dSwapChain->GetDepthBuffer();
    if (!depthBuffer)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void SimpleRenderer::BeginForwardPass(RHISwapChain* swapChain)
{
    if (!swapChain)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return;

    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    ID3D12Resource* depthBuffer = d3dSwapChain->GetDepthBuffer();
    if (!depthBuffer)
        return;

    // Transition Depth: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
    // (SSAO left it in SRV state)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // Bind Render Targets (SwapChain BackBuffer + Depth)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
}

void SimpleRenderer::ClearSceneDepth(RHISwapChain* swapChain)
{
    if (!swapChain)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return;

    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

// ============================================================================
// HZB Occlusion Culling Implementation
// ============================================================================



} // namespace Dot
