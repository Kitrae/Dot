// =============================================================================
// Dot Engine - D3D12 Texture Implementation
// =============================================================================

#include "D3D12Texture.h"

#include "D3D12Device.h"

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

D3D12Texture::D3D12Texture(D3D12Device* device, const RHITextureDesc& desc)
    : m_Device(device), m_Width(desc.width), m_Height(desc.height), m_Depth(desc.depth), m_MipLevels(desc.mipLevels),
      m_Format(desc.format), m_Type(desc.type)
{
    D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    switch (desc.type)
    {
        case RHITextureType::Texture1D:
            dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            break;
        case RHITextureType::Texture2D:
        case RHITextureType::TextureCube:
        case RHITextureType::Texture2DArray:
            dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            break;
        case RHITextureType::Texture3D:
            dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            break;
    }

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (static_cast<uint8>(desc.usage) & static_cast<uint8>(RHITextureUsage::RenderTarget))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (static_cast<uint8>(desc.usage) & static_cast<uint8>(RHITextureUsage::DepthStencil))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (static_cast<uint8>(desc.usage) & static_cast<uint8>(RHITextureUsage::Storage))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = dimension;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = (desc.type == RHITextureType::Texture3D) ? static_cast<UINT16>(desc.depth)
                                                                             : static_cast<UINT16>(desc.arrayLayers);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resourceDesc.Format = ToDXGIFormat(desc.format);
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = flags;

    D3D12_CLEAR_VALUE* clearValue = nullptr;
    D3D12_CLEAR_VALUE clearValueData = {};

    if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    {
        clearValueData.Format = resourceDesc.Format;
        clearValueData.Color[0] = 0.0f;
        clearValueData.Color[1] = 0.0f;
        clearValueData.Color[2] = 0.0f;
        clearValueData.Color[3] = 1.0f;
        clearValue = &clearValueData;
    }
    else if (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    {
        clearValueData.Format = resourceDesc.Format;
        clearValueData.DepthStencil.Depth = 1.0f;
        clearValueData.DepthStencil.Stencil = 0;
        clearValue = &clearValueData;
    }

    HRESULT hr = device->GetDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                              D3D12_RESOURCE_STATE_COMMON, clearValue,
                                                              IID_PPV_ARGS(&m_Resource));

    CheckHR(hr, "CreateCommittedResource for texture");
    m_CurrentState = RHIResourceState::Common;

    if (desc.debugName)
    {
        wchar_t wName[256];
        size_t converted;
        mbstowcs_s(&converted, wName, desc.debugName, 255);
        m_Resource->SetName(wName);
    }

    CreateViews(desc);
}

D3D12Texture::D3D12Texture(D3D12Device* device, ID3D12Resource* resource, const RHITextureDesc& desc,
                           RHIResourceState initialState)
    : m_Device(device), m_Width(desc.width), m_Height(desc.height), m_Depth(desc.depth), m_MipLevels(desc.mipLevels),
      m_Format(desc.format), m_Type(desc.type), m_CurrentState(initialState)
{
    m_Resource = resource;
    CreateViews(desc);
}

void D3D12Texture::CreateViews(const RHITextureDesc& desc)
{
    if (!m_Device || !m_Resource)
        return;

    ID3D12Device* device = m_Device->GetDevice();
    if (!device)
        return;

    if ((static_cast<uint8>(desc.usage) & static_cast<uint8>(RHITextureUsage::RenderTarget)) != 0)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (SUCCEEDED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_RTVHeap))))
        {
            m_RTV = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = ToDXGIFormat(desc.format);
            if (desc.type == RHITextureType::Texture2DArray)
            {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.FirstArraySlice = 0;
                rtvDesc.Texture2DArray.ArraySize = desc.arrayLayers;
                rtvDesc.Texture2DArray.MipSlice = 0;
            }
            else
            {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice = 0;
            }
            device->CreateRenderTargetView(m_Resource.Get(), &rtvDesc, m_RTV);
            m_HasRTV = true;
        }
    }

    if ((static_cast<uint8>(desc.usage) & static_cast<uint8>(RHITextureUsage::DepthStencil)) != 0)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (SUCCEEDED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_DSVHeap))))
        {
            m_DSV = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = ToDXGIFormat(desc.format);
            if (desc.type == RHITextureType::Texture2DArray)
            {
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.FirstArraySlice = 0;
                dsvDesc.Texture2DArray.ArraySize = desc.arrayLayers;
                dsvDesc.Texture2DArray.MipSlice = 0;
            }
            else
            {
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2D.MipSlice = 0;
            }
            device->CreateDepthStencilView(m_Resource.Get(), &dsvDesc, m_DSV);
            m_HasDSV = true;
        }
    }
}

