// =============================================================================
// Dot Engine - Linear Allocator
// =============================================================================
// Fast, sequential allocator for temporary per-frame data.
// Allocates from a contiguous buffer, resets everything at once.
// =============================================================================

#pragma once

#include "Core/Memory/Allocator.h"

namespace Dot
{

/// Linear (Arena/Bump) Allocator
///
/// The simplest and fastest allocator. Memory is allocated sequentially
/// from a contiguous buffer. Individual frees are not supported - the
/// entire buffer is reset at once.
///
/// Use cases:
/// - Per-frame scratch memory
/// - Temporary string building
/// - Short-lived allocations that share a lifetime
///
/// Example:
/// @code
///     LinearAllocator allocator(1024 * 1024); // 1MB buffer
///
///     // Allocate during frame
///     void* data = allocator.Allocate(256);
///     float* floats = static_cast<float*>(allocator.Allocate(sizeof(float) * 100));
///
///     // At end of frame, reset everything
///     allocator.Reset();
/// @endcode
class DOT_CORE_API LinearAllocator final : public IAllocator
{
public:
    /// Construct allocator with internal buffer.
    /// @param capacity Size of the internal buffer in bytes
    explicit LinearAllocator(usize capacity);

    /// Construct allocator with external buffer (no ownership).
    /// @param buffer External memory buffer
    /// @param capacity Size of the external buffer
    LinearAllocator(void* buffer, usize capacity);

    ~LinearAllocator() override;

    // Move semantics
    LinearAllocator(LinearAllocator&& other) noexcept;
    LinearAllocator& operator=(LinearAllocator&& other) noexcept;

    /// Allocate memory from the buffer.
    /// @param size Size in bytes to allocate
    /// @param alignment Memory alignment (must be power of 2)
    /// @return Pointer to allocated memory, or nullptr if not enough space
    void* Allocate(usize size, usize alignment = 16) override;

    /// Free is a no-op for linear allocator.
    /// Memory is only freed via Reset().
    void Free(void* ptr) override;

    /// Reset the allocator, making all memory available again.
    /// Does NOT call destructors - caller is responsible for cleanup.
    void Reset() override;

    /// Get total capacity of the allocator.
    usize GetCapacity() const override { return m_Capacity; }

    /// Get amount of memory currently allocated.
    usize GetUsedMemory() const override { return m_Offset; }

    /// Get current allocation offset (for debugging/markers).
    usize GetOffset() const { return m_Offset; }

    /// Get pointer to the start of the buffer.
    void* GetBuffer() const { return m_Buffer; }

    /// Get number of allocations made since last reset.
    usize GetAllocationCount() const { return m_AllocationCount; }

private:
    void* m_Buffer;          ///< Pointer to memory buffer
    usize m_Capacity;        ///< Total size of buffer
    usize m_Offset;          ///< Current allocation position
    usize m_AllocationCount; ///< Number of allocations (for debugging)
    bool m_OwnsMemory;       ///< True if we allocated the buffer
};

} // namespace Dot
