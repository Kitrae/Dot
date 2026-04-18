// =============================================================================
// Dot Engine - Frame Graph
// =============================================================================
// Shared render graph with virtual resources, versioning, lifetimes, culling,
// physical allocation, and barrier metadata.
// =============================================================================

#pragma once

#include "RHI/FrameGraphPass.h"
#include "RHI/FrameGraphResource.h"
#include "RHI/RHIDevice.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Dot
{

class FrameGraph
{
public:
    FrameGraph() = default;
    ~FrameGraph() = default;

    FrameGraph(const FrameGraph&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    template <typename SetupFunc, typename ExecuteFunc>
    void AddPass(const char* name, SetupFunc&& setup, ExecuteFunc&& execute)
    {
        uint32 passIndex = static_cast<uint32>(m_Passes.size());
        FrameGraphPass pass;
        pass.name = name;
        pass.index = passIndex;
        pass.execute = std::forward<ExecuteFunc>(execute);
        m_Passes.push_back(std::move(pass));

        FrameGraphPassBuilder builder(*this, passIndex);
        setup(builder);
    }

    void Compile();
    void Execute(RHIDevice& device);
    void Reset();

    FrameGraphResource* GetResource(FrameGraphResourceHandle handle);
    const FrameGraphResource* GetResource(FrameGraphResourceHandle handle) const;

    const std::vector<FrameGraphPass>& GetPasses() const { return m_Passes; }
    const std::vector<FrameGraphResource>& GetResources() const { return m_Resources; }
    const std::vector<FrameGraphPhysicalResource>& GetPhysicalResources() const { return m_PhysicalResources; }
    const std::vector<uint32>& GetExecutionOrder() const { return m_ExecutionOrder; }
    const std::vector<FrameGraphResourceHandle>& GetOutputs() const { return m_Outputs; }
    const std::vector<std::string>& GetValidationErrors() const { return m_ValidationErrors; }

    size_t GetPassCount() const { return m_Passes.size(); }
    size_t GetActivePassCount() const;
    size_t GetResourceCount() const { return m_Resources.size(); }
    size_t GetPhysicalResourceCount() const { return m_PhysicalResources.size(); }
    bool IsCompiled() const { return m_Compiled; }
    bool HasValidationErrors() const { return !m_ValidationErrors.empty(); }
    double GetLastCompileTimeMs() const { return m_LastCompileTimeMs; }
    double GetLastExecuteTimeMs() const { return m_LastExecuteTimeMs; }

    void SetOutput(FrameGraphResourceHandle handle);
    void ClearOutputs();

    FrameGraphTextureHandle ImportTexture(const FrameGraphResourceDesc& desc, RHITexturePtr texture = nullptr);
    FrameGraphBufferHandle ImportBuffer(const FrameGraphResourceDesc& desc, RHIBufferPtr buffer = nullptr);

private:
    friend class FrameGraphPassBuilder;

    FrameGraphResourceHandle CreateResource(const FrameGraphResourceDesc& desc, uint32 passIndex);
    FrameGraphResourceHandle ImportResource(const FrameGraphResourceDesc& desc, RHITexturePtr texture, RHIBufferPtr buffer);
    FrameGraphResourceHandle MarkRead(FrameGraphResourceHandle handle, uint32 passIndex, FrameGraphResourceUsage usage);
    FrameGraphResourceHandle MarkWrite(FrameGraphResourceHandle handle, uint32 passIndex, FrameGraphResourceUsage usage,
                                       FrameGraphResourceAccess access);
    void MarkSideEffects(uint32 passIndex, bool enabled);

    void BuildDependencies();
    void BuildExecutionOrder();
    void CullUnusedPasses();
    void ComputeResourceLifetimes();
    void AssignPhysicalResources();
    void BuildBarriers();
    void ValidateGraph();
    void AllocatePhysicalResources(RHIDevice& device);
    void AddValidationError(const std::string& message);

    uint64 ComputeResourceSignature(const FrameGraphResourceDesc& desc) const;
    static RHIResourceState ResolveResourceState(const FrameGraphResourceDesc& desc, FrameGraphResourceUsage usage,
                                                 FrameGraphResourceAccess access);
    static void AppendUniqueHandle(std::vector<FrameGraphResourceHandle>& handles, FrameGraphResourceHandle handle);
    static void AppendUniquePass(std::vector<uint32>& passes, uint32 passIndex);

    std::vector<FrameGraphPass> m_Passes;
    std::vector<FrameGraphResource> m_Resources;
    std::vector<FrameGraphPhysicalResource> m_PhysicalResources;
    std::vector<uint32> m_ExecutionOrder;
    std::vector<FrameGraphResourceHandle> m_Outputs;
    std::vector<std::string> m_ValidationErrors;

    uint32 m_NextLogicalId = 0;
    bool m_Compiled = false;
    double m_LastCompileTimeMs = 0.0;
    double m_LastExecuteTimeMs = 0.0;

    std::unordered_map<uint64, std::vector<RHITexturePtr>> m_TexturePool;
    std::unordered_map<uint64, std::vector<RHIBufferPtr>> m_BufferPool;
};

using RenderGraph = FrameGraph;
using RenderGraphBuilder = FrameGraphPassBuilder;

inline FrameGraphResourceHandle FrameGraphPassBuilder::Create(const FrameGraphResourceDesc& desc)
{
    auto handle = m_Graph.CreateResource(desc, m_PassIndex);
    m_Graph.m_Passes[m_PassIndex].creates.push_back(handle);
    return handle;
}

inline FrameGraphTextureHandle FrameGraphPassBuilder::ImportTexture(const FrameGraphResourceDesc& desc, RHITexturePtr texture)
{
    return m_Graph.ImportResource(desc, std::move(texture), nullptr);
}

inline FrameGraphBufferHandle FrameGraphPassBuilder::ImportBuffer(const FrameGraphResourceDesc& desc, RHIBufferPtr buffer)
{
    return m_Graph.ImportResource(desc, nullptr, std::move(buffer));
}

inline FrameGraphResourceHandle FrameGraphPassBuilder::Read(FrameGraphResourceHandle handle, FrameGraphResourceUsage usage)
{
    return m_Graph.MarkRead(handle, m_PassIndex, usage);
}

inline FrameGraphResourceHandle FrameGraphPassBuilder::Write(FrameGraphResourceHandle handle,
                                                             FrameGraphResourceUsage usage)
{
    return m_Graph.MarkWrite(handle, m_PassIndex, usage, FrameGraphResourceAccess::Write);
}

inline FrameGraphResourceHandle FrameGraphPassBuilder::ReadWrite(FrameGraphResourceHandle handle,
                                                                 FrameGraphResourceUsage usage)
{
    return m_Graph.MarkWrite(handle, m_PassIndex, usage, FrameGraphResourceAccess::ReadWrite);
}

inline void FrameGraphPassBuilder::SetHasSideEffects(bool enabled)
{
    m_Graph.MarkSideEffects(m_PassIndex, enabled);
}

} // namespace Dot
