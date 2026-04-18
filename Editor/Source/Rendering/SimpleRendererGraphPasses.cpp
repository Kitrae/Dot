// =============================================================================
// Dot Engine - Simple Renderer Graph Pass Executors
// =============================================================================

#include "SimpleRendererGraphPasses.h"

#include "SimpleRenderer.h"
#include "D3D12/D3D12SwapChain.h"

#include <algorithm>
#include <cmath>
#include <d3d12.h>
#include <cstring>
#include <limits>

extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);
extern ID3D12Resource* GetD3D12Buffer(Dot::RHIBuffer* buffer);
extern D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer);

namespace Dot
{

namespace
{

void RenderQueuedDraws(SimpleRenderer& renderer, const Camera& passCamera, RHISwapChain* swapChain,
                       const std::vector<RenderGraphQueuedDraw>& queue,
                       const std::vector<MaterialData>& resolvedMaterials, int* renderCallCounter)
{
    std::size_t currentMaterialIndex = std::numeric_limits<std::size_t>::max();

    for (std::size_t drawIndex = 0; drawIndex < queue.size();)
    {
        const RenderGraphQueuedDraw& firstDraw = queue[drawIndex];
        if (!firstDraw.mesh || firstDraw.materialIndex >= resolvedMaterials.size())
        {
            ++drawIndex;
            continue;
        }

        if (firstDraw.materialIndex != currentMaterialIndex)
        {
            renderer.SetMaterialData(resolvedMaterials[firstDraw.materialIndex]);
            currentMaterialIndex = firstDraw.materialIndex;
        }

        std::size_t groupEnd = drawIndex + 1;
        while (groupEnd < queue.size() && queue[groupEnd].materialIndex == firstDraw.materialIndex &&
               queue[groupEnd].mesh == firstDraw.mesh)
        {
            ++groupEnd;
        }

        for (std::size_t i = drawIndex; i < groupEnd; ++i)
        {
            if (!queue[i].mesh)
                continue;

            renderer.ClearReflectionProbeData();
            for (uint32_t probeIndex = 0; probeIndex < queue[i].reflectionProbeCount && probeIndex < 2; ++probeIndex)
            {
                const RenderGraphReflectionProbe& probe = queue[i].reflectionProbes[probeIndex];
                renderer.SetReflectionProbeData(probeIndex, probe.cubemapPath, probe.position, probe.radius,
                                                probe.boxExtents, probe.intensity, probe.falloff, probe.tint,
                                                probe.rotation, probe.blendWeight, true);
            }

            const PrimitiveMesh* drawMesh = queue[i].mesh;
            PrimitiveMesh overriddenMesh;
            if (queue[i].overrideLodThresholds)
            {
                overriddenMesh = *queue[i].mesh;
                overriddenMesh.lodScreenHeightThresholds[1] = std::max(queue[i].lod1ScreenHeight, 0.0f);
                overriddenMesh.lodScreenHeightThresholds[2] =
                    std::clamp(queue[i].lod2ScreenHeight, 0.0f, overriddenMesh.lodScreenHeightThresholds[1]);
                drawMesh = &overriddenMesh;
            }

            renderer.RenderMesh(passCamera, swapChain, queue[i].worldMatrix.Data(), *drawMesh);
            if (renderCallCounter)
                ++(*renderCallCounter);
        }

        drawIndex = groupEnd;
    }
}

} // namespace

void SSAOGraphPassExecutor::Execute(SimpleRenderer& renderer, RHISwapChain* swapChain, const Camera& camera)
{
    (void)swapChain;
    if (!renderer.m_SSAOSettings.enabled)
        return;

    if (!renderer.m_SSAOOcclusionRT || !renderer.m_SSAORTVHeap || !renderer.m_SSAOSRVHeap || !renderer.m_SSAOPSO ||
        !renderer.m_SSAORootSignature)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(renderer.m_Device);
    if (!cmdList)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = reinterpret_cast<ID3D12Resource*>(renderer.m_SSAOOcclusionRT);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_SSAORTVHeap)->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    const float aoWidth = static_cast<float>(std::max(renderer.m_SSAOBufferWidth, 1u));
    const float aoHeight = static_cast<float>(std::max(renderer.m_SSAOBufferHeight, 1u));
    D3D12_VIEWPORT vp = {0.0f, 0.0f, aoWidth, aoHeight, 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, static_cast<LONG>(aoWidth), static_cast<LONG>(aoHeight)};
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(renderer.m_SSAORootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(renderer.m_SSAOPSO));

