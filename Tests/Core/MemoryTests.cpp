// =============================================================================
// Dot Engine - Memory Allocator Unit Tests
// =============================================================================

#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/MemoryTracker.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/Memory/StackAllocator.h"

#include <gtest/gtest.h>

using namespace Dot;

// =============================================================================
// LinearAllocator Basic Tests
// =============================================================================

TEST(LinearAllocatorTests, Construction)
{
    LinearAllocator allocator(1024);

    EXPECT_EQ(allocator.GetCapacity(), 1024);
    EXPECT_EQ(allocator.GetUsedMemory(), 0);
    EXPECT_EQ(allocator.GetFreeMemory(), 1024);
    EXPECT_EQ(allocator.GetAllocationCount(), 0);
    EXPECT_NE(allocator.GetBuffer(), nullptr);
}

TEST(LinearAllocatorTests, ExternalBuffer)
{
    alignas(16) char buffer[512];
    LinearAllocator allocator(buffer, sizeof(buffer));

    EXPECT_EQ(allocator.GetCapacity(), 512);
    EXPECT_EQ(allocator.GetBuffer(), buffer);
}

TEST(LinearAllocatorTests, BasicAllocation)
{
    LinearAllocator allocator(1024);

    void* ptr = allocator.Allocate(64);

    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(allocator.GetAllocationCount(), 1);
    EXPECT_GE(allocator.GetUsedMemory(), 64);
}

TEST(LinearAllocatorTests, MultipleAllocations)
{
    LinearAllocator allocator(1024);

    void* ptr1 = allocator.Allocate(100);
    void* ptr2 = allocator.Allocate(100);
    void* ptr3 = allocator.Allocate(100);

    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr3, nullptr);

    // Pointers should be sequential (with possible alignment padding)
    EXPECT_LT(ptr1, ptr2);
    EXPECT_LT(ptr2, ptr3);

    EXPECT_EQ(allocator.GetAllocationCount(), 3);
}

// =============================================================================
// Alignment Tests
// =============================================================================

TEST(LinearAllocatorTests, DefaultAlignment)
{
    LinearAllocator allocator(1024);

    void* ptr = allocator.Allocate(32);

    // Default alignment is 16 bytes
    uintptr addr = reinterpret_cast<uintptr>(ptr);
    EXPECT_EQ(addr % 16, 0) << "Pointer should be 16-byte aligned";
}

TEST(LinearAllocatorTests, CustomAlignment)
{
    LinearAllocator allocator(1024);

    // Allocate with various alignments
    void* ptr1 = allocator.Allocate(1, 1);   // 1-byte aligned
    void* ptr32 = allocator.Allocate(1, 32); // 32-byte aligned
    void* ptr64 = allocator.Allocate(1, 64); // 64-byte aligned

    EXPECT_EQ(reinterpret_cast<uintptr>(ptr1) % 1, 0);
    EXPECT_EQ(reinterpret_cast<uintptr>(ptr32) % 32, 0);
    EXPECT_EQ(reinterpret_cast<uintptr>(ptr64) % 64, 0);
}

TEST(LinearAllocatorTests, StructAlignment)
{
    struct alignas(32) AlignedStruct
    {
        float data[8];
    };

    LinearAllocator allocator(1024);

    void* ptr = allocator.Allocate(sizeof(AlignedStruct), alignof(AlignedStruct));

    EXPECT_EQ(reinterpret_cast<uintptr>(ptr) % 32, 0);
}

// =============================================================================
// Reset Tests
// =============================================================================

TEST(LinearAllocatorTests, Reset)
{
    LinearAllocator allocator(1024);

    allocator.Allocate(100);
    allocator.Allocate(100);
    allocator.Allocate(100);

    EXPECT_GT(allocator.GetUsedMemory(), 0);
    EXPECT_EQ(allocator.GetAllocationCount(), 3);

    allocator.Reset();

    EXPECT_EQ(allocator.GetUsedMemory(), 0);
    EXPECT_EQ(allocator.GetAllocationCount(), 0);
    EXPECT_EQ(allocator.GetFreeMemory(), 1024);
}

