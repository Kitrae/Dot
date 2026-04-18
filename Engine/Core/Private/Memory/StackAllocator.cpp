// =============================================================================
// Dot Engine - Stack Allocator Implementation
// =============================================================================

#include "Core/Memory/StackAllocator.h"

#include "Core/Core.h"

#include <cstdlib>
#include <cstring>
#include <utility>


namespace Dot
{

StackAllocator::StackAllocator(usize capacity)
    : m_Buffer(nullptr), m_Capacity(capacity), m_Offset(0), m_AllocationCount(0), m_OwnsMemory(true)
#if DOT_DEBUG
      ,
      m_LastAllocation(nullptr)
#endif
{
    DOT_ASSERT(capacity > 0 && "Capacity must be greater than 0");

    constexpr usize kAlignment = 16;
    const usize allocSize = (capacity + kAlignment - 1) & ~(kAlignment - 1);

#if DOT_PLATFORM_WINDOWS
    m_Buffer = _aligned_malloc(allocSize, kAlignment);
#else
    m_Buffer = std::aligned_alloc(kAlignment, allocSize);
#endif

    DOT_ASSERT(m_Buffer != nullptr && "Failed to allocate memory for StackAllocator");

#if DOT_DEBUG
    if (m_Buffer)
    {
        std::memset(m_Buffer, 0xCD, allocSize);
    }
#endif
}

StackAllocator::StackAllocator(void* buffer, usize capacity)
    : m_Buffer(buffer), m_Capacity(capacity), m_Offset(0), m_AllocationCount(0), m_OwnsMemory(false)
#if DOT_DEBUG
      ,
      m_LastAllocation(nullptr)
#endif
{
    DOT_ASSERT(buffer != nullptr && "Buffer cannot be null");
    DOT_ASSERT(capacity > 0 && "Capacity must be greater than 0");
}

StackAllocator::~StackAllocator()
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

StackAllocator::StackAllocator(StackAllocator&& other) noexcept
    : m_Buffer(std::exchange(other.m_Buffer, nullptr)), m_Capacity(std::exchange(other.m_Capacity, 0)),
      m_Offset(std::exchange(other.m_Offset, 0)), m_AllocationCount(std::exchange(other.m_AllocationCount, 0)),
      m_OwnsMemory(std::exchange(other.m_OwnsMemory, false))
#if DOT_DEBUG
      ,
      m_LastAllocation(std::exchange(other.m_LastAllocation, nullptr))
#endif
{
}

StackAllocator& StackAllocator::operator=(StackAllocator&& other) noexcept
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
#if DOT_DEBUG
        m_LastAllocation = std::exchange(other.m_LastAllocation, nullptr);
#endif
    }
    return *this;
}

void* StackAllocator::Allocate(usize size, usize alignment)
{
    DOT_ASSERT(size > 0 && "Allocation size must be greater than 0");
    DOT_ASSERT((alignment & (alignment - 1)) == 0 && "Alignment must be power of 2");

    const usize headerSize = sizeof(AllocationHeader);

    uintptr bufferStart = reinterpret_cast<uintptr>(m_Buffer);
    uintptr headerAddr = bufferStart + m_Offset;
    uintptr dataAddr = headerAddr + headerSize;

    usize padding = CalculatePadding(dataAddr, alignment);
    uintptr alignedDataAddr = dataAddr + padding;

    usize totalSize = headerSize + padding + size;
    usize newOffset = m_Offset + totalSize;

    if (newOffset > m_Capacity)
    {
        DOT_ASSERT(false && "StackAllocator out of memory");
        return nullptr;
    }

    uintptr headerLocation = alignedDataAddr - headerSize;
    auto* header = reinterpret_cast<AllocationHeader*>(headerLocation);
    header->prevOffset = m_Offset;

    m_Offset = newOffset;
    m_AllocationCount++;

    void* result = reinterpret_cast<void*>(alignedDataAddr);

#if DOT_DEBUG
    m_LastAllocation = result;
#endif

    return result;
}

void StackAllocator::Free(void* ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    uintptr ptrAddr = reinterpret_cast<uintptr>(ptr);
    uintptr bufferStart = reinterpret_cast<uintptr>(m_Buffer);
    uintptr bufferEnd = bufferStart + m_Offset;
    DOT_ASSERT(ptrAddr >= bufferStart && ptrAddr < bufferEnd && "Pointer not from this allocator");

    auto* header = reinterpret_cast<AllocationHeader*>(ptrAddr - sizeof(AllocationHeader));
    DOT_ASSERT(header->prevOffset < m_Offset && "Corrupted allocation header");

    m_Offset = header->prevOffset;
    m_AllocationCount--;
}

void StackAllocator::Reset()
{
    m_Offset = 0;
    m_AllocationCount = 0;

#if DOT_DEBUG
    m_LastAllocation = nullptr;
    if (m_Buffer)
    {
        std::memset(m_Buffer, 0xDD, m_Capacity);
    }
#endif
}

void StackAllocator::FreeToMarker(Marker marker)
{
    DOT_ASSERT(marker.offset <= m_Offset && "Invalid marker - points beyond current allocation");

    m_Offset = marker.offset;
    m_AllocationCount = marker.allocationCount;

#if DOT_DEBUG
    m_LastAllocation = nullptr;
#endif
}

} // namespace Dot
