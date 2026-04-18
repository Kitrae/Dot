// =============================================================================
// Dot Engine - Pool Allocator Implementation
// =============================================================================

#include "Core/Memory/PoolAllocator.h"

#include "Core/Core.h"

#include <cstdlib>
#include <cstring>
#include <utility>


namespace Dot
{

PoolAllocator::PoolAllocator(usize blockSize, usize blockCount)
    : m_Buffer(nullptr), m_FreeList(nullptr), m_BlockSize(blockSize), m_BlockCount(blockCount), m_AllocatedCount(0),
      m_OwnsMemory(true)
{
    DOT_ASSERT(blockCount > 0 && "Block count must be greater than 0");

    constexpr usize kMinBlockSize = sizeof(FreeNode);
    constexpr usize kAlignment = 16;

    m_BlockSize = ((blockSize < kMinBlockSize ? kMinBlockSize : blockSize) + kAlignment - 1) & ~(kAlignment - 1);

    const usize totalSize = m_BlockSize * blockCount;

#if DOT_PLATFORM_WINDOWS
    m_Buffer = _aligned_malloc(totalSize, kAlignment);
#else
    const usize alignedSize = (totalSize + kAlignment - 1) & ~(kAlignment - 1);
    m_Buffer = std::aligned_alloc(kAlignment, alignedSize);
#endif

    DOT_ASSERT(m_Buffer != nullptr && "Failed to allocate memory for PoolAllocator");

#if DOT_DEBUG
    if (m_Buffer)
    {
        std::memset(m_Buffer, 0xCD, totalSize);
    }
#endif

    InitializeFreeList();
}

PoolAllocator::PoolAllocator(void* buffer, usize blockSize, usize blockCount)
    : m_Buffer(buffer), m_FreeList(nullptr), m_BlockSize(blockSize), m_BlockCount(blockCount), m_AllocatedCount(0),
      m_OwnsMemory(false)
{
    DOT_ASSERT(buffer != nullptr && "Buffer cannot be null");
    DOT_ASSERT(blockCount > 0 && "Block count must be greater than 0");

    constexpr usize kMinBlockSize = sizeof(FreeNode);
    DOT_ASSERT(blockSize >= kMinBlockSize && "Block size too small for free list");

    InitializeFreeList();
}

PoolAllocator::~PoolAllocator()
{
    if (m_OwnsMemory && m_Buffer)
    {
#if DOT_PLATFORM_WINDOWS
        _aligned_free(m_Buffer);
#else
        std::free(m_Buffer);
#endif
    }
    m_Buffer = nullptr;
    m_FreeList = nullptr;
    m_BlockCount = 0;
    m_AllocatedCount = 0;
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : m_Buffer(std::exchange(other.m_Buffer, nullptr)), m_FreeList(std::exchange(other.m_FreeList, nullptr)),
      m_BlockSize(std::exchange(other.m_BlockSize, 0)), m_BlockCount(std::exchange(other.m_BlockCount, 0)),
      m_AllocatedCount(std::exchange(other.m_AllocatedCount, 0)), m_OwnsMemory(std::exchange(other.m_OwnsMemory, false))
{
}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept
{
    if (this != &other)
    {
        if (m_OwnsMemory && m_Buffer)
        {
#if DOT_PLATFORM_WINDOWS
            _aligned_free(m_Buffer);
#else
            std::free(m_Buffer);
#endif
        }

        m_Buffer = std::exchange(other.m_Buffer, nullptr);
        m_FreeList = std::exchange(other.m_FreeList, nullptr);
        m_BlockSize = std::exchange(other.m_BlockSize, 0);
        m_BlockCount = std::exchange(other.m_BlockCount, 0);
        m_AllocatedCount = std::exchange(other.m_AllocatedCount, 0);
        m_OwnsMemory = std::exchange(other.m_OwnsMemory, false);
    }
    return *this;
}

void PoolAllocator::InitializeFreeList()
{
    uintptr start = reinterpret_cast<uintptr>(m_Buffer);

    FreeNode* prev = nullptr;
    for (usize i = m_BlockCount; i > 0; --i)
    {
        uintptr blockAddr = start + (i - 1) * m_BlockSize;
        auto* node = reinterpret_cast<FreeNode*>(blockAddr);
        node->next = prev;
        prev = node;
    }

    m_FreeList = prev;
}

void* PoolAllocator::Allocate([[maybe_unused]] usize size, [[maybe_unused]] usize alignment)
{
    if (m_FreeList == nullptr)
    {
        DOT_ASSERT(false && "PoolAllocator exhausted");
        return nullptr;
    }

    FreeNode* block = m_FreeList;
    m_FreeList = block->next;
    m_AllocatedCount++;

    return block;
}

void PoolAllocator::Free(void* ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    uintptr ptrAddr = reinterpret_cast<uintptr>(ptr);
    uintptr bufferStart = reinterpret_cast<uintptr>(m_Buffer);
    uintptr bufferEnd = bufferStart + (m_BlockSize * m_BlockCount);

    DOT_ASSERT(ptrAddr >= bufferStart && ptrAddr < bufferEnd && "Pointer not from this pool");
    DOT_ASSERT((ptrAddr - bufferStart) % m_BlockSize == 0 && "Pointer not aligned to block boundary");

    auto* node = static_cast<FreeNode*>(ptr);
    node->next = m_FreeList;
    m_FreeList = node;
    m_AllocatedCount--;
}

void PoolAllocator::Reset()
{
    m_AllocatedCount = 0;

#if DOT_DEBUG
    if (m_Buffer)
    {
        std::memset(m_Buffer, 0xDD, m_BlockSize * m_BlockCount);
    }
#endif

    InitializeFreeList();
}

} // namespace Dot
