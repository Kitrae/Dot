// =============================================================================
// Dot Engine - Memory System
// =============================================================================
// Central management for engine memory allocators.
// Provides per-frame and scratch allocators for temporary allocations.
// =============================================================================

#pragma once

// Include allocators directly - they are self-contained and provide DOT_CORE_API, usize, etc.
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/StackAllocator.h"

#include <memory>
#include <mutex>

namespace Dot
{

// uint64 for frame number (Allocator.h doesn't provide this)
using uint64 = std::uint64_t;
/// Memory System - centralized memory allocator management
///
/// Usage:
/// @code
///     // Initialize at engine startup
///     MemorySystem::Get().Initialize();
///
///     // Each frame:
///     MemorySystem::Get().BeginFrame();
///
///     // Allocate temporary per-frame data
///     void* data = MemorySystem::Get().FrameAlloc(1024);
///
///     MemorySystem::Get().EndFrame();
///
///     // Shutdown at engine exit
///     MemorySystem::Get().Shutdown();
/// @endcode
class DOT_CORE_API MemorySystem
{
public:
    /// Get singleton instance
    static MemorySystem& Get();

    /// Initialize the memory system with default allocator sizes
    /// @param frameAllocatorSize Size of per-frame linear allocator (default 16MB)
    /// @param scratchAllocatorSize Size of scratch stack allocator (default 8MB)
    void Initialize(usize frameAllocatorSize = 16 * 1024 * 1024, usize scratchAllocatorSize = 8 * 1024 * 1024);

    /// Shutdown and release all allocators
    void Shutdown();

    /// Check if system is initialized
    bool IsInitialized() const { return m_Initialized; }

    // -------------------------------------------------------------------------
    // Frame Lifecycle
    // -------------------------------------------------------------------------

    /// Call at the beginning of each frame
    /// Resets the frame allocator, invalidating all previous frame allocations
    void BeginFrame();

    /// Call at the end of each frame
    void EndFrame();

    /// Get current frame number
    uint64 GetFrameNumber() const { return m_FrameNumber; }

    // -------------------------------------------------------------------------
    // Frame Allocator (LinearAllocator)
    // -------------------------------------------------------------------------

    /// Allocate memory that lives until EndFrame()
    /// Fast bump-pointer allocation, no individual frees
    /// @param size Bytes to allocate
    /// @param alignment Alignment requirement (default 16)
    /// @return Pointer to allocated memory, or nullptr if out of space
    void* FrameAlloc(usize size, usize alignment = 16);

    /// Typed frame allocation with construction
    template <typename T, typename... Args> T* FrameNew(Args&&... args)
    {
        void* ptr = FrameAlloc(sizeof(T), alignof(T));
        if (ptr)
        {
            return new (ptr) T(std::forward<Args>(args)...);
        }
        return nullptr;
    }

    /// Allocate array on frame allocator
    template <typename T> T* FrameAllocArray(usize count)
    {
        return static_cast<T*>(FrameAlloc(sizeof(T) * count, alignof(T)));
    }

    /// Get frame allocator usage statistics
    usize GetFrameAllocatorUsed() const;
    usize GetFrameAllocatorCapacity() const;

    // -------------------------------------------------------------------------
    // Scratch Allocator (StackAllocator)
    // -------------------------------------------------------------------------

    /// Get a marker for the current scratch allocator state
    /// Use with ScratchFreeToMarker() for scoped allocations
    StackAllocator::Marker GetScratchMarker();

    /// Allocate scratch memory (must free in LIFO order or use markers)
    void* ScratchAlloc(usize size, usize alignment = 16);

    /// Free scratch memory back to a marker
    void ScratchFreeToMarker(StackAllocator::Marker marker);

    /// Reset entire scratch allocator
    void ScratchReset();

    // -------------------------------------------------------------------------
    // Direct Allocator Access (advanced usage)
    // -------------------------------------------------------------------------

    /// Get direct access to frame allocator (use with caution)
    LinearAllocator* GetFrameAllocator() { return m_FrameAllocator.get(); }

    /// Get direct access to scratch allocator (use with caution)
    StackAllocator* GetScratchAllocator() { return m_ScratchAllocator.get(); }

    // Non-copyable singleton
    MemorySystem(const MemorySystem&) = delete;
    MemorySystem& operator=(const MemorySystem&) = delete;

private:
    MemorySystem() = default;
    ~MemorySystem() = default;

    std::unique_ptr<LinearAllocator> m_FrameAllocator;
    std::unique_ptr<StackAllocator> m_ScratchAllocator;

    uint64 m_FrameNumber = 0;
    bool m_Initialized = false;
    bool m_InFrame = false;

    // Thread safety for frame allocator (optional, if needed for job system)
    mutable std::mutex m_FrameAllocMutex;
};

// =============================================================================
// Convenience Macros
// =============================================================================

/// Allocate temporary per-frame memory
#define DOT_FRAME_ALLOC(size) Dot::MemorySystem::Get().FrameAlloc(size)

/// Allocate and construct object on frame allocator
#define DOT_FRAME_NEW(Type, ...) Dot::MemorySystem::Get().FrameNew<Type>(__VA_ARGS__)

/// Allocate array on frame allocator
#define DOT_FRAME_ALLOC_ARRAY(Type, count) Dot::MemorySystem::Get().FrameAllocArray<Type>(count)

} // namespace Dot
