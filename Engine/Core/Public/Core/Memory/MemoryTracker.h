// =============================================================================
// Dot Engine - Memory Tracker
// =============================================================================
// Debug utility for tracking allocations, detecting leaks, and gathering stats.
// Only active in Debug builds - compiles to no-ops in Release.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace Dot
{

/// Allocation record stored for each tracked allocation
struct AllocationRecord
{
    void* ptr;
    usize size;
    const char* file;
    int line;
    const char* tag; ///< Optional category tag
};

/// Memory statistics
struct MemoryStats
{
    usize totalAllocations;    ///< Total number of allocations made
    usize totalDeallocations;  ///< Total number of deallocations made
    usize activeAllocations;   ///< Current number of active allocations
    usize totalBytesAllocated; ///< Total bytes ever allocated
    usize currentBytesInUse;   ///< Current bytes in use
    usize peakBytesInUse;      ///< Peak memory usage
};

/// Memory Tracker (Debug Only)
///
/// Singleton that tracks all allocations for leak detection and stats.
/// Thread-safe for multi-threaded allocation tracking.
///
/// Usage:
/// @code
///     // Record allocations (use macros for source location)
///     DOT_TRACK_ALLOC(ptr, size);
///     DOT_TRACK_FREE(ptr);
///
///     // Get stats
///     auto stats = MemoryTracker::Get().GetStats();
///
///     // Check for leaks at shutdown
///     MemoryTracker::Get().ReportLeaks();
/// @endcode
class DOT_CORE_API MemoryTracker
{
public:
    /// Get the singleton instance
    static MemoryTracker& Get();

    /// Record an allocation
    /// @param ptr Pointer to allocated memory
    /// @param size Size in bytes
    /// @param file Source file (use __FILE__)
    /// @param line Source line (use __LINE__)
    /// @param tag Optional category tag
    void RecordAlloc(void* ptr, usize size, const char* file = nullptr, int line = 0, const char* tag = nullptr);

    /// Record a deallocation
    /// @param ptr Pointer being freed
    void RecordFree(void* ptr);

    /// Get current memory statistics
    MemoryStats GetStats() const;

    /// Report any memory leaks to stderr
    /// @return Number of leaks found
    usize ReportLeaks() const;

    /// Check if there are any active allocations (leaks)
    bool HasLeaks() const;

    /// Reset all tracking data
    void Reset();

    // Non-copyable singleton
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

private:
    MemoryTracker() = default;
    ~MemoryTracker() = default;

    mutable std::mutex m_Mutex;
    std::unordered_map<void*, AllocationRecord> m_Allocations;
    MemoryStats m_Stats{};

    // Guard against infinite recursion if global new is hooked.
    // The unordered_map allocates memory, which would trigger RecordAlloc again.
    bool m_IsInternalAlloc = false;
};

// =============================================================================
// Tracking Macros (Debug Only)
// =============================================================================

#if DOT_DEBUG
    #define DOT_TRACK_ALLOC(ptr, size) ::Dot::MemoryTracker::Get().RecordAlloc(ptr, size, __FILE__, __LINE__)

    #define DOT_TRACK_ALLOC_TAGGED(ptr, size, tag)                                                                     \
        ::Dot::MemoryTracker::Get().RecordAlloc(ptr, size, __FILE__, __LINE__, tag)

    #define DOT_TRACK_FREE(ptr) ::Dot::MemoryTracker::Get().RecordFree(ptr)

    #define DOT_MEMORY_REPORT_LEAKS() ::Dot::MemoryTracker::Get().ReportLeaks()
#else
    #define DOT_TRACK_ALLOC(ptr, size) ((void)0)
    #define DOT_TRACK_ALLOC_TAGGED(ptr, size, tag) ((void)0)
    #define DOT_TRACK_FREE(ptr) ((void)0)
    #define DOT_MEMORY_REPORT_LEAKS() (0)
#endif

} // namespace Dot