TEST(LinearAllocatorTests, ReuseAfterReset)
{
    LinearAllocator allocator(256);

    // Fill the allocator
    void* firstPtr = allocator.Allocate(200);
    uintptr firstAddr = reinterpret_cast<uintptr>(firstPtr);

    allocator.Reset();

    // Should be able to allocate again from the beginning
    void* secondPtr = allocator.Allocate(200);
    uintptr secondAddr = reinterpret_cast<uintptr>(secondPtr);

    // Both allocations should start at the same aligned position
    EXPECT_EQ(firstAddr, secondAddr);
}

// =============================================================================
// Move Semantics Tests
// =============================================================================

TEST(LinearAllocatorTests, MoveConstruction)
{
    LinearAllocator allocator1(1024);
    void* originalBuffer = allocator1.GetBuffer();
    allocator1.Allocate(100);

    LinearAllocator allocator2(std::move(allocator1));

    EXPECT_EQ(allocator2.GetBuffer(), originalBuffer);
    EXPECT_EQ(allocator2.GetCapacity(), 1024);
    EXPECT_GE(allocator2.GetUsedMemory(), 100);

    // Moved-from allocator should be empty
    EXPECT_EQ(allocator1.GetBuffer(), nullptr);
    EXPECT_EQ(allocator1.GetCapacity(), 0);
}

