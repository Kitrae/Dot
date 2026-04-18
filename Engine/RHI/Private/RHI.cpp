// =============================================================================
// Dot Engine - RHI Module
// =============================================================================
// Factory functions for creating RHI objects.
// =============================================================================

#include "RHI/RHI.h"

#ifdef DOT_PLATFORM_WINDOWS
    #include "D3D12/D3D12Device.h"
#endif

namespace Dot
{

RHIDevicePtr CreateRHIDevice()
{
#ifdef DOT_PLATFORM_WINDOWS
    return CreateD3D12Device();
#else
    return nullptr;
#endif
}

} // namespace Dot
