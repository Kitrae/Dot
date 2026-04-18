// =============================================================================
// Dot Engine - Allocator Interface
// =============================================================================
// Base interface for all memory allocators in the engine.
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

// -----------------------------------------------------------------------------
// Core Types (duplicated to avoid circular include with Core.h)
// These must match the definitions in Core/Core.h
// -----------------------------------------------------------------------------
namespace Dot
{
using usize = std::size_t;
using uintptr = std::uintptr_t;
} // namespace Dot

// -----------------------------------------------------------------------------
// DLL Export (duplicated to avoid circular include with Core.h)
// -----------------------------------------------------------------------------
#ifndef DOT_CORE_API
    #if defined(_WIN32) || defined(_WIN64)
        #ifdef DOT_CORE_EXPORTS
            #define DOT_CORE_API __declspec(dllexport)
        #else
            #define DOT_CORE_API __declspec(dllimport)
        #endif
    #else
        #define DOT_CORE_API __attribute__((visibility("default")))
    #endif

    #ifndef DOT_BUILD_SHARED
        #undef DOT_CORE_API
        #define DOT_CORE_API
    #endif
#endif

namespace Dot
{

/// Base interface for custom memory allocators.
/// All allocators must implement Allocate and provide their own memory strategy.
class DOT_CORE_API IAllocator
{
public:
    virtual ~IAllocator() = default;

    /// Allocate memory with specified size and alignment.
    /// @param size Size in bytes to allocate
    /// @param alignment Memory alignment (must be power of 2, default 16)
    /// @return Pointer to allocated memory, or nullptr if allocation failed
    virtual void* Allocate(usize size, usize alignment = 16) = 0;

    /// Free previously allocated memory.
    /// Note: Some allocators (Linear, Stack) may not support individual frees.
    /// @param ptr Pointer to memory to free
    virtual void Free(void* ptr) = 0;

    /// Reset the allocator, freeing all allocations at once.
    /// This is the primary deallocation method for Linear and Stack allocators.
    virtual void Reset() = 0;

    /// Get total capacity of the allocator in bytes.
    virtual usize GetCapacity() const = 0;

    /// Get currently used memory in bytes.
    virtual usize GetUsedMemory() const = 0;

    /// Get remaining available memory in bytes.
    usize GetFreeMemory() const { return GetCapacity() - GetUsedMemory(); }

    // Non-copyable
    IAllocator(const IAllocator&) = delete;
    IAllocator& operator=(const IAllocator&) = delete;

protected:
    IAllocator() = default;

    /// Helper: Align a pointer to the specified alignment.
    /// @param ptr The address to align
    /// @param alignment Alignment boundary (must be power of 2)
    /// @return Aligned address (>= ptr)
    static uintptr AlignForward(uintptr ptr, usize alignment)
    {
        const uintptr mask = alignment - 1;
        return (ptr + mask) & ~mask;
    }

    /// Helper: Calculate padding needed to align a pointer.
    static usize CalculatePadding(uintptr ptr, usize alignment)
    {
        return static_cast<usize>(AlignForward(ptr, alignment) - ptr);
    }
};

} // namespace Dot