TEST(LinearAllocatorTests, MoveAssignment)
{
    LinearAllocator allocator1(1024);
    LinearAllocator allocator2(512);

    void* buffer1 = allocator1.GetBuffer();
    allocator1.Allocate(100);

    allocator2 = std::move(allocator1);

    EXPECT_EQ(allocator2.GetBuffer(), buffer1);
    EXPECT_EQ(allocator2.GetCapacity(), 1024);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(LinearAllocatorTests, FreeIsNoOp)
{
    LinearAllocator allocator(1024);

    void* ptr = allocator.Allocate(100);
    usize usedBefore = allocator.GetUsedMemory();

    allocator.Free(ptr);

    // Free should not change usage (it's a no-op)
    EXPECT_EQ(allocator.GetUsedMemory(), usedBefore);
}

TEST(LinearAllocatorTests, SmallAllocations)
{
    LinearAllocator allocator(1024);

    // Many small allocations
    for (int i = 0; i < 10; ++i)
    {
        void* ptr = allocator.Allocate(1, 1);
        EXPECT_NE(ptr, nullptr);
    }

    EXPECT_EQ(allocator.GetAllocationCount(), 10);
}

// =============================================================================
// StackAllocator Basic Tests
// =============================================================================

TEST(StackAllocatorTests, Construction)
{
    StackAllocator allocator(1024);

    EXPECT_EQ(allocator.GetCapacity(), 1024);
    EXPECT_EQ(allocator.GetUsedMemory(), 0);
    EXPECT_EQ(allocator.GetAllocationCount(), 0);
    EXPECT_NE(allocator.GetBuffer(), nullptr);
}

TEST(StackAllocatorTests, BasicAllocation)
{
    StackAllocator allocator(1024);

    void* ptr = allocator.Allocate(64);

    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(allocator.GetAllocationCount(), 1);
    EXPECT_GT(allocator.GetUsedMemory(), 64); // Includes header overhead
}

TEST(StackAllocatorTests, Alignment)
{
    StackAllocator allocator(1024);

    void* ptr16 = allocator.Allocate(32, 16);
    void* ptr32 = allocator.Allocate(32, 32);
    void* ptr64 = allocator.Allocate(32, 64);

    EXPECT_EQ(reinterpret_cast<uintptr>(ptr16) % 16, 0);
    EXPECT_EQ(reinterpret_cast<uintptr>(ptr32) % 32, 0);
    EXPECT_EQ(reinterpret_cast<uintptr>(ptr64) % 64, 0);
}

// =============================================================================
// StackAllocator LIFO Free Tests
// =============================================================================

TEST(StackAllocatorTests, LIFOFree)
{
    StackAllocator allocator(1024);

    void* ptr1 = allocator.Allocate(64);
    usize usedAfterFirst = allocator.GetUsedMemory();

    void* ptr2 = allocator.Allocate(64);
    EXPECT_GT(allocator.GetUsedMemory(), usedAfterFirst);

    // Free in LIFO order
    allocator.Free(ptr2);
    EXPECT_EQ(allocator.GetUsedMemory(), usedAfterFirst);

    allocator.Free(ptr1);
    EXPECT_EQ(allocator.GetUsedMemory(), 0);
}

TEST(StackAllocatorTests, Reset)
{
    StackAllocator allocator(1024);

    allocator.Allocate(100);
    allocator.Allocate(100);
    allocator.Allocate(100);

    allocator.Reset();

    EXPECT_EQ(allocator.GetUsedMemory(), 0);
    EXPECT_EQ(allocator.GetAllocationCount(), 0);
}

// =============================================================================
// StackAllocator Marker Tests
// =============================================================================

TEST(StackAllocatorTests, MarkerRollback)
{
    StackAllocator allocator(1024);

    allocator.Allocate(64);
    auto marker = allocator.GetMarker();
    usize usedAtMarker = allocator.GetUsedMemory();

    // Allocate more
    allocator.Allocate(64);
    allocator.Allocate(64);
    EXPECT_GT(allocator.GetUsedMemory(), usedAtMarker);

    // Rollback to marker
    allocator.FreeToMarker(marker);
    EXPECT_EQ(allocator.GetUsedMemory(), usedAtMarker);
}

TEST(StackAllocatorTests, MultipleMarkers)
{
    StackAllocator allocator(2048);

    auto marker0 = allocator.GetMarker();
    allocator.Allocate(100);

    auto marker1 = allocator.GetMarker();
    allocator.Allocate(100);

    auto marker2 = allocator.GetMarker();
    allocator.Allocate(100);

    // Markers should be in increasing order
    EXPECT_LT(marker0.offset, marker1.offset);
    EXPECT_LT(marker1.offset, marker2.offset);

    // Can rollback to any marker
    allocator.FreeToMarker(marker1);
    EXPECT_EQ(allocator.GetUsedMemory(), marker1.offset);
    EXPECT_EQ(allocator.GetAllocationCount(), marker1.allocationCount);

    // Can still allocate after rollback
    void* newPtr = allocator.Allocate(50);
    EXPECT_NE(newPtr, nullptr);
}

// =============================================================================
// StackAllocator Move Semantics
// =============================================================================

TEST(StackAllocatorTests, MoveConstruction)
{
    StackAllocator allocator1(1024);
    void* originalBuffer = allocator1.GetBuffer();
    allocator1.Allocate(100);

    StackAllocator allocator2(std::move(allocator1));

    EXPECT_EQ(allocator2.GetBuffer(), originalBuffer);
    EXPECT_EQ(allocator2.GetCapacity(), 1024);
    EXPECT_EQ(allocator1.GetBuffer(), nullptr);
}

TEST(StackAllocatorTests, MoveAssignment)
{
    StackAllocator allocator1(1024);
    StackAllocator allocator2(512);

    void* buffer1 = allocator1.GetBuffer();
    allocator1.Allocate(100);

    allocator2 = std::move(allocator1);

    EXPECT_EQ(allocator2.GetBuffer(), buffer1);
    EXPECT_EQ(allocator2.GetCapacity(), 1024);
}

// =============================================================================
// PoolAllocator Basic Tests
// =============================================================================

TEST(PoolAllocatorTests, Construction)
{
    PoolAllocator pool(64, 10);

    EXPECT_EQ(pool.GetBlockCount(), 10);
    EXPECT_GE(pool.GetBlockSize(), 64); // May be rounded up for alignment
    EXPECT_EQ(pool.GetAllocationCount(), 0);
    EXPECT_EQ(pool.GetFreeBlockCount(), 10);
    EXPECT_NE(pool.GetBuffer(), nullptr);
}

TEST(PoolAllocatorTests, BasicAllocation)
{
    PoolAllocator pool(64, 10);

    void* ptr = pool.Allocate();

    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(pool.GetAllocationCount(), 1);
    EXPECT_EQ(pool.GetFreeBlockCount(), 9);
}

TEST(PoolAllocatorTests, MultipleAllocations)
{
    PoolAllocator pool(64, 10);

    void* ptrs[5];
    for (int i = 0; i < 5; ++i)
    {
        ptrs[i] = pool.Allocate();
        EXPECT_NE(ptrs[i], nullptr);
    }

    EXPECT_EQ(pool.GetAllocationCount(), 5);
    EXPECT_EQ(pool.GetFreeBlockCount(), 5);

    // All pointers should be unique
    for (int i = 0; i < 5; ++i)
    {
        for (int j = i + 1; j < 5; ++j)
        {
            EXPECT_NE(ptrs[i], ptrs[j]);
        }
    }
}

// =============================================================================
// PoolAllocator Free Tests
// =============================================================================

TEST(PoolAllocatorTests, FreeAndReuse)
{
    PoolAllocator pool(64, 10);

    void* ptr1 = pool.Allocate();
    pool.Free(ptr1);

    EXPECT_EQ(pool.GetAllocationCount(), 0);
    EXPECT_EQ(pool.GetFreeBlockCount(), 10);

    // Should reuse the freed block
    void* ptr2 = pool.Allocate();
    EXPECT_EQ(ptr1, ptr2); // Same block reused (LIFO free list)
}

TEST(PoolAllocatorTests, AllocateAll)
{
    PoolAllocator pool(64, 5);

    // Allocate all blocks
    for (int i = 0; i < 5; ++i)
    {
        void* ptr = pool.Allocate();
        EXPECT_NE(ptr, nullptr);
    }

    EXPECT_EQ(pool.GetAllocationCount(), 5);
    EXPECT_EQ(pool.GetFreeBlockCount(), 0);
}

TEST(PoolAllocatorTests, Reset)
{
    PoolAllocator pool(64, 10);

    // Allocate some blocks
    for (int i = 0; i < 5; ++i)
    {
        pool.Allocate();
    }

    EXPECT_EQ(pool.GetAllocationCount(), 5);

    pool.Reset();

    EXPECT_EQ(pool.GetAllocationCount(), 0);
    EXPECT_EQ(pool.GetFreeBlockCount(), 10);
}

// =============================================================================
// PoolAllocator Alignment Tests
// =============================================================================

TEST(PoolAllocatorTests, BlockAlignment)
{
    PoolAllocator pool(64, 10);

    void* ptr = pool.Allocate();

    // Blocks should be 16-byte aligned
    uintptr addr = reinterpret_cast<uintptr>(ptr);
    EXPECT_EQ(addr % 16, 0);
}

// =============================================================================
// PoolAllocator Move Semantics
// =============================================================================

TEST(PoolAllocatorTests, MoveConstruction)
{
    PoolAllocator pool1(64, 10);
    void* originalBuffer = pool1.GetBuffer();
    pool1.Allocate();

    PoolAllocator pool2(std::move(pool1));

    EXPECT_EQ(pool2.GetBuffer(), originalBuffer);
    EXPECT_EQ(pool2.GetBlockCount(), 10);
    EXPECT_EQ(pool2.GetAllocationCount(), 1);
    EXPECT_EQ(pool1.GetBuffer(), nullptr);
}

TEST(PoolAllocatorTests, MoveAssignment)
{
    PoolAllocator pool1(64, 10);
    PoolAllocator pool2(32, 5);

    void* buffer1 = pool1.GetBuffer();
    pool1.Allocate();

    pool2 = std::move(pool1);

    EXPECT_EQ(pool2.GetBuffer(), buffer1);
    EXPECT_EQ(pool2.GetBlockCount(), 10);
}

// =============================================================================
// MemoryTracker Basic Tests
// =============================================================================

class MemoryTrackerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Reset tracker before each test
        MemoryTracker::Get().Reset();
    }

    void TearDown() override
    {
        // Reset to clean state
        MemoryTracker::Get().Reset();
    }
};

