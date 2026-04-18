// =============================================================================
// Dot Engine - D3D12 Buffer
// =============================================================================
// D3D12 implementation of RHIBuffer.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHIBuffer.h"


#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12Device;

class D3D12Buffer : public RHIBuffer
{
public:
    D3D12Buffer(D3D12Device* device, const RHIBufferDesc& desc);
    ~D3D12Buffer() override;

    // RHIBuffer interface
    usize GetSize() const override { return m_Size; }
    RHIBufferUsage GetUsage() const override { return m_Usage; }
    void* Map() override;
    void Unmap() override;
    void Update(const void* data, usize size, usize offset) override;

    // D3D12-specific
    ID3D12Resource* GetResource() const { return m_Resource.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const { return m_Resource->GetGPUVirtualAddress(); }

private:
    D3D12Device* m_Device = nullptr;
    ComPtr<ID3D12Resource> m_Resource;
    usize m_Size = 0;
    RHIBufferUsage m_Usage;
    RHIMemoryUsage m_Memory;
    void* m_MappedData = nullptr;
};

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