void D3D12Texture::Update(const void* data, uint32 mipLevel, uint32 arrayLayer)
{
    if (!data || !m_Device || !m_Resource)
        return;

    auto* d3dDevice = m_Device->GetDevice();
    auto* commandQueue = m_Device->GetCommandQueue();

    if (!d3dDevice || !commandQueue)
        return;

    // Create a temporary command allocator and list for this thread-safe update
    ComPtr<ID3D12CommandAllocator> tempAllocator;
    HRESULT hr = d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAllocator));
    if (FAILED(hr))
        return;

    ComPtr<ID3D12GraphicsCommandList> tempCmdList;
    hr = d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAllocator.Get(), nullptr,
                                      IID_PPV_ARGS(&tempCmdList));
    if (FAILED(hr))
        return;

    // Calculate subresource index: mip + (arrayLayer * mipLevels)
    UINT subresource = mipLevel + (arrayLayer * m_MipLevels);

    // Get the copy footprint for this subresource
    D3D12_RESOURCE_DESC resourceDesc = m_Resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSizeBytes;
    UINT64 totalBytes;
    d3dDevice->GetCopyableFootprints(&resourceDesc, subresource, 1, 0, &footprint, &numRows, &rowSizeBytes,
                                     &totalBytes);

    // Create upload heap (staging buffer)
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = totalBytes;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = d3dDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

    if (FAILED(hr))
        return;

    // Map and copy data to upload buffer
    void* mappedData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = uploadBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr))
        return;

    const uint8* srcData = static_cast<const uint8*>(data);
    uint8* dstData = static_cast<uint8*>(mappedData);
    UINT srcRowPitch = static_cast<UINT>(rowSizeBytes);
    UINT dstRowPitch = footprint.Footprint.RowPitch;

    for (UINT row = 0; row < numRows; ++row)
    {
        memcpy(dstData + row * dstRowPitch, srcData + row * srcRowPitch, srcRowPitch);
    }

    uploadBuffer->Unmap(0, nullptr);

    // Transition texture to copy destination
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_Resource.Get();
        barrier.Transition.Subresource = subresource;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        tempCmdList->ResourceBarrier(1, &barrier);
        m_CurrentState = RHIResourceState::CopyDest;
    }

    // Copy from upload buffer to texture
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadBuffer.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint = footprint;
    srcLocation.PlacedFootprint.Offset = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = m_Resource.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = subresource;

    tempCmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // Transition back to shader resource
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_Resource.Get();
        barrier.Transition.Subresource = subresource;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tempCmdList->ResourceBarrier(1, &barrier);
        m_CurrentState = RHIResourceState::ShaderResource;
    }

    tempCmdList->Close();

    // Execute immediately
    ID3D12CommandList* cmdLists[] = {tempCmdList.Get()};
    commandQueue->ExecuteCommandLists(1, cmdLists);

    // Create a fence to wait for completion
    ComPtr<ID3D12Fence> fence;
    hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (SUCCEEDED(hr))
    {
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        commandQueue->Signal(fence.Get(), 1);
        if (fence->GetCompletedValue() < 1)
        {
            fence->SetEventOnCompletion(1, event);
            WaitForSingleObject(event, INFINITE);
        }
        CloseHandle(event);
    }

    auto* d3dDeviceImpl = static_cast<D3D12Device*>(m_Device);
    d3dDeviceImpl->KeepAlive(uploadBuffer);
}

D3D12Sampler::D3D12Sampler(D3D12Device* device, const RHISamplerDesc& desc)
{
    D3D12_SAMPLER_DESC samplerDesc = {};

    // Filter
    if (desc.minFilter == RHIFilter::Nearest && desc.magFilter == RHIFilter::Nearest)
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    else if (desc.minFilter == RHIFilter::Linear && desc.magFilter == RHIFilter::Linear)
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    else
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;

    // Anisotropy
    if (desc.maxAnisotropy > 1.0f)
    {
        samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
        samplerDesc.MaxAnisotropy = static_cast<UINT>(desc.maxAnisotropy);
    }

    // Address modes
    auto toD3D = [](RHISamplerAddressMode mode) -> D3D12_TEXTURE_ADDRESS_MODE
    {
        switch (mode)
        {
            case RHISamplerAddressMode::Repeat:
                return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            case RHISamplerAddressMode::MirroredRepeat:
                return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
            case RHISamplerAddressMode::ClampToEdge:
                return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            case RHISamplerAddressMode::ClampToBorder:
                return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            default:
                return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        }
    };

    samplerDesc.AddressU = toD3D(desc.addressU);
    samplerDesc.AddressV = toD3D(desc.addressV);
    samplerDesc.AddressW = toD3D(desc.addressW);
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    // TODO: Allocate from sampler descriptor heap
    (void)device;
    (void)samplerDesc;
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
