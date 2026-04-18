// =============================================================================
// Dot Engine - D3D12 Common
// =============================================================================
// Common D3D12 includes and utilities.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include "RHI/RHITypes.h"

#ifdef DOT_PLATFORM_WINDOWS

    #include <d3d12.h>
    #include <d3dcompiler.h>
    #include <dxgi1_6.h>
    #include <wrl/client.h>

    // Link libraries
    #pragma comment(lib, "d3d12.lib")
    #pragma comment(lib, "dxgi.lib")
    #pragma comment(lib, "d3dcompiler.lib")

namespace Dot
{

using Microsoft::WRL::ComPtr;

/// Convert RHI format to DXGI format
inline DXGI_FORMAT ToDXGIFormat(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R8_UNORM:
            return DXGI_FORMAT_R8_UNORM;
        case RHIFormat::R8G8_UNORM:
            return DXGI_FORMAT_R8G8_UNORM;
        case RHIFormat::R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::B8G8R8A8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::R16_FLOAT:
            return DXGI_FORMAT_R16_FLOAT;
        case RHIFormat::R16G16_FLOAT:
            return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::R16G16B16A16_FLOAT:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::R32G32_FLOAT:
            return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::R32G32B32_FLOAT:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::D16_UNORM:
            return DXGI_FORMAT_D16_UNORM;
        case RHIFormat::D24_UNORM_S8_UINT:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32_FLOAT:
            return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::D32_FLOAT_S8_UINT:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

/// Check HRESULT and log on failure
inline bool CheckHR(HRESULT hr, const char* operation)
{
    (void)operation; // TODO: Use for logging
    if (FAILED(hr))
    {
        return false;
    }
    return true;
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