    struct SSAOConstants
    {
        float matProj[16];
        float aoParams0[4];
        float aoParams1[4];
        float aoParams2[4];
    } constants = {};

    const float* projMatrix = camera.GetProjectionMatrix();
    if (!projMatrix)
        return;
    const int kernelSampleCount = std::max(1, static_cast<int>(renderer.m_SSAOKernel.size() / 4));
    std::memcpy(constants.matProj, projMatrix, 16 * sizeof(float));
    constants.aoParams0[0] = renderer.m_SSAOSettings.radius;
    constants.aoParams0[1] = renderer.m_SSAOSettings.bias;
    constants.aoParams0[2] = renderer.m_SSAOSettings.intensity;
    constants.aoParams0[3] = renderer.m_SSAOSettings.power;
    constants.aoParams1[0] = renderer.m_SSAOSettings.thickness;
    constants.aoParams1[1] = static_cast<float>(std::clamp(renderer.m_SSAOSettings.sampleCount, 1, kernelSampleCount));
    constants.aoParams1[2] = renderer.m_SSAOSettings.maxScreenRadius;
    constants.aoParams1[3] = 0.0f;
    constants.aoParams2[0] = 1.0f / aoWidth;
    constants.aoParams2[1] = 1.0f / aoHeight;
    constants.aoParams2[2] = aoWidth / 4.0f;
    constants.aoParams2[3] = aoHeight / 4.0f;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);

    ID3D12DescriptorHeap* heaps[] = {reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_SSAOSRVHeap)};
    if (!heaps[0])
        return;

    if (swapChain)
    {
        auto depthSRV = reinterpret_cast<ID3D12DescriptorHeap*>(swapChain->GetDepthSRV());
        if (depthSRV)
        {
            ID3D12Device* d3dDevice = GetD3D12DevicePtr(renderer.m_Device);
            D3D12_CPU_DESCRIPTOR_HANDLE dest = heaps[0]->GetCPUDescriptorHandleForHeapStart();
            D3D12_CPU_DESCRIPTOR_HANDLE src = depthSRV->GetCPUDescriptorHandleForHeapStart();
            d3dDevice->CopyDescriptorsSimple(1, dest, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(1, heaps[0]->GetGPUDescriptorHandleForHeapStart());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}

void SSAOBlurGraphPassExecutor::Execute(SimpleRenderer& renderer, RHISwapChain* swapChain)
{
    (void)swapChain;
    if (!renderer.m_SSAOSettings.enabled)
        return;

    if (!renderer.m_SSAOBlurredRT || !renderer.m_SSAORTVHeap || !renderer.m_SSAOSRVHeap || !renderer.m_SSAOBlurPSO ||
        !renderer.m_SSAORootSignature)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(renderer.m_Device);
    if (!cmdList)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = reinterpret_cast<ID3D12Resource*>(renderer.m_SSAOBlurredRT);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_SSAORTVHeap)->GetCPUDescriptorHandleForHeapStart();
    UINT rtvSize = GetD3D12DevicePtr(renderer.m_Device)->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtv.ptr += rtvSize;
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(renderer.m_SSAORootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(renderer.m_SSAOBlurPSO));

    ID3D12DescriptorHeap* heaps[] = {reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_SSAOSRVHeap)};
    cmdList->SetDescriptorHeaps(1, heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heaps[0]->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(1, gpuHandle);

    struct
    {
        float texelSize[2];
        float depthThreshold;
        float pad;
    } blurConstants;
    const float aoWidth = static_cast<float>(std::max(renderer.m_SSAOBufferWidth, 1u));
    const float aoHeight = static_cast<float>(std::max(renderer.m_SSAOBufferHeight, 1u));
    blurConstants.texelSize[0] = 1.0f / aoWidth;
    blurConstants.texelSize[1] = 1.0f / aoHeight;
    blurConstants.depthThreshold = renderer.m_SSAOSettings.blurDepthThreshold;
    blurConstants.pad = 0.0f;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(blurConstants) / 4, &blurConstants, 0);

    D3D12_VIEWPORT vp = {0.0f, 0.0f, aoWidth, aoHeight, 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, static_cast<LONG>(aoWidth), static_cast<LONG>(aoHeight)};
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}

