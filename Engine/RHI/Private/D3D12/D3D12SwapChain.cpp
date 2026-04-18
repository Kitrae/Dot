// =============================================================================
// Dot Engine - D3D12 SwapChain Implementation
// =============================================================================

#include "D3D12SwapChain.h"

#include "D3D12Device.h"

#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

D3D12SwapChain::D3D12SwapChain(D3D12Device* device, const RHISwapChainDesc& desc)
    : m_Device(device), m_Width(desc.Width), m_Height(desc.Height), m_BufferCount(desc.BufferCount),
      m_Format(DXGI_FORMAT_R8G8B8A8_UNORM), m_VSync(desc.VSync)
{
    CreateSwapChain(static_cast<HWND>(desc.WindowHandle));
    CreateRTVs();
    CreateDepthBuffer();
}

D3D12SwapChain::~D3D12SwapChain()
{
    ReleaseBuffers();
}

void D3D12SwapChain::CreateSwapChain(HWND hwnd)
{
    // Get the factory from the device's adapter
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (!CheckHR(hr, "CreateDXGIFactory2 for SwapChain"))
        return;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_Width;
    swapChainDesc.Height = m_Height;
    swapChainDesc.Format = m_Format;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = m_BufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(m_Device->GetCommandQueue(), hwnd, &swapChainDesc,
                                         nullptr, // fullscreen desc
                                         nullptr, // restrict output
                                         &swapChain1);

    if (!CheckHR(hr, "CreateSwapChainForHwnd"))
        return;

    // Disable Alt+Enter fullscreen toggle
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Get IDXGISwapChain4 interface
    swapChain1.As(&m_SwapChain);

    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void D3D12SwapChain::CreateRTVs()
{
    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = m_BufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = m_Device->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RTVHeap));
    if (!CheckHR(hr, "CreateDescriptorHeap for RTV"))
        return;

    m_RTVDescriptorSize = m_Device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create RTVs for each back buffer
    m_BackBuffers.resize(m_BufferCount);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < m_BufferCount; ++i)
    {
        hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
        CheckHR(hr, "GetBuffer");

        m_Device->GetDevice()->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_RTVDescriptorSize;
    }
}

void D3D12SwapChain::CreateDepthBuffer()
{
    // Create DSV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = m_Device->GetDevice()->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap));
    if (!CheckHR(hr, "CreateDescriptorHeap for DSV"))
        return;

    // Create SRV descriptor heap for depth sampling
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // Staging heap for copying to shader-visible heaps

    hr = m_Device->GetDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_DepthSRVHeap));
    if (!CheckHR(hr, "CreateDescriptorHeap for Depth SRV"))
        return;

    // Create depth buffer resource
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = m_Width;
    depthDesc.Height = m_Height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    // Use TYPELESS format for the resource so we can create both DSV and SRV
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT; // Actual depth format for DSV
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    hr = m_Device->GetDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                                        IID_PPV_ARGS(&m_DepthBuffer));
    if (!CheckHR(hr, "CreateCommittedResource for depth buffer"))
        return;

    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    m_Device->GetDevice()->CreateDepthStencilView(m_DepthBuffer.Get(), &dsvDesc,
                                                  m_DSVHeap->GetCPUDescriptorHandleForHeapStart());

    // Create SRV for depth sampling
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    m_Device->GetDevice()->CreateShaderResourceView(m_DepthBuffer.Get(), &srvDesc,
                                                    m_DepthSRVHeap->GetCPUDescriptorHandleForHeapStart());
}

void D3D12SwapChain::ReleaseBuffers()
{
    m_DepthBuffer.Reset();
    m_DSVHeap.Reset();
    m_DepthSRVHeap.Reset();
    for (auto& buffer : m_BackBuffers)
    {
        buffer.Reset();
    }
    m_BackBuffers.clear();
}

ID3D12Resource* D3D12SwapChain::GetCurrentBackBuffer() const
{
    return m_BackBuffers[m_CurrentBackBufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12SwapChain::GetCurrentRTV() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(m_CurrentBackBufferIndex) * m_RTVDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12SwapChain::GetDSV() const
{
    return m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3D12SwapChain::SetClearColor(float r, float g, float b, float a)
{
    m_ClearColor[0] = r;
    m_ClearColor[1] = g;
    m_ClearColor[2] = b;
    m_ClearColor[3] = a;
}

void D3D12SwapChain::GetClearColor(float& r, float& g, float& b, float& a) const
{
    r = m_ClearColor[0];
    g = m_ClearColor[1];
    b = m_ClearColor[2];
    a = m_ClearColor[3];
}

void* D3D12SwapChain::GetDepthSRV() const
{
    return m_DepthSRVHeap.Get();
}

void D3D12SwapChain::BeginFrame()
{
    ID3D12GraphicsCommandList* cmdList = m_Device->GetCommandList();
    ID3D12Resource* backBuffer = GetCurrentBackBuffer();

    // Transition back buffer to render target
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // Clear the back buffer and depth buffer
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetDSV();
    cmdList->ClearRenderTargetView(rtv, m_ClearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void D3D12SwapChain::EndFrame()
{
    ID3D12GraphicsCommandList* cmdList = m_Device->GetCommandList();
    ID3D12Resource* backBuffer = GetCurrentBackBuffer();

    // Transition back buffer to present
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void D3D12SwapChain::Present()
{
    UINT syncInterval = m_VSync ? 1 : 0;
    m_SwapChain->Present(syncInterval, 0);
    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void D3D12SwapChain::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    m_Width = width;
    m_Height = height;

    // Release old buffers
    ReleaseBuffers();

    // Resize swap chain
    HRESULT hr = m_SwapChain->ResizeBuffers(m_BufferCount, m_Width, m_Height, m_Format, 0);
    CheckHR(hr, "ResizeBuffers");

    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    // Recreate RTVs and depth buffer
    CreateRTVs();
    CreateDepthBuffer();
}

// Factory function implementation
RHISwapChainPtr CreateSwapChain(RHIDevice* device, const RHISwapChainDesc& desc)
{
    // For now, only D3D12 on Windows
    auto* d3d12Device = static_cast<D3D12Device*>(device);
    return std::make_shared<D3D12SwapChain>(d3d12Device, desc);
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
