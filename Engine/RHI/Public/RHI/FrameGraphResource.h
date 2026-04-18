// =============================================================================
// Dot Engine - Frame Graph Resource
// =============================================================================
// Virtual resources, versions, physical allocations, and access metadata.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include "RHI/RHIBuffer.h"
#include "RHI/RHITexture.h"
#include "RHI/RHITypes.h"

#include <string>
#include <vector>

namespace Dot
{

struct FrameGraphResourceHandle
{
    uint32 index = UINT32_MAX;

    bool IsValid() const { return index != UINT32_MAX; }
    bool operator==(const FrameGraphResourceHandle& other) const { return index == other.index; }
    bool operator!=(const FrameGraphResourceHandle& other) const { return index != other.index; }
};

using FrameGraphTextureHandle = FrameGraphResourceHandle;
using FrameGraphBufferHandle = FrameGraphResourceHandle;

enum class FrameGraphResourceType : uint8
{
    Buffer,
    Texture,
};

enum class FrameGraphResourceAccess : uint8
{
    None = 0,
    Read,
    Write,
    ReadWrite,
};

enum class FrameGraphResourceUsage : uint8
{
    Unknown = 0,
    ShaderRead,
    ColorAttachment,
    DepthStencilRead,
    DepthStencilWrite,
    Storage,
    CopySource,
    CopyDest,
    Present,
};

struct FrameGraphResourceDesc
{
    std::string name;
    FrameGraphResourceType type = FrameGraphResourceType::Texture;

    uint32 width = 0;
    uint32 height = 0;
    uint32 depth = 1;
    uint32 mipLevels = 1;
    uint32 arrayLayers = 1;
    uint32 sampleCount = 1;
    RHIFormat format = RHIFormat::R8G8B8A8_UNORM;
    RHITextureType textureType = RHITextureType::Texture2D;
    RHITextureUsage usage = RHITextureUsage::Sampled;

    usize bufferSize = 0;
    usize bufferStride = 0;
    RHIBufferUsage bufferUsage = RHIBufferUsage::Uniform;
    RHIMemoryUsage bufferMemory = RHIMemoryUsage::GPU_Only;

    bool imported = false;
    bool hasClearValue = false;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepth = 1.0f;
    uint8 clearStencil = 0;
    RHIResourceState initialState = RHIResourceState::Common;

    static FrameGraphResourceDesc Texture2D(const char* name, uint32 w, uint32 h, RHIFormat fmt,
                                            RHITextureUsage usageFlags = RHITextureUsage::Sampled)
    {
        FrameGraphResourceDesc desc;
        desc.name = name;
        desc.type = FrameGraphResourceType::Texture;
        desc.width = w;
        desc.height = h;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.sampleCount = 1;
        desc.format = fmt;
        desc.textureType = RHITextureType::Texture2D;
        desc.usage = usageFlags;
        desc.initialState = (static_cast<uint8>(usageFlags) & static_cast<uint8>(RHITextureUsage::DepthStencil)) != 0
                                ? RHIResourceState::DepthWrite
                                : ((static_cast<uint8>(usageFlags) & static_cast<uint8>(RHITextureUsage::RenderTarget)) != 0
                                       ? RHIResourceState::RenderTarget
                                       : RHIResourceState::ShaderResource);
        return desc;
    }

    static FrameGraphResourceDesc ImportedTexture(const char* name, uint32 w, uint32 h, RHIFormat fmt,
                                                  RHITextureUsage usageFlags = RHITextureUsage::Sampled,
                                                  RHIResourceState initial = RHIResourceState::Common)
    {
        FrameGraphResourceDesc desc = Texture2D(name, w, h, fmt, usageFlags);
        desc.imported = true;
        desc.initialState = initial;
        return desc;
    }

    static FrameGraphResourceDesc Buffer(const char* name, usize size, RHIBufferUsage usage,
                                         RHIMemoryUsage memory = RHIMemoryUsage::GPU_Only, usize stride = 0)
    {
        FrameGraphResourceDesc desc;
        desc.name = name;
        desc.type = FrameGraphResourceType::Buffer;
        desc.bufferSize = size;
        desc.bufferStride = stride;
        desc.bufferUsage = usage;
        desc.bufferMemory = memory;
        desc.initialState = RHIResourceState::Common;
        return desc;
    }

    static FrameGraphResourceDesc ImportedBuffer(const char* name, usize size, RHIBufferUsage usage,
                                                 RHIMemoryUsage memory = RHIMemoryUsage::GPU_Only, usize stride = 0,
                                                 RHIResourceState initial = RHIResourceState::Common)
    {
        FrameGraphResourceDesc desc = Buffer(name, size, usage, memory, stride);
        desc.imported = true;
        desc.initialState = initial;
        return desc;
    }
};

struct FrameGraphBarrier
{
    FrameGraphResourceHandle handle;
    RHIResourceState before = RHIResourceState::Unknown;
    RHIResourceState after = RHIResourceState::Unknown;
};

struct FrameGraphResourceUse
{
    FrameGraphResourceHandle handle;
    FrameGraphResourceAccess access = FrameGraphResourceAccess::None;
    FrameGraphResourceUsage usage = FrameGraphResourceUsage::Unknown;
    RHIResourceState state = RHIResourceState::Unknown;
};

struct FrameGraphPhysicalResource
{
    uint32 index = UINT32_MAX;
    FrameGraphResourceType type = FrameGraphResourceType::Texture;
    FrameGraphResourceDesc desc;
    bool imported = false;
    uint64 signature = 0;
    RHITexturePtr texture;
    RHIBufferPtr buffer;
    uint32 firstPass = UINT32_MAX;
    uint32 lastPass = 0;
};

struct FrameGraphResource
{
    FrameGraphResourceDesc desc;
    FrameGraphResourceHandle handle;

    uint32 logicalId = UINT32_MAX;
    uint32 version = 0;
    FrameGraphResourceHandle parent;
    bool imported = false;

    uint32 producerPass = UINT32_MAX;
    std::vector<uint32> consumerPasses;
    uint32 firstPass = UINT32_MAX;
    uint32 lastPass = 0;

    int physicalIndex = -1;
    RHIResourceState initialState = RHIResourceState::Unknown;
    RHIResourceState finalState = RHIResourceState::Unknown;
    RHITexturePtr texture;
    RHIBufferPtr buffer;
};

} // namespace Dot