void SSAODebugCompositeExecutor::Execute(SimpleRenderer& renderer, RHISwapChain* swapChain)
{
    if (!renderer.m_SSAOSettings.enabled || !renderer.m_SSAOOcclusionRT || !renderer.m_SSAOSRVHeap || !swapChain)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(renderer.m_Device);
    if (!cmdList)
        return;

    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3dSwapChain->GetCurrentRTV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = {0.0f, 0.0f, (float)swapChain->GetWidth(), (float)swapChain->GetHeight(), 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, (LONG)swapChain->GetWidth(), (LONG)swapChain->GetHeight()};
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    if (!renderer.m_SSAODebugPSO || !renderer.m_SSAORootSignature)
        return;

    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(renderer.m_SSAORootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(renderer.m_SSAODebugPSO));

    ID3D12DescriptorHeap* heaps[] = {reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_SSAOSRVHeap)};
    cmdList->SetDescriptorHeaps(1, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heaps[0]->GetGPUDescriptorHandleForHeapStart();
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(renderer.m_Device);
    UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    gpuHandle.ptr += 3 * srvSize;
    cmdList->SetGraphicsRootDescriptorTable(1, gpuHandle);

    struct
    {
        float texelSize[2];
        float pad[2];
    } blurConst = {{1.0f / swapChain->GetWidth(), 1.0f / swapChain->GetHeight()}, {0, 0}};
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(blurConst) / 4, &blurConst, 0);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void HZBGraphPassExecutor::Execute(SimpleRenderer& renderer, RHISwapChain* swapChain)
{
    if (!renderer.m_Occlusion.enabled || !swapChain || !renderer.m_Occlusion.hzbTexture || !renderer.m_Occlusion.hzbDownsamplePSO ||
        !renderer.m_Occlusion.hzbRootSignature || !renderer.m_Occlusion.hzbSrvHeap)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(renderer.m_Device);
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(renderer.m_Device);
    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);
    if (!cmdList || !d3dDevice || !d3dSwapChain)
        return;

    ID3D12Resource* depthBuffer = d3dSwapChain->GetDepthBuffer();
    ID3D12Resource* hzbTexture = reinterpret_cast<ID3D12Resource*>(renderer.m_Occlusion.hzbTexture);
    if (!depthBuffer || !hzbTexture)
        return;

    ++renderer.m_FrameCounter;

    if (renderer.m_Occlusion.hzbReadbackBuffer && !renderer.m_Occlusion.readbackData.empty())
    {
        void* mappedData = nullptr;
        ID3D12Resource* readbackBuffer = reinterpret_cast<ID3D12Resource*>(renderer.m_Occlusion.hzbReadbackBuffer);
        if (SUCCEEDED(readbackBuffer->Map(0, nullptr, &mappedData)) && mappedData)
        {
            std::memcpy(renderer.m_Occlusion.readbackData.data(), mappedData, renderer.m_Occlusion.readbackData.size());
            readbackBuffer->Unmap(0, nullptr);
            renderer.m_Occlusion.readbackValid = true;
        }
    }

    D3D12_RESOURCE_BARRIER copyBarriers[2] = {};
    copyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyBarriers[0].Transition.pResource = depthBuffer;
    copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    copyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyBarriers[1].Transition.pResource = hzbTexture;
    copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    copyBarriers[1].Transition.Subresource = 0;
    cmdList->ResourceBarrier(2, copyBarriers);

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = depthBuffer;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = hzbTexture;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;
    cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(2, copyBarriers);

    cmdList->SetComputeRootSignature(reinterpret_cast<ID3D12RootSignature*>(renderer.m_Occlusion.hzbRootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(renderer.m_Occlusion.hzbDownsamplePSO));

    ID3D12DescriptorHeap* descriptorHeap = reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_Occlusion.hzbSrvHeap);
    ID3D12DescriptorHeap* heaps[] = {descriptorHeap};
    cmdList->SetDescriptorHeaps(1, heaps);

    const UINT descriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    uint32_t mipWidth = renderer.m_Occlusion.width;
    uint32_t mipHeight = renderer.m_Occlusion.height;

    for (uint32_t mip = 1; mip < renderer.m_Occlusion.mipLevels; ++mip)
    {
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);

        D3D12_RESOURCE_BARRIER uavTransition = {};
        uavTransition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        uavTransition.Transition.pResource = hzbTexture;
        uavTransition.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        uavTransition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        uavTransition.Transition.Subresource = mip;
        cmdList->ResourceBarrier(1, &uavTransition);

        uint32_t constants[4] = {mip - 1, mipWidth, mipHeight, 0};
        cmdList->SetComputeRoot32BitConstants(0, 4, constants, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle.ptr += static_cast<SIZE_T>(mip - 1) * descriptorSize;
        cmdList->SetComputeRootDescriptorTable(1, srvHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        uavHandle.ptr += static_cast<SIZE_T>(renderer.m_Occlusion.mipLevels + mip) * descriptorSize;
        cmdList->SetComputeRootDescriptorTable(2, uavHandle);

        cmdList->Dispatch((mipWidth + 7) / 8, (mipHeight + 7) / 8, 1);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = hzbTexture;
        cmdList->ResourceBarrier(1, &uavBarrier);

        uavTransition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        uavTransition.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &uavTransition);
    }

    if (renderer.m_Occlusion.hzbReadbackBuffer)
    {
        D3D12_RESOURCE_BARRIER readbackTransition = {};
        readbackTransition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        readbackTransition.Transition.pResource = hzbTexture;
        readbackTransition.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        readbackTransition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        readbackTransition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &readbackTransition);

        ID3D12Resource* readbackBuffer = reinterpret_cast<ID3D12Resource*>(renderer.m_Occlusion.hzbReadbackBuffer);
        for (uint32_t mip = 0; mip < renderer.m_Occlusion.mipLevels; ++mip)
        {
            D3D12_TEXTURE_COPY_LOCATION copySrc = {};
            copySrc.pResource = hzbTexture;
            copySrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            copySrc.SubresourceIndex = mip;

            D3D12_TEXTURE_COPY_LOCATION copyDst = {};
            copyDst.pResource = readbackBuffer;
            copyDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            copyDst.PlacedFootprint.Offset = renderer.m_Occlusion.mipReadback[mip].offset;
            copyDst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
            copyDst.PlacedFootprint.Footprint.Width = renderer.m_Occlusion.mipReadback[mip].width;
            copyDst.PlacedFootprint.Footprint.Height = renderer.m_Occlusion.mipReadback[mip].height;
            copyDst.PlacedFootprint.Footprint.Depth = 1;
            copyDst.PlacedFootprint.Footprint.RowPitch = renderer.m_Occlusion.mipReadback[mip].rowPitch;

            cmdList->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);
        }

        readbackTransition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        readbackTransition.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &readbackTransition);
    }
}

