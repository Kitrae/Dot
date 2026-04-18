// =============================================================================
// Dot Engine - D3D12 Device Implementation
// =============================================================================

#include "D3D12Device.h"

#include "D3D12Buffer.h"
#include "D3D12Pipeline.h"
#include "D3D12Shader.h"
#include "D3D12SwapChain.h"
#include "D3D12Texture.h"

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

static D3D12_RESOURCE_STATES ToD3D12ResourceState(RHIResourceState state)
{
    switch (state)
    {
        case RHIResourceState::Common:
            return D3D12_RESOURCE_STATE_COMMON;
        case RHIResourceState::RenderTarget:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHIResourceState::DepthWrite:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHIResourceState::DepthRead:
            return D3D12_RESOURCE_STATE_DEPTH_READ;
        case RHIResourceState::ShaderResource:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case RHIResourceState::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHIResourceState::CopySource:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHIResourceState::CopyDest:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHIResourceState::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
        case RHIResourceState::Unknown:
        default:
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

static DXGI_FORMAT ConvertIndexFormat(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R16_UINT:
            return DXGI_FORMAT_R16_UINT;
        case RHIFormat::R32_UINT:
            return DXGI_FORMAT_R32_UINT;
        default:
            return DXGI_FORMAT_R16_UINT;
    }
}

D3D12Device::D3D12Device()
{
    CreateDeviceAndQueue();
    CreateCommandObjects();
}

D3D12Device::~D3D12Device()
{
    WaitForGPU();

    if (m_FenceEvent)
    {
        CloseHandle(m_FenceEvent);
    }
}

bool D3D12Device::CreateDeviceAndQueue()
{
    UINT dxgiFlags = 0;

    #if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    #endif

    HRESULT hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_Factory));
    if (!CheckHR(hr, "CreateDXGIFactory2"))
        return false;

    hr = m_Factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_Adapter));

    if (FAILED(hr))
    {
        ComPtr<IDXGIAdapter1> adapter1;
        m_Factory->EnumAdapters1(0, &adapter1);
        adapter1.As(&m_Adapter);
    }

    DXGI_ADAPTER_DESC3 adapterDesc;
    m_Adapter->GetDesc3(&adapterDesc);

    char name[256];
    size_t converted;
    wcstombs_s(&converted, name, adapterDesc.Description, 255);
    m_DeviceName = name;

    switch (adapterDesc.VendorId)
    {
        case 0x10DE:
            m_VendorName = "NVIDIA";
            break;
        case 0x1002:
            m_VendorName = "AMD";
            break;
        case 0x8086:
            m_VendorName = "Intel";
            break;
        default:
            m_VendorName = "Unknown";
            break;
    }

    hr = D3D12CreateDevice(m_Adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device));
    if (!CheckHR(hr, "D3D12CreateDevice"))
        return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue));
    if (!CheckHR(hr, "CreateCommandQueue"))
        return false;

    return true;
}

bool D3D12Device::CreateCommandObjects()
{
    HRESULT hr = m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocator));
    if (!CheckHR(hr, "CreateCommandAllocator"))
        return false;

    hr = m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), nullptr,
                                     IID_PPV_ARGS(&m_CommandList));
    if (!CheckHR(hr, "CreateCommandList"))
        return false;

    m_CommandList->Close();

    hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
    if (!CheckHR(hr, "CreateFence"))
        return false;

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (!CreateTimestampResources())
        return false;

    return true;
}

bool D3D12Device::CreateTimestampResources()
{
    constexpr uint32 kMaxTimestampQueries = 512;

    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Count = kMaxTimestampQueries;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    HRESULT hr = m_Device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_TimestampQueryHeap));
    if (!CheckHR(hr, "CreateQueryHeap"))
        return false;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(uint64) * kMaxTimestampQueries;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = m_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                           IID_PPV_ARGS(&m_TimestampReadbackBuffer));
    if (!CheckHR(hr, "CreateTimestampReadbackBuffer"))
        return false;

    m_CommandQueue->GetTimestampFrequency(&m_TimestampFrequency);
    return true;
}

