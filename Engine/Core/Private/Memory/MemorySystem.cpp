// =============================================================================
// Dot Engine - Memory System Implementation
// =============================================================================

#include "Core/Memory/MemorySystem.h"

#include "Core/Core.h"
#include "Core/Log.h"

namespace Dot
{

MemorySystem& MemorySystem::Get()
{
    static MemorySystem instance;
    return instance;
}

void MemorySystem::Initialize(usize frameAllocatorSize, usize scratchAllocatorSize)
{
    if (m_Initialized)
    {
        DOT_LOG_WARN("MemorySystem::Initialize called when already initialized");
        return;
    }

    DOT_LOG_INFO("Initializing MemorySystem (Frame: {} MB, Scratch: {} MB)", frameAllocatorSize / (1024 * 1024),
                 scratchAllocatorSize / (1024 * 1024));

    m_FrameAllocator = std::make_unique<LinearAllocator>(frameAllocatorSize);
    m_ScratchAllocator = std::make_unique<StackAllocator>(scratchAllocatorSize);

    m_FrameNumber = 0;
    m_Initialized = true;
    m_InFrame = false;

    DOT_LOG_INFO("MemorySystem initialized successfully");
}

void MemorySystem::Shutdown()
{
    if (!m_Initialized)
    {
        return;
    }

    DOT_LOG_INFO("Shutting down MemorySystem");

    // Log final statistics
    if (m_FrameAllocator)
    {
        DOT_LOG_INFO("  Frame allocator peak usage: {} bytes", m_FrameAllocator->GetUsedMemory());
    }
    if (m_ScratchAllocator)
    {
        DOT_LOG_INFO("  Scratch allocator peak usage: {} bytes", m_ScratchAllocator->GetUsedMemory());
    }

    m_FrameAllocator.reset();
    m_ScratchAllocator.reset();
    m_Initialized = false;
}

void MemorySystem::BeginFrame()
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");
    DOT_ASSERT(!m_InFrame && "BeginFrame called without EndFrame");

    // Reset frame allocator - all previous frame allocations are now invalid
    if (m_FrameAllocator)
    {
        m_FrameAllocator->Reset();
    }

    m_InFrame = true;
}

void MemorySystem::EndFrame()
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");
    DOT_ASSERT(m_InFrame && "EndFrame called without BeginFrame");

    m_InFrame = false;
    m_FrameNumber++;
}

void* MemorySystem::FrameAlloc(usize size, usize alignment)
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");
    DOT_ASSERT(m_InFrame && "FrameAlloc called outside of BeginFrame/EndFrame");

    if (!m_FrameAllocator)
    {
        return nullptr;
    }

    // Thread-safe allocation (if needed for job system)
    std::lock_guard<std::mutex> lock(m_FrameAllocMutex);
    return m_FrameAllocator->Allocate(size, alignment);
}

usize MemorySystem::GetFrameAllocatorUsed() const
{
    if (!m_FrameAllocator)
        return 0;
    return m_FrameAllocator->GetUsedMemory();
}

usize MemorySystem::GetFrameAllocatorCapacity() const
{
    if (!m_FrameAllocator)
        return 0;
    return m_FrameAllocator->GetCapacity();
}

StackAllocator::Marker MemorySystem::GetScratchMarker()
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");
    DOT_ASSERT(m_ScratchAllocator && "Scratch allocator not available");

    return m_ScratchAllocator->GetMarker();
}

void* MemorySystem::ScratchAlloc(usize size, usize alignment)
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");

    if (!m_ScratchAllocator)
    {
        return nullptr;
    }

    return m_ScratchAllocator->Allocate(size, alignment);
}

void MemorySystem::ScratchFreeToMarker(StackAllocator::Marker marker)
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");

    if (m_ScratchAllocator)
    {
        m_ScratchAllocator->FreeToMarker(marker);
    }
}

void MemorySystem::ScratchReset()
{
    DOT_ASSERT(m_Initialized && "MemorySystem not initialized");

    if (m_ScratchAllocator)
    {
        m_ScratchAllocator->Reset();
    }
}

} // namespace Dot