void DirectionalShadowGraphPassExecutor::Execute(
    SimpleRenderer& renderer, const Camera& camera, const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters)
{
    if (!renderer.m_Initialized || !renderer.m_ShadowPSO || !renderer.m_ShadowDepthBuffer || shadowCasters.empty())
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(renderer.m_Device);
    if (!cmdList)
        return;

    float ldirX = renderer.m_LightData.lightDirX;
    float ldirY = renderer.m_LightData.lightDirY;
    float ldirZ = renderer.m_LightData.lightDirZ;
    float len = std::sqrt(ldirX * ldirX + ldirY * ldirY + ldirZ * ldirZ);
    if (len < 0.0001f)
    {
        ldirX = 0.5f;
        ldirY = -0.7f;
        ldirZ = 0.5f;
        len = 1.0f;
    }
    ldirX /= len;
    ldirY /= len;
    ldirZ /= len;

    float targetX, targetY, targetZ;
    camera.GetTarget(targetX, targetY, targetZ);
    float lightPosX = targetX - ldirX * renderer.m_ShadowDistance;
    float lightPosY = targetY - ldirY * renderer.m_ShadowDistance;
    float lightPosZ = targetZ - ldirZ * renderer.m_ShadowDistance;

    float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
    if (std::abs(ldirY) > 0.99f)
    {
        upX = 0.0f;
        upY = 0.0f;
        upZ = 1.0f;
    }

    float fwdX = ldirX, fwdY = ldirY, fwdZ = ldirZ;
    float rightX = upY * fwdZ - upZ * fwdY;
    float rightY = upZ * fwdX - upX * fwdZ;
    float rightZ = upX * fwdY - upY * fwdX;
    float rightLen = std::sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
    rightX /= rightLen;
    rightY /= rightLen;
    rightZ /= rightLen;
    float upX2 = fwdY * rightZ - fwdZ * rightY;
    float upY2 = fwdZ * rightX - fwdX * rightZ;
    float upZ2 = fwdX * rightY - fwdY * rightX;

    float lightView[16] = {rightX,
                           upX2,
                           fwdX,
                           0.0f,
                           rightY,
                           upY2,
                           fwdY,
                           0.0f,
                           rightZ,
                           upZ2,
                           fwdZ,
                           0.0f,
                           -(rightX * lightPosX + rightY * lightPosY + rightZ * lightPosZ),
                           -(upX2 * lightPosX + upY2 * lightPosY + upZ2 * lightPosZ),
                           -(fwdX * lightPosX + fwdY * lightPosY + fwdZ * lightPosZ),
                           1.0f};

    float orthoSize = std::max(10.0f, renderer.m_ShadowDistance * 0.1f);
    float nearPlane = 1.0f;
    float farPlane = renderer.m_ShadowDistance * 3.0f;
    float lightProj[16] = {1.0f / orthoSize,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           1.0f / orthoSize,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           1.0f / (farPlane - nearPlane),
                           0.0f,
                           0.0f,
                           0.0f,
                           -nearPlane / (farPlane - nearPlane),
                           1.0f};

    float lightVP[16];
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            lightVP[col * 4 + row] = 0.0f;
            for (int k = 0; k < 4; ++k)
                lightVP[col * 4 + row] += lightProj[k * 4 + row] * lightView[col * 4 + k];
        }
    }

    std::memcpy(renderer.m_ShadowLightMatrix, lightVP, sizeof(renderer.m_ShadowLightMatrix));
    renderer.m_ShadowEnabled = true;

    ID3D12DescriptorHeap* dsvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_ShadowDSVHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (float)SimpleRenderer::kShadowMapResolution,
                               (float)SimpleRenderer::kShadowMapResolution, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)SimpleRenderer::kShadowMapResolution, (LONG)SimpleRenderer::kShadowMapResolution};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(renderer.m_ShadowPSO));
    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(renderer.m_ShadowRootSignature));
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const auto& caster : shadowCasters)
    {
        const float* worldMatrix = caster.first;
        const PrimitiveMesh* mesh = caster.second;
        if (!mesh || mesh->indexCount == 0)
            continue;

        float lightMVP[16];
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                lightMVP[col * 4 + row] =
                    lightVP[0 * 4 + row] * worldMatrix[col * 4 + 0] + lightVP[1 * 4 + row] * worldMatrix[col * 4 + 1] +
                    lightVP[2 * 4 + row] * worldMatrix[col * 4 + 2] + lightVP[3 * 4 + row] * worldMatrix[col * 4 + 3];
            }
        }

        if (renderer.m_CurrentCBOffset + 256 > SimpleRenderer::kMaxCBSize)
            renderer.m_CurrentCBOffset = 0;

        uint8_t* cbData = reinterpret_cast<uint8_t*>(renderer.m_MappedLightBuffer) + renderer.m_CurrentCBOffset;
        std::memcpy(cbData, lightMVP, sizeof(lightMVP));

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
            reinterpret_cast<ID3D12Resource*>(renderer.m_CBResource)->GetGPUVirtualAddress() + renderer.m_CurrentCBOffset;
        cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);
        renderer.m_CurrentCBOffset += 256;

        if (!mesh->vertexBuffer || !mesh->indexBuffer || !GetD3D12Buffer(mesh->vertexBuffer.get()) ||
            !GetD3D12Buffer(mesh->indexBuffer.get()))
        {
            continue;
        }

        D3D12_VERTEX_BUFFER_VIEW vbView = {};
        vbView.BufferLocation = GetD3D12BufferGPUAddress(mesh->vertexBuffer.get());
        vbView.SizeInBytes = static_cast<UINT>(mesh->vertexBuffer->GetSize());
        vbView.StrideInBytes = sizeof(PrimitiveVertex);
        cmdList->IASetVertexBuffers(0, 1, &vbView);

        D3D12_INDEX_BUFFER_VIEW ibView = {};
        ibView.BufferLocation = GetD3D12BufferGPUAddress(mesh->indexBuffer.get());
        ibView.SizeInBytes = static_cast<UINT>(mesh->indexBuffer->GetSize());
        ibView.Format = DXGI_FORMAT_R32_UINT;
        cmdList->IASetIndexBuffer(&ibView);
        cmdList->DrawIndexedInstanced(mesh->indexCount, 1, 0, 0, 0);
    }

}

