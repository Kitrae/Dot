// =============================================================================
// Dot Engine - RHI
// =============================================================================
// Main RHI include header.
// =============================================================================

#pragma once

#include "RHI/RHIBuffer.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHIGUI.h"
#include "RHI/RHIPipeline.h"
#include "RHI/RHIShader.h"
#include "RHI/RHISwapChain.h"
#include "RHI/RHITexture.h"
#include "RHI/RHITypes.h"

namespace Dot
{

/// Create the default RHI device for the current platform
/// Windows: D3D12
RHIDevicePtr CreateRHIDevice();

} // namespace Dot
