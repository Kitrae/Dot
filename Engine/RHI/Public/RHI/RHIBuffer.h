// =============================================================================
// Dot Engine - RHI Buffer
// =============================================================================
// Abstract GPU buffer interface.
// =============================================================================

#pragma once

#include "RHI/RHITypes.h"

#include <memory>


namespace Dot
{

/// Abstract GPU buffer
class RHIBuffer
{
public:
    virtual ~RHIBuffer() = default;

    // Buffer info
    virtual usize GetSize() const = 0;
    virtual RHIBufferUsage GetUsage() const = 0;

    // Data operations
    virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual void Update(const void* data, usize size, usize offset = 0) = 0;

protected:
    RHIBuffer() = default;
};

using RHIBufferPtr = std::shared_ptr<RHIBuffer>;

} // namespace Dot
