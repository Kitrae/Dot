// =============================================================================
// Dot Engine - Stack Allocator
// =============================================================================
// LIFO allocator with marker-based rollback support.
// Allocates sequentially, frees in reverse order or to checkpoints.
// =============================================================================

#pragma once

#include "Core/Memory/Allocator.h"

namespace Dot
{

/// Stack (LIFO) Allocator
///
/// Like LinearAllocator but supports freeing in LIFO order. Each allocation
/// stores a header with the previous offset, enabling rollback.
///
/// Use cases:
/// - Scoped temporary allocations
/// - Recursive algorithms needing rollback
/// - State save/restore patterns
///
/// Example:
/// @code
///     StackAllocator allocator(1024);
///
///     auto marker = allocator.GetMarker();  // Save checkpoint
///     void* a = allocator.Allocate(64);
///     void* b = allocator.Allocate(64);
///     allocator.FreeToMarker(marker);       // Free both a and b
///
///     // Or free individually (LIFO order):
///     void* c = allocator.Allocate(64);
///     void* d = allocator.Allocate(64);
///     allocator.Free(d);  // Must free d before c
///     allocator.Free(c);
/// @endcode
class DOT_CORE_API StackAllocator final : public IAllocator
{
public:
    /// Marker for checkpoint/rollback - stores both offset and allocation count
    struct Marker
    {
        usize offset;
        usize allocationCount;
    };

    /// Construct allocator with internal buffer.
    /// @param capacity Size of the internal buffer in bytes
    explicit StackAllocator(usize capacity);

    /// Construct allocator with external buffer (no ownership).
    /// @param buffer External memory buffer
    /// @param capacity Size of the external buffer
    StackAllocator(void* buffer, usize capacity);

    ~StackAllocator() override;

    // Move semantics
    StackAllocator(StackAllocator&& other) noexcept;
    StackAllocator& operator=(StackAllocator&& other) noexcept;

    /// Allocate memory from the stack.
    /// Each allocation has a small header overhead.
    /// @param size Size in bytes to allocate
    /// @param alignment Memory alignment (must be power of 2)
    /// @return Pointer to allocated memory, or nullptr if not enough space
    void* Allocate(usize size, usize alignment = 16) override;

    /// Free the most recent allocation (LIFO order).
    /// @param ptr Must be the pointer returned by the most recent Allocate()
    void Free(void* ptr) override;

    /// Reset the allocator, making all memory available again.
    void Reset() override;

    /// Get a marker for the current allocation state.
    /// Use with FreeToMarker() to rollback multiple allocations.
    Marker GetMarker() const { return {m_Offset, m_AllocationCount}; }

    /// Free all allocations made after the marker was obtained.
    /// @param marker A marker obtained from GetMarker()
    void FreeToMarker(Marker marker);

    /// Get total capacity of the allocator.
    usize GetCapacity() const override { return m_Capacity; }

    /// Get amount of memory currently allocated (including headers).
    usize GetUsedMemory() const override { return m_Offset; }

    /// Get pointer to the start of the buffer.
    void* GetBuffer() const { return m_Buffer; }

    /// Get number of active allocations.
    usize GetAllocationCount() const { return m_AllocationCount; }

private:
    /// Header stored before each allocation for LIFO tracking
    struct AllocationHeader
    {
        usize prevOffset; ///< Offset before this allocation
    };

    void* m_Buffer;          ///< Pointer to memory buffer
    usize m_Capacity;        ///< Total size of buffer
    usize m_Offset;          ///< Current allocation position
    usize m_AllocationCount; ///< Number of active allocations
    bool m_OwnsMemory;       ///< True if we allocated the buffer

#if DOT_DEBUG
    void* m_LastAllocation; ///< Track last allocation for LIFO validation
#endif
};

} // namespace Dot