TEST_F(MemoryTrackerTests, InitialState)
{
    auto stats = MemoryTracker::Get().GetStats();

    EXPECT_EQ(stats.totalAllocations, 0);
    EXPECT_EQ(stats.totalDeallocations, 0);
    EXPECT_EQ(stats.activeAllocations, 0);
    EXPECT_EQ(stats.currentBytesInUse, 0);
    EXPECT_EQ(stats.peakBytesInUse, 0);
    EXPECT_FALSE(MemoryTracker::Get().HasLeaks());
}

TEST_F(MemoryTrackerTests, TrackAllocation)
{
    int dummy = 42;
    void* ptr = &dummy;

    DOT_TRACK_ALLOC(ptr, 100);

    auto stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.totalAllocations, 1);
    EXPECT_EQ(stats.activeAllocations, 1);
    EXPECT_EQ(stats.currentBytesInUse, 100);
    EXPECT_TRUE(MemoryTracker::Get().HasLeaks());

    DOT_TRACK_FREE(ptr);

    stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.totalDeallocations, 1);
    EXPECT_EQ(stats.activeAllocations, 0);
    EXPECT_EQ(stats.currentBytesInUse, 0);
    EXPECT_FALSE(MemoryTracker::Get().HasLeaks());
}

TEST_F(MemoryTrackerTests, PeakMemory)
{
    int dummies[3] = {1, 2, 3};

    DOT_TRACK_ALLOC(&dummies[0], 100);
    DOT_TRACK_ALLOC(&dummies[1], 200);
    DOT_TRACK_ALLOC(&dummies[2], 150);

    // Peak should be 450
    auto stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.peakBytesInUse, 450);
    EXPECT_EQ(stats.currentBytesInUse, 450);

    DOT_TRACK_FREE(&dummies[1]); // Free 200

    stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.peakBytesInUse, 450); // Peak unchanged
    EXPECT_EQ(stats.currentBytesInUse, 250);

    // Cleanup
    DOT_TRACK_FREE(&dummies[0]);
    DOT_TRACK_FREE(&dummies[2]);
}

