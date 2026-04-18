// =============================================================================
// Dot Engine - D3D12 Texture
// =============================================================================
// D3D12 implementation of RHITexture.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHITexture.h"


#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12Device;

class D3D12Texture : public RHITexture
{
public:
    D3D12Texture(D3D12Device* device, const RHITextureDesc& desc);
    D3D12Texture(D3D12Device* device, ID3D12Resource* resource, const RHITextureDesc& desc,
                 RHIResourceState initialState);
    ~D3D12Texture() override = default;

    // RHITexture interface
    uint32 GetWidth() const override { return m_Width; }
    uint32 GetHeight() const override { return m_Height; }
    uint32 GetDepth() const override { return m_Depth; }
    uint32 GetMipLevels() const override { return m_MipLevels; }
    RHIFormat GetFormat() const override { return m_Format; }
    RHITextureType GetType() const override { return m_Type; }
    void Update(const void* data, uint32 mipLevel, uint32 arrayLayer) override;

    // D3D12-specific
    ID3D12Resource* GetResource() const { return m_Resource.Get(); }
    bool HasRTV() const { return m_HasRTV; }
    bool HasDSV() const { return m_HasDSV; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const { return m_RTV; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return m_DSV; }
    RHIResourceState GetCurrentState() const { return m_CurrentState; }
    void SetCurrentState(RHIResourceState state) { m_CurrentState = state; }

private:
    void CreateViews(const RHITextureDesc& desc);

    ComPtr<ID3D12Resource> m_Resource;
    ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_DSVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_RTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_DSV = {};
    bool m_HasRTV = false;
    bool m_HasDSV = false;
    uint32 m_Width = 0;
    uint32 m_Height = 0;
    uint32 m_Depth = 0;
    uint32 m_MipLevels = 0;
    RHIFormat m_Format = RHIFormat::Unknown;
    RHITextureType m_Type = RHITextureType::Texture2D;
    RHIResourceState m_CurrentState = RHIResourceState::Common;
    D3D12Device* m_Device = nullptr;
};

class D3D12Sampler : public RHISampler
{
public:
    D3D12Sampler(D3D12Device* device, const RHISamplerDesc& desc);
    ~D3D12Sampler() override = default;

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return m_Handle; }

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_Handle = {};
};

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