void D3D12Device::WaitForGPU()
{
    m_FenceValue++;
    m_CommandQueue->Signal(m_Fence.Get(), m_FenceValue);

    if (m_Fence->GetCompletedValue() < m_FenceValue)
    {
        m_Fence->SetEventOnCompletion(m_FenceValue, m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
}

// =============================================================================
// Resource Creation
// =============================================================================

RHIBufferPtr D3D12Device::CreateBuffer(const RHIBufferDesc& desc)
{
    return std::make_shared<D3D12Buffer>(this, desc);
}

RHITexturePtr D3D12Device::CreateTexture(const RHITextureDesc& desc)
{
    return std::make_shared<D3D12Texture>(this, desc);
}

RHISamplerPtr D3D12Device::CreateSampler(const RHISamplerDesc& desc)
{
    return std::make_shared<D3D12Sampler>(this, desc);
}

RHIShaderPtr D3D12Device::CreateShader(const RHIShaderBytecode& bytecode)
{
    return std::make_shared<D3D12Shader>(this, bytecode);
}

RHIShaderProgramPtr D3D12Device::CreateShaderProgram(RHIShaderPtr vertex, RHIShaderPtr fragment)
{
    return std::make_shared<D3D12ShaderProgram>(std::move(vertex), std::move(fragment));
}

RHISwapChainPtr D3D12Device::CreateSwapChain(const RHISwapChainDesc& desc)
{
    return std::make_shared<D3D12SwapChain>(this, desc);
}

RHIPipelinePtr D3D12Device::CreatePipeline(const RHIPipelineDesc& desc)
{
    return std::make_shared<D3D12Pipeline>(this, desc);
}

void D3D12Device::UpdateTexture(RHITexturePtr texture, const void* data)
{
    if (texture)
    {
        texture->Update(data);
    }
}

// =============================================================================
// Frame Operations
// =============================================================================

void D3D12Device::BeginFrame()
{
    m_Garbage.clear();
    m_TimestampQueryCount = 0;
    m_TimestampSpans.clear();
    m_CommandAllocator->Reset();
    m_CommandList->Reset(m_CommandAllocator.Get(), nullptr);
}

void D3D12Device::EndFrame()
{
    if (m_TimestampQueryHeap && m_TimestampReadbackBuffer && m_TimestampQueryCount > 0)
    {
        m_CommandList->ResolveQueryData(m_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_TimestampQueryCount,
                                        m_TimestampReadbackBuffer.Get(), 0);
    }
    m_CommandList->Close();
}

void D3D12Device::Submit()
{
    ID3D12CommandList* cmdLists[] = {m_CommandList.Get()};
    m_CommandQueue->ExecuteCommandLists(1, cmdLists);
}

void D3D12Device::Present()
{
    if (m_CurrentSwapChain)
    {
        m_CurrentSwapChain->Present();
    }
    WaitForGPU();
    ResolveTimestampResults();
}

uint32 D3D12Device::BeginTimestamp(const char* label)
{
    if (!m_TimestampQueryHeap || !m_CommandList)
        return UINT32_MAX;
    if (m_TimestampQueryCount + 1 >= 512)
        return UINT32_MAX;

    TimestampSpan span;
    span.label = label ? label : "UnnamedPass";
    span.beginIndex = m_TimestampQueryCount++;
    span.endIndex = m_TimestampQueryCount++;
    m_TimestampSpans.push_back(span);
    m_CommandList->EndQuery(m_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, span.beginIndex);
    return static_cast<uint32>(m_TimestampSpans.size() - 1);
}

void D3D12Device::EndTimestamp(uint32 handle)
{
    if (!m_TimestampQueryHeap || !m_CommandList || handle == UINT32_MAX || handle >= m_TimestampSpans.size())
        return;

    const TimestampSpan& span = m_TimestampSpans[handle];
    m_CommandList->EndQuery(m_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, span.endIndex);
}

double D3D12Device::GetLastTimestampMs(const char* label) const
{
    if (!label)
        return 0.0;

    auto it = m_LastTimestampResults.find(label);
    return it != m_LastTimestampResults.end() ? it->second : 0.0;
}

RHITexturePtr D3D12Device::WrapNativeTexture(void* nativeResource, const RHITextureDesc& desc,
                                             RHIResourceState initialState)
{
    if (!nativeResource)
        return nullptr;

    return std::make_shared<D3D12Texture>(this, static_cast<ID3D12Resource*>(nativeResource), desc, initialState);
}

void D3D12Device::ResolveTimestampResults()
{
    if (!m_TimestampReadbackBuffer || m_TimestampSpans.empty() || m_TimestampFrequency == 0)
        return;

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, sizeof(uint64) * m_TimestampQueryCount};
    if (FAILED(m_TimestampReadbackBuffer->Map(0, &readRange, &mapped)) || !mapped)
        return;

    const uint64* timestamps = static_cast<const uint64*>(mapped);
    m_LastTimestampResults.clear();
    for (const TimestampSpan& span : m_TimestampSpans)
    {
        if (span.beginIndex >= m_TimestampQueryCount || span.endIndex >= m_TimestampQueryCount)
            continue;
        const uint64 begin = timestamps[span.beginIndex];
        const uint64 end = timestamps[span.endIndex];
        if (end < begin)
            continue;
        const double elapsedMs =
            (static_cast<double>(end - begin) * 1000.0) / static_cast<double>(m_TimestampFrequency);
        m_LastTimestampResults[span.label] = elapsedMs;
    }

    D3D12_RANGE writeRange = {0, 0};
    m_TimestampReadbackBuffer->Unmap(0, &writeRange);
}

void* D3D12Device::GetNativeTextureResource(RHITexture* texture)
{
    if (!texture)
        return nullptr;

    // Cast to D3D12Texture and get the native resource
    D3D12Texture* d3d12Texture = static_cast<D3D12Texture*>(texture);
    return d3d12Texture->GetResource();
}

// =============================================================================
// Render Commands
// =============================================================================

void D3D12Device::SetPipeline(RHIPipelinePtr pipeline)
{
    auto* d3d12Pipeline = dynamic_cast<D3D12Pipeline*>(pipeline.get());
    if (d3d12Pipeline)
    {
        m_CurrentPipeline = d3d12Pipeline;
        m_CommandList->SetGraphicsRootSignature(d3d12Pipeline->GetRootSignature());
        m_CommandList->SetPipelineState(d3d12Pipeline->GetPipelineState());
        m_CommandList->IASetPrimitiveTopology(d3d12Pipeline->GetTopology());
    }
}

void D3D12Device::SetVertexBuffer(RHIBufferPtr buffer, uint32 slot)
{
    auto* d3d12Buffer = dynamic_cast<D3D12Buffer*>(buffer.get());
    if (d3d12Buffer && m_CurrentPipeline)
    {
        D3D12_VERTEX_BUFFER_VIEW vbView = {};
        vbView.BufferLocation = d3d12Buffer->GetResource()->GetGPUVirtualAddress();
        vbView.SizeInBytes = static_cast<UINT>(d3d12Buffer->GetSize());
        vbView.StrideInBytes = m_CurrentPipeline->GetVertexStride();
        m_CommandList->IASetVertexBuffers(slot, 1, &vbView);
    }
}

void D3D12Device::SetIndexBuffer(RHIBufferPtr buffer, RHIFormat format)
{
    auto* d3d12Buffer = dynamic_cast<D3D12Buffer*>(buffer.get());
    if (d3d12Buffer)
    {
        D3D12_INDEX_BUFFER_VIEW ibView = {};
        ibView.BufferLocation = d3d12Buffer->GetResource()->GetGPUVirtualAddress();
        ibView.SizeInBytes = static_cast<UINT>(d3d12Buffer->GetSize());
        ibView.Format = ConvertIndexFormat(format);
        m_CommandList->IASetIndexBuffer(&ibView);
    }
}

void D3D12Device::SetConstantBuffer(RHIBufferPtr buffer, uint32 slot)
{
    auto* d3d12Buffer = dynamic_cast<D3D12Buffer*>(buffer.get());
    if (d3d12Buffer)
    {
        m_CommandList->SetGraphicsRootConstantBufferView(slot, d3d12Buffer->GetResource()->GetGPUVirtualAddress());
    }
}

void D3D12Device::SetRenderTarget(RHITexturePtr color, RHITexturePtr depth)
{
    auto* colorTexture = dynamic_cast<D3D12Texture*>(color.get());
    auto* depthTexture = dynamic_cast<D3D12Texture*>(depth.get());

    if (colorTexture)
    {
        TransitionTexture(color, colorTexture->GetCurrentState(), RHIResourceState::RenderTarget);
        m_CurrentRTV = colorTexture->GetRTV();
    }

    if (depthTexture)
    {
        TransitionTexture(depth, depthTexture->GetCurrentState(), RHIResourceState::DepthWrite);
        m_CurrentDSV = depthTexture->GetDSV();
    }

    if (m_CurrentRTV.ptr != 0)
    {
        if (m_CurrentDSV.ptr != 0)
            m_CommandList->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, &m_CurrentDSV);
        else
            m_CommandList->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
    }
}

