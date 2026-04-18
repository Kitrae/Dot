// =============================================================================
// Dot Engine - Linear Allocator Implementation
// =============================================================================

#include "Core/Memory/LinearAllocator.h"

#include "Core/Core.h"

#include <cstdlib>
#include <cstring>
#include <utility>


namespace Dot
{

LinearAllocator::LinearAllocator(usize capacity)
    : m_Buffer(nullptr), m_Capacity(capacity), m_Offset(0), m_AllocationCount(0), m_OwnsMemory(true)
{
    DOT_ASSERT(capacity > 0 && "Capacity must be greater than 0");

    constexpr usize kAlignment = 16;
    const usize allocSize = (capacity + kAlignment - 1) & ~(kAlignment - 1);

#if DOT_PLATFORM_WINDOWS
    m_Buffer = _aligned_malloc(allocSize, kAlignment);
#else
    m_Buffer = std::aligned_alloc(kAlignment, allocSize);
#endif

    DOT_ASSERT(m_Buffer != nullptr && "Failed to allocate memory for LinearAllocator");

#if DOT_DEBUG
    if (m_Buffer)
    {
        std::memset(m_Buffer, 0xCD, allocSize);
    }
#endif
}

LinearAllocator::LinearAllocator(void* buffer, usize capacity)
    : m_Buffer(buffer), m_Capacity(capacity), m_Offset(0), m_AllocationCount(0), m_OwnsMemory(false)
{
    DOT_ASSERT(buffer != nullptr && "Buffer cannot be null");
    DOT_ASSERT(capacity > 0 && "Capacity must be greater than 0");
}

LinearAllocator::~LinearAllocator()
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
    m_Capacity = 0;
    m_Offset = 0;
}

LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    : m_Buffer(std::exchange(other.m_Buffer, nullptr)), m_Capacity(std::exchange(other.m_Capacity, 0)),
      m_Offset(std::exchange(other.m_Offset, 0)), m_AllocationCount(std::exchange(other.m_AllocationCount, 0)),
      m_OwnsMemory(std::exchange(other.m_OwnsMemory, false))
{
}

LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept
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
        m_Capacity = std::exchange(other.m_Capacity, 0);
        m_Offset = std::exchange(other.m_Offset, 0);
        m_AllocationCount = std::exchange(other.m_AllocationCount, 0);
        m_OwnsMemory = std::exchange(other.m_OwnsMemory, false);
    }
    return *this;
}

void* LinearAllocator::Allocate(usize size, usize alignment)
{
    DOT_ASSERT(size > 0 && "Allocation size must be greater than 0");
    DOT_ASSERT((alignment & (alignment - 1)) == 0 && "Alignment must be power of 2");

    uintptr currentAddr = reinterpret_cast<uintptr>(m_Buffer) + m_Offset;
    usize padding = CalculatePadding(currentAddr, alignment);
    usize alignedOffset = m_Offset + padding;
    usize newOffset = alignedOffset + size;

    if (newOffset > m_Capacity)
    {
        DOT_ASSERT(false && "LinearAllocator out of memory");
        return nullptr;
    }

    m_Offset = newOffset;
    m_AllocationCount++;

    return reinterpret_cast<void*>(reinterpret_cast<uintptr>(m_Buffer) + alignedOffset);
}

void LinearAllocator::Free([[maybe_unused]] void* ptr)
{
    // Linear allocator does not support individual frees.
    // Memory is only freed via Reset().
}

void LinearAllocator::Reset()
{
    m_Offset = 0;
    m_AllocationCount = 0;

#if DOT_DEBUG
    if (m_Buffer)
    {
        std::memset(m_Buffer, 0xDD, m_Capacity);
    }
#endif
}

} // namespace Dot
