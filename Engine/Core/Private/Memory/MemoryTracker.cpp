// =============================================================================
// Dot Engine - Memory Tracker Implementation
// =============================================================================

#include "Core/Memory/MemoryTracker.h"

namespace Dot
{

MemoryTracker& MemoryTracker::Get()
{
    static MemoryTracker instance;
    return instance;
}

void MemoryTracker::RecordAlloc(void* ptr, usize size, const char* file, int line, const char* tag)
{
    if (ptr == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    // Prevent infinite recursion if global new is hooked
    // (the unordered_map allocates memory internally)
    if (m_IsInternalAlloc)
    {
        return;
    }
    m_IsInternalAlloc = true;

    // Check for double allocation (shouldn't happen, indicates bug)
    DOT_ASSERT(m_Allocations.find(ptr) == m_Allocations.end() &&
               "Double allocation detected - pointer already tracked");

    // Record the allocation
    AllocationRecord record{};
    record.ptr = ptr;
    record.size = size;
    record.file = file;
    record.line = line;
    record.tag = tag;

    m_Allocations[ptr] = record;

    // Update stats
    m_Stats.totalAllocations++;
    m_Stats.activeAllocations++;
    m_Stats.totalBytesAllocated += size;
    m_Stats.currentBytesInUse += size;

    if (m_Stats.currentBytesInUse > m_Stats.peakBytesInUse)
    {
        m_Stats.peakBytesInUse = m_Stats.currentBytesInUse;
    }

    m_IsInternalAlloc = false;
}

void MemoryTracker::RecordFree(void* ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    // Prevent infinite recursion if global delete is hooked
    if (m_IsInternalAlloc)
    {
        return;
    }
    m_IsInternalAlloc = true;

    auto it = m_Allocations.find(ptr);
    if (it == m_Allocations.end())
    {
        // Freeing untracked memory - could be allocated before tracking started
        // or a double-free bug
        m_IsInternalAlloc = false;
        DOT_ASSERT(false && "Freeing untracked memory - possible double-free or untracked allocation");
        return;
    }

    // Update stats before removing
    m_Stats.totalDeallocations++;
    m_Stats.activeAllocations--;
    m_Stats.currentBytesInUse -= it->second.size;

    m_Allocations.erase(it);
    m_IsInternalAlloc = false;
}

MemoryStats MemoryTracker::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Stats;
}

usize MemoryTracker::ReportLeaks() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_Allocations.empty())
    {
        return 0;
    }

    std::fprintf(stderr, "\n");
    std::fprintf(stderr, "=== MEMORY LEAK REPORT ===\n");
    std::fprintf(stderr, "Detected %zu leaked allocation(s):\n\n", m_Allocations.size());

    usize totalLeakedBytes = 0;

    for (const auto& [ptr, record] : m_Allocations)
    {
        std::fprintf(stderr, "  LEAK: %zu bytes at %p", record.size, record.ptr);

        if (record.file)
        {
            std::fprintf(stderr, "\n        Location: %s:%d", record.file, record.line);
        }

        if (record.tag)
        {
            std::fprintf(stderr, "\n        Tag: %s", record.tag);
        }

        std::fprintf(stderr, "\n\n");
        totalLeakedBytes += record.size;
    }

    std::fprintf(stderr, "Total leaked: %zu bytes in %zu allocation(s)\n", totalLeakedBytes, m_Allocations.size());
    std::fprintf(stderr, "==========================\n\n");

    return m_Allocations.size();
}

bool MemoryTracker::HasLeaks() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return !m_Allocations.empty();
}

void MemoryTracker::Reset()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Allocations.clear();
    m_Stats = MemoryStats{};
}

} // namespace Dot