void LocalShadowGraphPassExecutor::Execute(
    SimpleRenderer& renderer, const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters)
{
    if (!renderer.m_Initialized || !renderer.m_ShadowPSO || !renderer.m_LocalShadowDepthBuffer || !renderer.m_LocalShadowDSVHeap ||
        shadowCasters.empty())
    {
        return;
    }

    struct ActivePointShadow
    {
        int lightIndex = -1;
        uint32_t baseSlice = 0;
        float range = 0.0f;
    };

    struct ActiveSpotShadow
    {
        int lightIndex = -1;
        uint32_t slice = 0;
        float range = 0.0f;
    };

    std::vector<ActivePointShadow> activeLights;
    activeLights.reserve(kMaxShadowedPointLights);
    for (int i = 0; i < renderer.m_LightData.numPointLights && i < SceneLightData::MAX_POINT_LIGHTS; ++i)
    {
        const auto& light = renderer.m_LightData.pointLights[i];
        if (light.shadowEnabled > 0.5f && light.range > 0.0f)
            activeLights.push_back({i, static_cast<uint32_t>(light.shadowBaseSlice), light.range});
    }

    std::vector<ActiveSpotShadow> activeSpotLights;
    activeSpotLights.reserve(kMaxShadowedSpotLights);
    for (int i = 0; i < renderer.m_LightData.numSpotLights && i < SceneLightData::MAX_SPOT_LIGHTS; ++i)
    {
        const auto& light = renderer.m_LightData.spotLights[i];
        if (light.shadowEnabled > 0.5f && light.range > 0.0f)
            activeSpotLights.push_back({i, static_cast<uint32_t>(light.shadowBaseSlice), light.range});
    }

    if (activeLights.empty() && activeSpotLights.empty())
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(renderer.m_Device);
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(renderer.m_Device);
    if (!cmdList || !d3dDevice)
        return;

    D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(kLocalShadowResolution),
                               static_cast<float>(kLocalShadowResolution), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(kLocalShadowResolution), static_cast<LONG>(kLocalShadowResolution)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(renderer.m_ShadowPSO));
    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(renderer.m_ShadowRootSignature));
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap* localDsvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(renderer.m_LocalShadowDSVHeap);
    const UINT dsvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    auto drawCasterSet = [&](const float* lightVP, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
    {
        cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        for (const auto& caster : shadowCasters)
        {
            const float* worldMatrix = caster.first;
            const PrimitiveMesh* mesh = caster.second;
            if (!mesh || mesh->indexCount == 0)
                continue;

            float lightMVP[16];
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    lightMVP[col * 4 + row] = lightVP[0 * 4 + row] * worldMatrix[col * 4 + 0] +
                                              lightVP[1 * 4 + row] * worldMatrix[col * 4 + 1] +
                                              lightVP[2 * 4 + row] * worldMatrix[col * 4 + 2] +
                                              lightVP[3 * 4 + row] * worldMatrix[col * 4 + 3];
                }
            }

            if (renderer.m_CurrentCBOffset + 256 > SimpleRenderer::kMaxCBSize)
                renderer.m_CurrentCBOffset = 0;

            uint8_t* cbData = reinterpret_cast<uint8_t*>(renderer.m_MappedLightBuffer) + renderer.m_CurrentCBOffset;
            std::memcpy(cbData, lightMVP, sizeof(lightMVP));

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = reinterpret_cast<ID3D12Resource*>(renderer.m_CBResource)->GetGPUVirtualAddress() +
                                               renderer.m_CurrentCBOffset;
            cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);
            renderer.m_CurrentCBOffset += 256;

            if (!mesh->vertexBuffer || !mesh->indexBuffer || !GetD3D12Buffer(mesh->vertexBuffer.get()) ||
                !GetD3D12Buffer(mesh->indexBuffer.get()))
            {
                continue;
            }

            D3D12_VERTEX_BUFFER_VIEW vbView = {};
            vbView.BufferLocation = GetD3D12BufferGPUAddress(mesh->vertexBuffer.get());
            vbView.SizeInBytes = static_cast<UINT>(mesh->vertexBuffer->GetSize());
            vbView.StrideInBytes = sizeof(PrimitiveVertex);
            cmdList->IASetVertexBuffers(0, 1, &vbView);

            D3D12_INDEX_BUFFER_VIEW ibView = {};
            ibView.BufferLocation = GetD3D12BufferGPUAddress(mesh->indexBuffer.get());
            ibView.SizeInBytes = static_cast<UINT>(mesh->indexBuffer->GetSize());
            ibView.Format = DXGI_FORMAT_R32_UINT;
            cmdList->IASetIndexBuffer(&ibView);
            cmdList->DrawIndexedInstanced(mesh->indexCount, 1, 0, 0, 0);
        }
    };

    for (const ActivePointShadow& active : activeLights)
    {
        const auto& light = renderer.m_LightData.pointLights[active.lightIndex];
        const Vec3 lightPosition(light.posX, light.posY, light.posZ);
        const auto faces = BuildPointLightShadowFaces(lightPosition, light.range);
        for (uint32_t faceIndex = 0; faceIndex < kPointShadowFacesPerLight; ++faceIndex)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = localDsvHeap->GetCPUDescriptorHandleForHeapStart();
            dsv.ptr += static_cast<SIZE_T>(active.baseSlice + faceIndex) * dsvSize;
            drawCasterSet(faces[faceIndex].viewProjection.Data(), dsv);
        }
    }

    for (const ActiveSpotShadow& active : activeSpotLights)
    {
        const auto& light = renderer.m_LightData.spotLights[active.lightIndex];
        const Vec3 lightPosition(light.posX, light.posY, light.posZ);
        const Vec3 lightDirection(light.dirX, light.dirY, light.dirZ);
        const float outerConeAngleDegrees = std::acos(std::clamp(light.outerCos, -1.0f, 1.0f)) * (180.0f / 3.14159265f);
        const SpotLightShadowFace face =
            BuildSpotLightShadowFace(lightPosition, lightDirection, light.range, outerConeAngleDegrees);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = localDsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(active.slice) * dsvSize;
        drawCasterSet(face.viewProjection.Data(), dsv);
    }

}