void D3D12Device::SetSwapChainRenderTarget(RHISwapChainPtr swapChain, RHITexturePtr depth)
{
    (void)depth;
    auto* d3d12SwapChain = dynamic_cast<D3D12SwapChain*>(swapChain.get());
    if (d3d12SwapChain)
    {
        m_CurrentSwapChain = d3d12SwapChain;
        m_CurrentRTV = d3d12SwapChain->GetCurrentRTV();
        m_CurrentDSV = d3d12SwapChain->GetDSV();

        // Transition to render target
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = d3d12SwapChain->GetCurrentBackBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_CommandList->ResourceBarrier(1, &barrier);

        m_CommandList->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, &m_CurrentDSV);
    }
}

void D3D12Device::ClearRenderTarget(float r, float g, float b, float a)
{
    float color[] = {r, g, b, a};
    m_CommandList->ClearRenderTargetView(m_CurrentRTV, color, 0, nullptr);
}

void D3D12Device::ClearDepthStencil(float depth, uint8 stencil)
{
    if (m_CurrentDSV.ptr != 0)
    {
        m_CommandList->ClearDepthStencilView(m_CurrentDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth,
                                             stencil, 0, nullptr);
    }
}

void D3D12Device::TransitionTexture(RHITexturePtr texture, RHIResourceState before, RHIResourceState after)
{
    auto* d3d12Texture = dynamic_cast<D3D12Texture*>(texture.get());
    if (!d3d12Texture || !d3d12Texture->GetResource() || before == after || after == RHIResourceState::Unknown)
        return;

    if (before == RHIResourceState::Unknown)
        before = d3d12Texture->GetCurrentState();
    if (before == after)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = d3d12Texture->GetResource();
    barrier.Transition.StateBefore = ToD3D12ResourceState(before);
    barrier.Transition.StateAfter = ToD3D12ResourceState(after);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_CommandList->ResourceBarrier(1, &barrier);
    d3d12Texture->SetCurrentState(after);
}

