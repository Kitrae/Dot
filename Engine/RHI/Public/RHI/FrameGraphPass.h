// =============================================================================
// Dot Engine - Frame Graph Pass
// =============================================================================
// Pass declarations, resource uses, and compiled metadata.
// =============================================================================

#pragma once

#include "RHI/FrameGraphResource.h"

#include <functional>
#include <string>
#include <vector>

namespace Dot
{

class FrameGraph;
class RHIDevice;

struct FrameGraphPassData
{
    virtual ~FrameGraphPassData() = default;
};

class FrameGraphPassBuilder
{
public:
    FrameGraphPassBuilder(FrameGraph& graph, uint32 passIndex) : m_Graph(graph), m_PassIndex(passIndex) {}

    FrameGraphResourceHandle Create(const FrameGraphResourceDesc& desc);
    FrameGraphTextureHandle CreateTexture(const FrameGraphResourceDesc& desc)
    {
        return Create(desc);
    }
    FrameGraphBufferHandle CreateBuffer(const FrameGraphResourceDesc& desc)
    {
        return Create(desc);
    }

    FrameGraphTextureHandle ImportTexture(const FrameGraphResourceDesc& desc, RHITexturePtr texture = nullptr);
    FrameGraphBufferHandle ImportBuffer(const FrameGraphResourceDesc& desc, RHIBufferPtr buffer = nullptr);

    FrameGraphResourceHandle Read(FrameGraphResourceHandle handle,
                                  FrameGraphResourceUsage usage = FrameGraphResourceUsage::ShaderRead);
    FrameGraphResourceHandle Write(FrameGraphResourceHandle handle,
                                   FrameGraphResourceUsage usage = FrameGraphResourceUsage::ColorAttachment);
    FrameGraphResourceHandle ReadWrite(FrameGraphResourceHandle handle,
                                       FrameGraphResourceUsage usage = FrameGraphResourceUsage::Storage);

    void SetHasSideEffects(bool enabled = true);

private:
    FrameGraph& m_Graph;
    uint32 m_PassIndex;
};

struct FrameGraphPass
{
    std::string name;
    uint32 index = 0;

    std::vector<FrameGraphResourceHandle> reads;
    std::vector<FrameGraphResourceHandle> writes;
    std::vector<FrameGraphResourceHandle> creates;
    std::vector<FrameGraphResourceUse> resourceUses;
    std::vector<uint32> dependencies;
    std::vector<FrameGraphBarrier> barriers;

    using ExecuteFunc = std::function<void(RHIDevice&, const FrameGraphPass&)>;
    ExecuteFunc execute;

    bool culled = false;
    bool hasSideEffects = false;
    uint32 refCount = 0;
    uint32 executionIndex = UINT32_MAX;
    double cpuTimeMs = 0.0;
    double gpuTimeMs = 0.0;
};

} // namespace Dot
