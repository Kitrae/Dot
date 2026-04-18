// =============================================================================
// Dot Engine - D3D12 Buffer Implementation
// =============================================================================

#include "D3D12Buffer.h"

#include "D3D12Device.h"


#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

D3D12Buffer::D3D12Buffer(D3D12Device* device, const RHIBufferDesc& desc)
    : m_Device(device), m_Size(desc.size), m_Usage(desc.usage), m_Memory(desc.memory)
{
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

    switch (desc.memory)
    {
        case RHIMemoryUsage::GPU_Only:
            heapType = D3D12_HEAP_TYPE_DEFAULT;
            initialState = D3D12_RESOURCE_STATE_COMMON;
            break;
        case RHIMemoryUsage::CPU_To_GPU:
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case RHIMemoryUsage::GPU_To_CPU:
            heapType = D3D12_HEAP_TYPE_READBACK;
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = static_cast<UINT64>(desc.size);
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->GetDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                              initialState, nullptr, IID_PPV_ARGS(&m_Resource));

    CheckHR(hr, "CreateCommittedResource for buffer");

    if (desc.debugName)
    {
        // Convert to wide string for D3D12
        wchar_t wName[256];
        size_t converted;
        mbstowcs_s(&converted, wName, desc.debugName, 255);
        m_Resource->SetName(wName);
    }
}

D3D12Buffer::~D3D12Buffer()
{
    if (m_MappedData)
    {
        Unmap();
    }
}

void* D3D12Buffer::Map()
{
    if (m_MappedData)
        return m_MappedData;

    D3D12_RANGE readRange = {}; // Empty range means we don't read
    if (m_Memory == RHIMemoryUsage::GPU_To_CPU)
    {
        readRange.End = m_Size; // Full range for readback
    }

    HRESULT hr = m_Resource->Map(0, &readRange, &m_MappedData);
    if (FAILED(hr))
    {
        m_MappedData = nullptr;
    }
    return m_MappedData;
}

void D3D12Buffer::Unmap()
{
    if (!m_MappedData)
        return;

    D3D12_RANGE writtenRange = {};
    if (m_Memory == RHIMemoryUsage::CPU_To_GPU)
    {
        writtenRange.End = m_Size; // Full range for upload
    }

    m_Resource->Unmap(0, &writtenRange);
    m_MappedData = nullptr;
}

void D3D12Buffer::Update(const void* data, usize size, usize offset)
{
    if (!data || size == 0 || !m_Resource || offset >= m_Size)
        return;

    const usize uploadSize = std::min(size, m_Size - offset);
    if (uploadSize == 0)
        return;

    if (m_Memory == RHIMemoryUsage::CPU_To_GPU)
    {
        void* mapped = Map();
        if (mapped)
        {
            memcpy(static_cast<uint8*>(mapped) + offset, data, uploadSize);
            Unmap();
        }
        return;
    }

    if (m_Memory != RHIMemoryUsage::GPU_Only || !m_Device)
        return;

    ID3D12Device* d3dDevice = m_Device->GetDevice();
    ID3D12CommandQueue* commandQueue = m_Device->GetCommandQueue();
    if (!d3dDevice || !commandQueue)
        return;

    ComPtr<ID3D12CommandAllocator> tempAllocator;
    HRESULT hr = d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAllocator));
    if (FAILED(hr))
        return;

    ComPtr<ID3D12GraphicsCommandList> tempCmdList;
    hr = d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAllocator.Get(), nullptr,
                                      IID_PPV_ARGS(&tempCmdList));
    if (FAILED(hr))
        return;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = d3dDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr))
        return;

    void* mappedUpload = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = uploadBuffer->Map(0, &readRange, &mappedUpload);
    if (FAILED(hr) || !mappedUpload)
        return;
    memcpy(mappedUpload, data, uploadSize);
    uploadBuffer->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER toCopyDest = {};
    toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopyDest.Transition.pResource = m_Resource.Get();
    toCopyDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tempCmdList->ResourceBarrier(1, &toCopyDest);

    tempCmdList->CopyBufferRegion(m_Resource.Get(), offset, uploadBuffer.Get(), 0, uploadSize);

    D3D12_RESOURCE_BARRIER backToCommon = {};
    backToCommon.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    backToCommon.Transition.pResource = m_Resource.Get();
    backToCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    backToCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    backToCommon.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tempCmdList->ResourceBarrier(1, &backToCommon);

    tempCmdList->Close();

    ID3D12CommandList* cmdLists[] = {tempCmdList.Get()};
    commandQueue->ExecuteCommandLists(1, cmdLists);

    ComPtr<ID3D12Fence> fence;
    hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (SUCCEEDED(hr))
    {
        const UINT64 fenceValue = 1;
        HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (eventHandle)
        {
            commandQueue->Signal(fence.Get(), fenceValue);
            if (fence->GetCompletedValue() < fenceValue)
            {
                fence->SetEventOnCompletion(fenceValue, eventHandle);
                WaitForSingleObject(eventHandle, INFINITE);
            }
            CloseHandle(eventHandle);
        }
    }

    m_Device->KeepAlive(uploadBuffer);
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
