// =============================================================================
// Dot Engine - Pool Allocator
// =============================================================================
// Fixed-size block allocator with O(1) allocation and deallocation.
// Uses an embedded free list for zero-overhead tracking.
// =============================================================================

#pragma once

#include "Core/Memory/Allocator.h"

namespace Dot
{

/// Pool Allocator
///
/// Allocates fixed-size blocks from a contiguous buffer. Both allocation
/// and deallocation are O(1) operations using an embedded free list.
///
/// Use cases:
/// - Game objects with uniform size
/// - Particle systems
/// - ECS components
/// - Any frequently allocated/deallocated objects of same size
///
/// Example:
/// @code
///     // Pool of 100 objects, each 64 bytes
///     PoolAllocator pool(64, 100);
///
///     void* obj1 = pool.Allocate();
///     void* obj2 = pool.Allocate();
///
///     pool.Free(obj1);  // O(1) - returns to free list
///
///     void* obj3 = pool.Allocate();  // Reuses obj1's slot
/// @endcode
class DOT_CORE_API PoolAllocator final : public IAllocator
{
public:
    /// Construct pool allocator with internal buffer.
    /// @param blockSize Size of each block in bytes (minimum 8 for free list pointer)
    /// @param blockCount Number of blocks in the pool
    PoolAllocator(usize blockSize, usize blockCount);

    /// Construct pool allocator with external buffer.
    /// @param buffer External memory buffer
    /// @param blockSize Size of each block in bytes
    /// @param blockCount Number of blocks
    PoolAllocator(void* buffer, usize blockSize, usize blockCount);

    ~PoolAllocator() override;

    // Move semantics
    PoolAllocator(PoolAllocator&& other) noexcept;
    PoolAllocator& operator=(PoolAllocator&& other) noexcept;

    /// Allocate a single block from the pool.
    /// @param size Ignored (always allocates blockSize bytes)
    /// @param alignment Ignored (blocks are pre-aligned)
    /// @return Pointer to block, or nullptr if pool is exhausted
    void* Allocate(usize size = 0, usize alignment = 16) override;

    /// Return a block to the pool. O(1) operation.
    /// @param ptr Pointer to block previously returned by Allocate()
    void Free(void* ptr) override;

    /// Reset the pool, making all blocks available again.
    /// Does NOT call destructors - caller is responsible.
    void Reset() override;

    /// Get total capacity in bytes.
    usize GetCapacity() const override { return m_BlockSize * m_BlockCount; }

    /// Get currently used memory in bytes.
    usize GetUsedMemory() const override { return m_BlockSize * m_AllocatedCount; }

    /// Get size of each block.
    usize GetBlockSize() const { return m_BlockSize; }

    /// Get total number of blocks in pool.
    usize GetBlockCount() const { return m_BlockCount; }

    /// Get number of currently allocated blocks.
    usize GetAllocationCount() const { return m_AllocatedCount; }

    /// Get number of free blocks available.
    usize GetFreeBlockCount() const { return m_BlockCount - m_AllocatedCount; }

    /// Get pointer to the start of the buffer.
    void* GetBuffer() const { return m_Buffer; }

private:
    /// Initialize the free list by chaining all blocks together
    void InitializeFreeList();

    /// Node stored in each free block (embedded free list)
    struct FreeNode
    {
        FreeNode* next;
    };

    void* m_Buffer;         ///< Start of memory buffer
    FreeNode* m_FreeList;   ///< Head of free list
    usize m_BlockSize;      ///< Size of each block
    usize m_BlockCount;     ///< Total number of blocks
    usize m_AllocatedCount; ///< Number of currently allocated blocks
    bool m_OwnsMemory;      ///< True if we allocated the buffer
};

} // namespace Dot
