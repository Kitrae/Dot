// =============================================================================
// Dot Engine - Frame Graph Implementation
// =============================================================================

#include "RHI/FrameGraph.h"

#include <chrono>
#include <algorithm>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace Dot
{

namespace
{

template <typename T>
inline void HashCombine(uint64& seed, const T& value)
{
    seed ^= static_cast<uint64>(std::hash<T>{}(value)) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

} // namespace

uint64 FrameGraph::ComputeResourceSignature(const FrameGraphResourceDesc& desc) const
{
    uint64 signature = 0xcbf29ce484222325ull;
    HashCombine(signature, static_cast<uint32>(desc.type));
    HashCombine(signature, desc.width);
    HashCombine(signature, desc.height);
    HashCombine(signature, desc.depth);
    HashCombine(signature, desc.mipLevels);
    HashCombine(signature, desc.arrayLayers);
    HashCombine(signature, desc.sampleCount);
    HashCombine(signature, static_cast<uint32>(desc.format));
    HashCombine(signature, static_cast<uint32>(desc.textureType));
    HashCombine(signature, static_cast<uint32>(desc.usage));
    HashCombine(signature, static_cast<uint64>(desc.bufferSize));
    HashCombine(signature, static_cast<uint64>(desc.bufferStride));
    HashCombine(signature, static_cast<uint32>(desc.bufferUsage));
    HashCombine(signature, static_cast<uint32>(desc.bufferMemory));
    HashCombine(signature, desc.imported ? 1u : 0u);
    return signature;
}

RHIResourceState FrameGraph::ResolveResourceState(const FrameGraphResourceDesc& desc, FrameGraphResourceUsage usage,
                                                  FrameGraphResourceAccess access)
{
    (void)access;
    switch (usage)
    {
        case FrameGraphResourceUsage::ColorAttachment:
            return RHIResourceState::RenderTarget;
        case FrameGraphResourceUsage::DepthStencilWrite:
            return RHIResourceState::DepthWrite;
        case FrameGraphResourceUsage::DepthStencilRead:
            return RHIResourceState::DepthRead;
        case FrameGraphResourceUsage::Storage:
            return RHIResourceState::UnorderedAccess;
        case FrameGraphResourceUsage::CopySource:
            return RHIResourceState::CopySource;
        case FrameGraphResourceUsage::CopyDest:
            return RHIResourceState::CopyDest;
        case FrameGraphResourceUsage::Present:
            return RHIResourceState::Present;
        case FrameGraphResourceUsage::ShaderRead:
            return RHIResourceState::ShaderResource;
        case FrameGraphResourceUsage::Unknown:
        default:
            return desc.initialState;
    }
}

void FrameGraph::AppendUniqueHandle(std::vector<FrameGraphResourceHandle>& handles, FrameGraphResourceHandle handle)
{
    if (!handle.IsValid())
        return;

    if (std::find(handles.begin(), handles.end(), handle) == handles.end())
        handles.push_back(handle);
}

void FrameGraph::AppendUniquePass(std::vector<uint32>& passes, uint32 passIndex)
{
    if (std::find(passes.begin(), passes.end(), passIndex) == passes.end())
        passes.push_back(passIndex);
}

void FrameGraph::AddValidationError(const std::string& message)
{
    if (std::find(m_ValidationErrors.begin(), m_ValidationErrors.end(), message) == m_ValidationErrors.end())
        m_ValidationErrors.push_back(message);
}

FrameGraphTextureHandle FrameGraph::ImportTexture(const FrameGraphResourceDesc& desc, RHITexturePtr texture)
{
    return ImportResource(desc, std::move(texture), nullptr);
}

FrameGraphBufferHandle FrameGraph::ImportBuffer(const FrameGraphResourceDesc& desc, RHIBufferPtr buffer)
{
    return ImportResource(desc, nullptr, std::move(buffer));
}

FrameGraphResourceHandle FrameGraph::CreateResource(const FrameGraphResourceDesc& desc, uint32 passIndex)
{
    FrameGraphResource resource;
    resource.desc = desc;
    resource.handle.index = static_cast<uint32>(m_Resources.size());
    resource.logicalId = m_NextLogicalId++;
    resource.version = 0;
    resource.imported = false;
    resource.producerPass = passIndex;
    resource.firstPass = passIndex;
    resource.lastPass = passIndex;
    resource.initialState = desc.initialState;
    resource.finalState = desc.initialState;
    m_Resources.push_back(std::move(resource));
    m_Compiled = false;
    return m_Resources.back().handle;
}

FrameGraphResourceHandle FrameGraph::ImportResource(const FrameGraphResourceDesc& sourceDesc, RHITexturePtr texture,
                                                    RHIBufferPtr buffer)
{
    FrameGraphResourceDesc desc = sourceDesc;
    desc.imported = true;

    FrameGraphResource resource;
    resource.desc = desc;
    resource.handle.index = static_cast<uint32>(m_Resources.size());
    resource.logicalId = m_NextLogicalId++;
    resource.version = 0;
    resource.imported = true;
    resource.firstPass = UINT32_MAX;
    resource.lastPass = 0;
    resource.initialState = desc.initialState;
    resource.finalState = desc.initialState;
    resource.texture = std::move(texture);
    resource.buffer = std::move(buffer);
    m_Resources.push_back(std::move(resource));
    m_Compiled = false;
    return m_Resources.back().handle;
}

FrameGraphResourceHandle FrameGraph::MarkRead(FrameGraphResourceHandle handle, uint32 passIndex,
                                              FrameGraphResourceUsage usage)
{
    if (!handle.IsValid() || handle.index >= m_Resources.size())
        return {};

    FrameGraphResource& resource = m_Resources[handle.index];
    resource.firstPass = std::min(resource.firstPass, passIndex);
    resource.lastPass = std::max(resource.lastPass, passIndex);
    AppendUniquePass(resource.consumerPasses, passIndex);

    FrameGraphPass& pass = m_Passes[passIndex];
    AppendUniqueHandle(pass.reads, handle);
    pass.resourceUses.push_back(
        {handle, FrameGraphResourceAccess::Read, usage, ResolveResourceState(resource.desc, usage, FrameGraphResourceAccess::Read)});
    m_Compiled = false;
    return handle;
}

FrameGraphResourceHandle FrameGraph::MarkWrite(FrameGraphResourceHandle handle, uint32 passIndex,
                                               FrameGraphResourceUsage usage, FrameGraphResourceAccess access)
{
    if (!handle.IsValid() || handle.index >= m_Resources.size())
        return {};

    FrameGraphResource& source = m_Resources[handle.index];

    // Writing a prior version implicitly depends on it.
    MarkRead(handle, passIndex, usage == FrameGraphResourceUsage::Unknown ? FrameGraphResourceUsage::ShaderRead : usage);

    FrameGraphResource resource;
    resource.desc = source.desc;
    resource.handle.index = static_cast<uint32>(m_Resources.size());
    resource.logicalId = source.logicalId;
    resource.version = source.version + 1;
    resource.parent = handle;
    resource.imported = source.imported;
    resource.producerPass = passIndex;
    resource.firstPass = passIndex;
    resource.lastPass = passIndex;
    resource.initialState = ResolveResourceState(resource.desc, usage, access);
    resource.finalState = resource.initialState;
    resource.texture = source.texture;
    resource.buffer = source.buffer;
    m_Resources.push_back(std::move(resource));

    FrameGraphPass& pass = m_Passes[passIndex];
    AppendUniqueHandle(pass.writes, m_Resources.back().handle);
    pass.resourceUses.push_back(
        {m_Resources.back().handle, access, usage, ResolveResourceState(source.desc, usage, access)});
    m_Compiled = false;
    return m_Resources.back().handle;
}

void FrameGraph::MarkSideEffects(uint32 passIndex, bool enabled)
{
    if (passIndex >= m_Passes.size())
        return;
    m_Passes[passIndex].hasSideEffects = enabled;
    m_Compiled = false;
}

void FrameGraph::SetOutput(FrameGraphResourceHandle handle)
{
    AppendUniqueHandle(m_Outputs, handle);
    m_Compiled = false;
}

void FrameGraph::ClearOutputs()
{
    m_Outputs.clear();
    m_Compiled = false;
}

void FrameGraph::BuildDependencies()
{
    for (FrameGraphPass& pass : m_Passes)
    {
        pass.dependencies.clear();
        pass.barriers.clear();
        pass.executionIndex = UINT32_MAX;
        pass.refCount = 0;
        pass.cpuTimeMs = 0.0;
        pass.gpuTimeMs = 0.0;
    }

    for (FrameGraphPass& pass : m_Passes)
    {
        for (const FrameGraphResourceUse& use : pass.resourceUses)
        {
            if (!use.handle.IsValid() || use.handle.index >= m_Resources.size())
                continue;

            const FrameGraphResource& resource = m_Resources[use.handle.index];
            if (resource.producerPass != UINT32_MAX && resource.producerPass != pass.index)
                AppendUniquePass(pass.dependencies, resource.producerPass);

            if (use.access != FrameGraphResourceAccess::Read && resource.parent.IsValid() &&
                resource.parent.index < m_Resources.size())
            {
                const FrameGraphResource& parent = m_Resources[resource.parent.index];
                if (parent.producerPass != UINT32_MAX && parent.producerPass != pass.index)
                    AppendUniquePass(pass.dependencies, parent.producerPass);
            }
        }
    }
}

void FrameGraph::ValidateGraph()
{
    m_ValidationErrors.clear();

    for (FrameGraphResourceHandle output : m_Outputs)
    {
        if (!output.IsValid() || output.index >= m_Resources.size())
            AddValidationError("FrameGraph output references an invalid resource handle.");
    }

    for (const FrameGraphPass& pass : m_Passes)
    {
        std::unordered_map<uint32, RHIResourceState> requiredStates;

        for (const FrameGraphResourceUse& use : pass.resourceUses)
        {
            if (!use.handle.IsValid() || use.handle.index >= m_Resources.size())
            {
                std::ostringstream oss;
                oss << "Pass '" << pass.name << "' references an invalid resource handle.";
                AddValidationError(oss.str());
                continue;
            }

            const FrameGraphResource& resource = m_Resources[use.handle.index];
            if (use.usage == FrameGraphResourceUsage::ColorAttachment)
            {
                if (resource.desc.type != FrameGraphResourceType::Texture ||
                    (static_cast<uint8>(resource.desc.usage) & static_cast<uint8>(RHITextureUsage::RenderTarget)) == 0)
                {
                    std::ostringstream oss;
                    oss << "Pass '" << pass.name << "' uses '" << resource.desc.name
                        << "' as a color attachment without render-target usage.";
                    AddValidationError(oss.str());
                }
            }
            else if (use.usage == FrameGraphResourceUsage::DepthStencilWrite ||
                     use.usage == FrameGraphResourceUsage::DepthStencilRead)
            {
                if (resource.desc.type != FrameGraphResourceType::Texture ||
                    (static_cast<uint8>(resource.desc.usage) & static_cast<uint8>(RHITextureUsage::DepthStencil)) == 0)
                {
                    std::ostringstream oss;
                    oss << "Pass '" << pass.name << "' uses '" << resource.desc.name
                        << "' as depth without depth-stencil usage.";
                    AddValidationError(oss.str());
                }
            }
            else if (use.usage == FrameGraphResourceUsage::Storage)
            {
                if (resource.desc.type == FrameGraphResourceType::Texture &&
                    (static_cast<uint8>(resource.desc.usage) & static_cast<uint8>(RHITextureUsage::Storage)) == 0)
                {
                    std::ostringstream oss;
                    oss << "Pass '" << pass.name << "' uses '" << resource.desc.name
                        << "' as storage without storage usage.";
                    AddValidationError(oss.str());
                }
            }
            else if (use.usage == FrameGraphResourceUsage::Present)
            {
                if (resource.desc.type != FrameGraphResourceType::Texture)
                {
                    std::ostringstream oss;
                    oss << "Pass '" << pass.name << "' uses a buffer for present.";
                    AddValidationError(oss.str());
                }
            }

            auto stateIt = requiredStates.find(use.handle.index);
            if (stateIt == requiredStates.end())
            {
                requiredStates.emplace(use.handle.index, use.state);
            }
            else if (stateIt->second != use.state && stateIt->second != RHIResourceState::Unknown &&
                     use.state != RHIResourceState::Unknown)
            {
                std::ostringstream oss;
                oss << "Pass '" << pass.name << "' requests conflicting states for resource '" << resource.desc.name
                    << "'.";
                AddValidationError(oss.str());
            }
        }
    }
}

void FrameGraph::CullUnusedPasses()
{
    if (m_Outputs.empty())
    {
        for (FrameGraphPass& pass : m_Passes)
            pass.culled = false;
        return;
    }

    std::vector<bool> live(m_Passes.size(), false);
    std::vector<uint32> stack;

    for (FrameGraphPass& pass : m_Passes)
    {
        if (pass.hasSideEffects)
            stack.push_back(pass.index);
    }

    for (FrameGraphResourceHandle output : m_Outputs)
    {
        if (!output.IsValid() || output.index >= m_Resources.size())
            continue;
        const FrameGraphResource& resource = m_Resources[output.index];
        if (resource.producerPass != UINT32_MAX)
            stack.push_back(resource.producerPass);
    }

    while (!stack.empty())
    {
        const uint32 passIndex = stack.back();
        stack.pop_back();
        if (passIndex >= m_Passes.size() || live[passIndex])
            continue;

        live[passIndex] = true;
        for (uint32 dep : m_Passes[passIndex].dependencies)
            stack.push_back(dep);
    }

    for (FrameGraphPass& pass : m_Passes)
        pass.culled = !live[pass.index];
}

void FrameGraph::BuildExecutionOrder()
{
    m_ExecutionOrder.clear();

    std::vector<uint32> indegree(m_Passes.size(), 0);
    for (const FrameGraphPass& pass : m_Passes)
    {
        if (pass.culled)
            continue;
        for (uint32 dep : pass.dependencies)
        {
            if (dep < m_Passes.size() && !m_Passes[dep].culled)
                indegree[pass.index]++;
        }
    }

    std::queue<uint32> ready;
    for (const FrameGraphPass& pass : m_Passes)
    {
        if (!pass.culled && indegree[pass.index] == 0)
            ready.push(pass.index);
    }

    while (!ready.empty())
    {
        const uint32 passIndex = ready.front();
        ready.pop();
        m_ExecutionOrder.push_back(passIndex);

        for (FrameGraphPass& other : m_Passes)
        {
            if (other.culled)
                continue;
            if (std::find(other.dependencies.begin(), other.dependencies.end(), passIndex) == other.dependencies.end())
                continue;
            if (indegree[other.index] > 0)
            {
                indegree[other.index]--;
                if (indegree[other.index] == 0)
                    ready.push(other.index);
            }
        }
    }

    // Fail open to declaration order if a cycle or malformed dependency slipped in.
    if (m_ExecutionOrder.size() != GetActivePassCount())
    {
        m_ExecutionOrder.clear();
        for (const FrameGraphPass& pass : m_Passes)
        {
            if (!pass.culled)
                m_ExecutionOrder.push_back(pass.index);
        }
    }

    for (uint32 i = 0; i < m_ExecutionOrder.size(); ++i)
        m_Passes[m_ExecutionOrder[i]].executionIndex = i;
}

void FrameGraph::ComputeResourceLifetimes()
{
    for (FrameGraphResource& resource : m_Resources)
    {
        resource.firstPass = (resource.producerPass != UINT32_MAX) ? resource.producerPass : UINT32_MAX;
        resource.lastPass = (resource.producerPass != UINT32_MAX) ? resource.producerPass : 0;

        for (uint32 consumer : resource.consumerPasses)
        {
            if (consumer >= m_Passes.size() || m_Passes[consumer].culled)
                continue;
            resource.firstPass = std::min(resource.firstPass, consumer);
            resource.lastPass = std::max(resource.lastPass, consumer);
        }

        if (resource.firstPass == UINT32_MAX)
            resource.firstPass = 0;

        if (resource.imported && resource.producerPass == UINT32_MAX)
            resource.lastPass = std::max(resource.lastPass, resource.firstPass);
    }
}

void FrameGraph::AssignPhysicalResources()
{
    m_PhysicalResources.clear();

    std::vector<uint32> liveResources;
    liveResources.reserve(m_Resources.size());
    for (const FrameGraphResource& resource : m_Resources)
    {
        const bool producedByLivePass =
            resource.producerPass != UINT32_MAX && resource.producerPass < m_Passes.size() && !m_Passes[resource.producerPass].culled;
        const bool consumedByLivePass = std::any_of(resource.consumerPasses.begin(), resource.consumerPasses.end(),
                                                    [this](uint32 passIndex)
                                                    {
                                                        return passIndex < m_Passes.size() && !m_Passes[passIndex].culled;
                                                    });
        const bool isOutput =
            std::find(m_Outputs.begin(), m_Outputs.end(), resource.handle) != m_Outputs.end();

        if (resource.imported || producedByLivePass || consumedByLivePass || isOutput)
            liveResources.push_back(resource.handle.index);
    }

    std::sort(liveResources.begin(), liveResources.end(),
              [this](uint32 a, uint32 b)
              {
                  if (m_Resources[a].firstPass != m_Resources[b].firstPass)
                      return m_Resources[a].firstPass < m_Resources[b].firstPass;
                  return a < b;
              });

    for (uint32 resourceIndex : liveResources)
    {
        FrameGraphResource& resource = m_Resources[resourceIndex];
        const uint64 signature = ComputeResourceSignature(resource.desc);

        bool assigned = false;
        if (resource.imported && resource.parent.IsValid())
        {
            const FrameGraphResource& parent = m_Resources[resource.parent.index];
            if (parent.physicalIndex >= 0)
            {
                resource.physicalIndex = parent.physicalIndex;
                FrameGraphPhysicalResource& physical = m_PhysicalResources[parent.physicalIndex];
                physical.lastPass = std::max(physical.lastPass, resource.lastPass);
                assigned = true;
            }
        }
        if (!resource.imported)
        {
            for (FrameGraphPhysicalResource& physical : m_PhysicalResources)
            {
                if (physical.imported)
                    continue;
                if (physical.signature != signature)
                    continue;
                if (physical.lastPass >= resource.firstPass)
                    continue;

                resource.physicalIndex = static_cast<int>(physical.index);
                physical.lastPass = resource.lastPass;
                assigned = true;
                break;
            }
        }

        if (!assigned)
        {
            FrameGraphPhysicalResource physical;
            physical.index = static_cast<uint32>(m_PhysicalResources.size());
            physical.type = resource.desc.type;
            physical.desc = resource.desc;
            physical.imported = resource.imported;
            physical.signature = signature;
            physical.firstPass = resource.firstPass;
            physical.lastPass = resource.lastPass;
            physical.texture = resource.texture;
            physical.buffer = resource.buffer;
            m_PhysicalResources.push_back(std::move(physical));
            resource.physicalIndex = static_cast<int>(m_PhysicalResources.back().index);
        }
    }

    for (FrameGraphResource& resource : m_Resources)
    {
        if (resource.physicalIndex < 0 || static_cast<size_t>(resource.physicalIndex) >= m_PhysicalResources.size())
            continue;
        FrameGraphPhysicalResource& physical = m_PhysicalResources[resource.physicalIndex];
        if (resource.desc.type == FrameGraphResourceType::Texture)
            resource.texture = physical.texture;
        else
            resource.buffer = physical.buffer;
    }
}

void FrameGraph::BuildBarriers()
{
    for (FrameGraphPass& pass : m_Passes)
        pass.barriers.clear();

    for (FrameGraphResource& resource : m_Resources)
    {
        if (resource.imported)
            resource.finalState = resource.desc.initialState;
        else
            resource.finalState = resource.initialState;
    }

    for (uint32 passIndex : m_ExecutionOrder)
    {
        FrameGraphPass& pass = m_Passes[passIndex];
        for (const FrameGraphResourceUse& use : pass.resourceUses)
        {
            if (!use.handle.IsValid() || use.handle.index >= m_Resources.size())
                continue;

            FrameGraphResource& resource = m_Resources[use.handle.index];
            const RHIResourceState desired = use.state;
            const RHIResourceState before = resource.finalState;
            if (desired != RHIResourceState::Unknown && before != RHIResourceState::Unknown && before != desired)
            {
                pass.barriers.push_back({use.handle, before, desired});
            }
            resource.finalState = desired;
        }
    }
}

void FrameGraph::AllocatePhysicalResources(RHIDevice& device)
{
    for (FrameGraphPhysicalResource& physical : m_PhysicalResources)
    {
        if (physical.imported)
            continue;

        if (physical.type == FrameGraphResourceType::Texture)
        {
            if (physical.texture)
                continue;

            auto& pool = m_TexturePool[physical.signature];
            if (!pool.empty())
            {
                physical.texture = pool.back();
                pool.pop_back();
            }
            else
            {
                RHITextureDesc desc = {};
                desc.width = physical.desc.width;
                desc.height = physical.desc.height;
                desc.depth = physical.desc.depth;
                desc.mipLevels = physical.desc.mipLevels;
                desc.arrayLayers = physical.desc.arrayLayers;
                desc.format = physical.desc.format;
                desc.type = physical.desc.textureType;
                desc.usage = physical.desc.usage;
                desc.debugName = physical.desc.name.c_str();
                physical.texture = device.CreateTexture(desc);
            }
        }
        else
        {
            if (physical.buffer)
                continue;

            auto& pool = m_BufferPool[physical.signature];
            if (!pool.empty())
            {
                physical.buffer = pool.back();
                pool.pop_back();
            }
            else
            {
                RHIBufferDesc desc = {};
                desc.size = physical.desc.bufferSize;
                desc.usage = physical.desc.bufferUsage;
                desc.memory = physical.desc.bufferMemory;
                desc.debugName = physical.desc.name.c_str();
                physical.buffer = device.CreateBuffer(desc);
            }
        }
    }

    for (FrameGraphResource& resource : m_Resources)
    {
        if (resource.physicalIndex < 0 || static_cast<size_t>(resource.physicalIndex) >= m_PhysicalResources.size())
            continue;
        FrameGraphPhysicalResource& physical = m_PhysicalResources[resource.physicalIndex];
        resource.texture = physical.texture;
        resource.buffer = physical.buffer;
    }
}

void FrameGraph::Compile()
{
    const auto compileStart = std::chrono::high_resolution_clock::now();
    BuildDependencies();
    ValidateGraph();
    CullUnusedPasses();
    BuildExecutionOrder();
    ComputeResourceLifetimes();
    AssignPhysicalResources();
    BuildBarriers();
    m_Compiled = true;
    const auto compileEnd = std::chrono::high_resolution_clock::now();
    m_LastCompileTimeMs =
        std::chrono::duration<double, std::milli>(compileEnd - compileStart).count();
}

void FrameGraph::Execute(RHIDevice& device)
{
    if (!m_Compiled)
        Compile();

    const auto executeStart = std::chrono::high_resolution_clock::now();
    AllocatePhysicalResources(device);

    for (uint32 passIndex : m_ExecutionOrder)
    {
        FrameGraphPass& pass = m_Passes[passIndex];
        if (pass.culled || !pass.execute)
            continue;

        const uint32 timingHandle = device.BeginTimestamp(pass.name.c_str());
        const auto passStart = std::chrono::high_resolution_clock::now();
        for (const FrameGraphBarrier& barrier : pass.barriers)
        {
            if (!barrier.handle.IsValid() || barrier.handle.index >= m_Resources.size())
                continue;

            const FrameGraphResource& resource = m_Resources[barrier.handle.index];
            if (resource.desc.type == FrameGraphResourceType::Texture && resource.texture)
                device.TransitionTexture(resource.texture, barrier.before, barrier.after);
            else if (resource.desc.type == FrameGraphResourceType::Buffer && resource.buffer)
                device.TransitionBuffer(resource.buffer, barrier.before, barrier.after);
        }

        pass.execute(device, pass);
        device.EndTimestamp(timingHandle);
        const auto passEnd = std::chrono::high_resolution_clock::now();
        pass.cpuTimeMs = std::chrono::duration<double, std::milli>(passEnd - passStart).count();
        pass.gpuTimeMs = device.GetLastTimestampMs(pass.name.c_str());
    }

    const auto executeEnd = std::chrono::high_resolution_clock::now();
    m_LastExecuteTimeMs =
        std::chrono::duration<double, std::milli>(executeEnd - executeStart).count();
}

void FrameGraph::Reset()
{
    for (FrameGraphPhysicalResource& physical : m_PhysicalResources)
    {
        if (physical.imported)
            continue;
        if (physical.type == FrameGraphResourceType::Texture && physical.texture)
            m_TexturePool[physical.signature].push_back(physical.texture);
        if (physical.type == FrameGraphResourceType::Buffer && physical.buffer)
            m_BufferPool[physical.signature].push_back(physical.buffer);
    }

    m_Passes.clear();
    m_Resources.clear();
    m_PhysicalResources.clear();
    m_ExecutionOrder.clear();
    m_Outputs.clear();
    m_ValidationErrors.clear();
    m_NextLogicalId = 0;
    m_Compiled = false;
    m_LastCompileTimeMs = 0.0;
    m_LastExecuteTimeMs = 0.0;
}

FrameGraphResource* FrameGraph::GetResource(FrameGraphResourceHandle handle)
{
    if (!handle.IsValid() || handle.index >= m_Resources.size())
        return nullptr;
    return &m_Resources[handle.index];
}

const FrameGraphResource* FrameGraph::GetResource(FrameGraphResourceHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_Resources.size())
        return nullptr;
    return &m_Resources[handle.index];
}

size_t FrameGraph::GetActivePassCount() const
{
    size_t count = 0;
    for (const FrameGraphPass& pass : m_Passes)
    {
        if (!pass.culled)
            count++;
    }
    return count;
}

} // namespace Dot