void D3D12Device::TransitionBuffer(RHIBufferPtr buffer, RHIResourceState before, RHIResourceState after)
{
    auto* d3d12Buffer = dynamic_cast<D3D12Buffer*>(buffer.get());
    if (!d3d12Buffer || !d3d12Buffer->GetResource() || before == after || after == RHIResourceState::Unknown)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = d3d12Buffer->GetResource();
    barrier.Transition.StateBefore = ToD3D12ResourceState(before);
    barrier.Transition.StateAfter = ToD3D12ResourceState(after);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_CommandList->ResourceBarrier(1, &barrier);
}

void D3D12Device::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
    D3D12_VIEWPORT viewport = {x, y, width, height, minDepth, maxDepth};
    m_CommandList->RSSetViewports(1, &viewport);
}

void D3D12Device::SetScissor(int32 x, int32 y, int32 width, int32 height)
{
    D3D12_RECT scissor = {x, y, x + width, y + height};
    m_CommandList->RSSetScissorRects(1, &scissor);
}

void D3D12Device::Draw(uint32 vertexCount, uint32 firstVertex)
{
    m_CommandList->DrawInstanced(vertexCount, 1, firstVertex, 0);
}

void D3D12Device::DrawIndexed(uint32 indexCount, uint32 firstIndex, int32 vertexOffset)
{
    m_CommandList->DrawIndexedInstanced(indexCount, 1, firstIndex, vertexOffset, 0);
}

void D3D12Device::DrawInstanced(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance)
{
    m_CommandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

RHIDevicePtr CreateD3D12Device()
{
    return std::make_shared<D3D12Device>();
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
