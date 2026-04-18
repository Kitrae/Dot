// =============================================================================
// Dot Engine - D3D12 Pipeline
// =============================================================================
// D3D12 implementation of RHIPipeline.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHIPipeline.h"

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12Device;

class D3D12Pipeline : public RHIPipeline
{
public:
    D3D12Pipeline(D3D12Device* device, const RHIPipelineDesc& desc);
    ~D3D12Pipeline() override = default;

    // D3D12-specific
    ID3D12PipelineState* GetPipelineState() const { return m_PipelineState.Get(); }
    ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.Get(); }
    D3D_PRIMITIVE_TOPOLOGY GetTopology() const { return m_Topology; }
    uint32 GetVertexStride() const { return m_VertexStride; }

private:
    bool CreateRootSignature(D3D12Device* device);
    bool CreatePipelineState(D3D12Device* device, const RHIPipelineDesc& desc);

    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PipelineState;
    D3D_PRIMITIVE_TOPOLOGY m_Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    uint32 m_VertexStride = 0;
};

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