TEST_F(MemoryTrackerTests, MultipleAllocations)
{
    int dummies[5];

    for (int i = 0; i < 5; ++i)
    {
        DOT_TRACK_ALLOC(&dummies[i], 64);
    }

    auto stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.totalAllocations, 5);
    EXPECT_EQ(stats.activeAllocations, 5);
    EXPECT_EQ(stats.currentBytesInUse, 320);

    for (int i = 0; i < 5; ++i)
    {
        DOT_TRACK_FREE(&dummies[i]);
    }

    stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.totalDeallocations, 5);
    EXPECT_EQ(stats.activeAllocations, 0);
}

TEST_F(MemoryTrackerTests, Reset)
{
    int dummy;
    DOT_TRACK_ALLOC(&dummy, 100);

    MemoryTracker::Get().Reset();

    auto stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.totalAllocations, 0);
    EXPECT_EQ(stats.activeAllocations, 0);
    EXPECT_FALSE(MemoryTracker::Get().HasLeaks());
}

TEST_F(MemoryTrackerTests, TaggedAllocation)
{
    int dummy;
    DOT_TRACK_ALLOC_TAGGED(&dummy, 256, "Textures");

    auto stats = MemoryTracker::Get().GetStats();
    EXPECT_EQ(stats.currentBytesInUse, 256);

    DOT_TRACK_FREE(&dummy);
}
