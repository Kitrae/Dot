// =============================================================================
// Dot Engine - Core Module Public Header
// =============================================================================
// Main include for the Core module
// This header provides platform detection, build configuration, and core types
// =============================================================================

#pragma once

// -----------------------------------------------------------------------------
// Platform Detection
// -----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define DOT_PLATFORM_WINDOWS 1
    #define DOT_PLATFORM_NAME "Windows"
#elif defined(__APPLE__) && defined(__MACH__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define DOT_PLATFORM_MACOS 1
        #define DOT_PLATFORM_NAME "macOS"
    #endif
#elif defined(__linux__)
    #define DOT_PLATFORM_LINUX 1
    #define DOT_PLATFORM_NAME "Linux"
#else
    #error "Unsupported platform"
#endif

// Ensure only one platform is defined
#if (defined(DOT_PLATFORM_WINDOWS) + defined(DOT_PLATFORM_MACOS) + defined(DOT_PLATFORM_LINUX)) != 1
    #error "Multiple platforms detected"
#endif

// -----------------------------------------------------------------------------
// Compiler Detection
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
    #define DOT_COMPILER_MSVC 1
    #define DOT_COMPILER_VERSION _MSC_VER
#elif defined(__clang__)
    #define DOT_COMPILER_CLANG 1
    #define DOT_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#elif defined(__GNUC__)
    #define DOT_COMPILER_GCC 1
    #define DOT_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
    #error "Unsupported compiler"
#endif

// -----------------------------------------------------------------------------
// Build Configuration
// -----------------------------------------------------------------------------
#if !defined(DOT_DEBUG) && !defined(DOT_RELEASE) && !defined(DOT_PROFILE)
    #if defined(_DEBUG) || defined(DEBUG)
        #define DOT_DEBUG 1
    #else
        #define DOT_RELEASE 1
    #endif
#endif

// -----------------------------------------------------------------------------
// DLL Export/Import Macros
// -----------------------------------------------------------------------------
#ifndef DOT_CORE_API
    #if DOT_PLATFORM_WINDOWS
        #ifdef DOT_CORE_EXPORTS
            #define DOT_CORE_API __declspec(dllexport)
        #else
            #define DOT_CORE_API __declspec(dllimport)
        #endif
    #else
        #define DOT_CORE_API __attribute__((visibility("default")))
    #endif
#endif

// For static library builds, API macro is empty
#ifndef DOT_BUILD_SHARED
    #undef DOT_CORE_API
    #define DOT_CORE_API
#endif

// -----------------------------------------------------------------------------
// Compiler Hints
// -----------------------------------------------------------------------------
#if DOT_COMPILER_MSVC
    #define DOT_FORCEINLINE __forceinline
    #define DOT_NOINLINE __declspec(noinline)
    #define DOT_RESTRICT __restrict
    #define DOT_LIKELY(x) (x)
    #define DOT_UNLIKELY(x) (x)
    #define DOT_ALIGN(n) __declspec(align(n))
#else
    #define DOT_FORCEINLINE inline __attribute__((always_inline))
    #define DOT_NOINLINE __attribute__((noinline))
    #define DOT_RESTRICT __restrict__
    #define DOT_LIKELY(x) __builtin_expect(!!(x), 1)
    #define DOT_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define DOT_ALIGN(n) __attribute__((aligned(n)))
#endif

// -----------------------------------------------------------------------------
// Debug Utilities
// -----------------------------------------------------------------------------
#if DOT_DEBUG
    #if DOT_PLATFORM_WINDOWS
        #define DOT_DEBUGBREAK() __debugbreak()
    #elif DOT_COMPILER_CLANG || DOT_COMPILER_GCC
        #define DOT_DEBUGBREAK() __builtin_trap()
    #else
        #include <signal.h>
        #define DOT_DEBUGBREAK() raise(SIGTRAP)
    #endif
#else
    #define DOT_DEBUGBREAK() ((void)0)
#endif

// -----------------------------------------------------------------------------
// Assertions
// -----------------------------------------------------------------------------
#if DOT_COMPILER_MSVC
    // Suppress "conditional expression is constant" warning for assertions
    #define DOT_DISABLE_CONST_EXPR_WARNING __pragma(warning(push)) __pragma(warning(disable : 4127))
    #define DOT_RESTORE_WARNINGS __pragma(warning(pop))
#else
    #define DOT_DISABLE_CONST_EXPR_WARNING
    #define DOT_RESTORE_WARNINGS
#endif

#if DOT_DEBUG
    #define DOT_ASSERT(condition)                                                                                      \
        do                                                                                                             \
        {                                                                                                              \
            DOT_DISABLE_CONST_EXPR_WARNING                                                                             \
            if (!(condition))                                                                                          \
            {                                                                                                          \
                DOT_DEBUGBREAK();                                                                                      \
            }                                                                                                          \
            DOT_RESTORE_WARNINGS                                                                                       \
        } while (0)

    #define DOT_ASSERT_MSG(condition, msg)                                                                             \
        do                                                                                                             \
        {                                                                                                              \
            DOT_DISABLE_CONST_EXPR_WARNING                                                                             \
            if (!(condition))                                                                                          \
            {                                                                                                          \
                DOT_DEBUGBREAK();                                                                                      \
            }                                                                                                          \
            DOT_RESTORE_WARNINGS                                                                                       \
        } while (0)
#else
    #define DOT_ASSERT(condition) ((void)0)
    #define DOT_ASSERT_MSG(condition, msg) ((void)0)
#endif

// Compile-time assert
#define DOT_STATIC_ASSERT(condition, msg) static_assert(condition, msg)

// -----------------------------------------------------------------------------
// Core Types
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace Dot
{
// Fixed-width integer types
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// Size types
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Floating point
using float32 = float;
using float64 = double;

// Pointer types
using uintptr = std::uintptr_t;
using intptr = std::intptr_t;

// Compile-time size verification
DOT_STATIC_ASSERT(sizeof(int8) == 1, "int8 must be 1 byte");
DOT_STATIC_ASSERT(sizeof(int16) == 2, "int16 must be 2 bytes");
DOT_STATIC_ASSERT(sizeof(int32) == 4, "int32 must be 4 bytes");
DOT_STATIC_ASSERT(sizeof(int64) == 8, "int64 must be 8 bytes");
DOT_STATIC_ASSERT(sizeof(float32) == 4, "float32 must be 4 bytes");
DOT_STATIC_ASSERT(sizeof(float64) == 8, "float64 must be 8 bytes");

} // namespace Dot

// -----------------------------------------------------------------------------
// Memory Module Headers (Stage 1.1)
// -----------------------------------------------------------------------------
#include "Core/Memory/Allocator.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/MemorySystem.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/Memory/StackAllocator.h"

// -----------------------------------------------------------------------------
// Jobs Module Headers (Stage 1.2)
// -----------------------------------------------------------------------------
#include "Core/Jobs/Job.h"
#include "Core/Jobs/JobSystem.h"

// -----------------------------------------------------------------------------
// Math Module Headers (Stage 1.3)
// -----------------------------------------------------------------------------
// #include "Core/Math/Vector.h"
// #include "Core/Math/Matrix.h"
// #include "Core/Math/Quaternion.h"
