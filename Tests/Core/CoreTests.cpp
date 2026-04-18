// =============================================================================
// Dot Engine - Core Module Unit Tests
// =============================================================================

#include "Core/Core.h"

#include <gtest/gtest.h>


// =============================================================================
// Platform Detection Tests
// =============================================================================
TEST(CoreTests, PlatformDetection)
{
    // One platform should be defined
#if DOT_PLATFORM_WINDOWS
    EXPECT_TRUE(true) << "Running on Windows";
#elif DOT_PLATFORM_MACOS
    EXPECT_TRUE(true) << "Running on macOS";
#elif DOT_PLATFORM_LINUX
    EXPECT_TRUE(true) << "Running on Linux";
#else
    FAIL() << "No platform detected";
#endif
}

// =============================================================================
// Type Size Tests
// =============================================================================
TEST(CoreTests, TypeSizes)
{
    // Integer types
    EXPECT_EQ(sizeof(Dot::int8), 1);
    EXPECT_EQ(sizeof(Dot::int16), 2);
    EXPECT_EQ(sizeof(Dot::int32), 4);
    EXPECT_EQ(sizeof(Dot::int64), 8);
    EXPECT_EQ(sizeof(Dot::uint8), 1);
    EXPECT_EQ(sizeof(Dot::uint16), 2);
    EXPECT_EQ(sizeof(Dot::uint32), 4);
    EXPECT_EQ(sizeof(Dot::uint64), 8);

    // Floating point types
    EXPECT_EQ(sizeof(Dot::float32), 4);
    EXPECT_EQ(sizeof(Dot::float64), 8);
}

// =============================================================================
// Build Configuration Tests
// =============================================================================
TEST(CoreTests, BuildConfiguration)
{
    // Exactly one build configuration should be defined
    int configCount = 0;
#if DOT_DEBUG
    configCount++;
#endif
#if DOT_RELEASE
    configCount++;
#endif
#if DOT_PROFILE
    configCount++;
#endif

    EXPECT_EQ(configCount, 1) << "Exactly one build configuration should be defined";
}

// =============================================================================
// Placeholder test for future Memory module
// =============================================================================
TEST(MemoryTests, Placeholder)
{
    // This test will be replaced with real memory allocator tests
    EXPECT_TRUE(true) << "Memory module tests will be added in Stage 1.1";
}
