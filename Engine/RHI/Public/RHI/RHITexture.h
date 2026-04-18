// =============================================================================
// Dot Engine - RHI Texture
// =============================================================================
// Abstract GPU texture interface.
// =============================================================================

#pragma once

#include "RHI/RHITypes.h"

#include <memory>


namespace Dot
{

/// Abstract GPU texture
class RHITexture
{
public:
    virtual ~RHITexture() = default;

    // Texture info
    virtual uint32 GetWidth() const = 0;
    virtual uint32 GetHeight() const = 0;
    virtual uint32 GetDepth() const = 0;
    virtual uint32 GetMipLevels() const = 0;
    virtual RHIFormat GetFormat() const = 0;
    virtual RHITextureType GetType() const = 0;

    // Data operations
    virtual void Update(const void* data, uint32 mipLevel = 0, uint32 arrayLayer = 0) = 0;

protected:
    RHITexture() = default;
};

using RHITexturePtr = std::shared_ptr<RHITexture>;

/// Abstract GPU sampler
class RHISampler
{
public:
    virtual ~RHISampler() = default;

protected:
    RHISampler() = default;
};

using RHISamplerPtr = std::shared_ptr<RHISampler>;

} // namespace Dot