void MainSceneGraphPassExecutor::Execute(SimpleRenderer& renderer, RHISwapChain* swapChain, const Camera& camera,
                                         const std::vector<RenderGraphQueuedDraw>& drawQueue,
                                         const std::vector<RenderGraphMapPreviewDraw>& mapPreviewDraws,
                                         const std::vector<MaterialData>& resolvedMaterials,
                                         bool antiAliasingEnabled, bool deferFXAAResolve, int* renderCallCounter)
{
    if (!swapChain)
        return;

    RenderQueuedDraws(renderer, camera, swapChain, drawQueue, resolvedMaterials, renderCallCounter);

    for (const RenderGraphMapPreviewDraw& draw : mapPreviewDraws)
    {
        if (!draw.mesh || !draw.material)
            continue;

        renderer.SetMaterialData(*draw.material);
        renderer.ClearReflectionProbeData();
        for (uint32_t probeIndex = 0; probeIndex < draw.reflectionProbeCount && probeIndex < 2; ++probeIndex)
        {
            const RenderGraphReflectionProbe& probe = draw.reflectionProbes[probeIndex];
            renderer.SetReflectionProbeData(probeIndex, probe.cubemapPath, probe.position, probe.radius,
                                            probe.boxExtents, probe.intensity, probe.falloff, probe.tint,
                                            probe.rotation, probe.blendWeight, true);
        }
        renderer.RenderMesh(camera, swapChain, draw.worldMatrix.Data(), *draw.mesh);
        if (renderCallCounter)
            ++(*renderCallCounter);
    }

    if (antiAliasingEnabled && !deferFXAAResolve)
        renderer.EndFXAAPass(swapChain);
}

void ViewmodelGraphPassExecutor::Execute(SimpleRenderer& renderer, RHISwapChain* swapChain,
                                         const Camera& viewmodelCamera,
                                         const std::vector<RenderGraphQueuedDraw>& drawQueue,
                                         const std::vector<MaterialData>& resolvedMaterials,
                                         bool antiAliasingEnabled, int* renderCallCounter)
{
    if (!swapChain)
        return;

    if (!drawQueue.empty())
    {
        renderer.ClearSceneDepth(swapChain);
        RenderQueuedDraws(renderer, viewmodelCamera, swapChain, drawQueue, resolvedMaterials, renderCallCounter);
    }

    if (antiAliasingEnabled)
        renderer.EndFXAAPass(swapChain);
}

} // namespace Dot
