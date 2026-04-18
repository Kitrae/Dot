#include "../../Engine/Runtime/Shared/Rendering/StableAOPattern.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

using namespace Dot;

TEST(StableAOPatternTests, KernelSamplesStayInTheUpperHemisphere)
{
    const auto& kernel = GetStableAOKernel();
    ASSERT_EQ(kernel.size(), GetStableAOKernelSampleCount());

    for (const auto& sample : kernel)
    {
        EXPECT_GE(sample[2], 0.0f);
    }
}

TEST(StableAOPatternTests, NoisePatternUsesOnlyPlanarRotationVectors)
{
    const auto& noise = GetStableAONoisePattern();
    ASSERT_EQ(noise.size(), GetStableAONoiseTexelCount());

    for (const auto& sample : noise)
    {
        EXPECT_NEAR(sample[2], 0.0f, 1e-5f);
        EXPECT_NEAR(sample[3], 1.0f, 1e-5f);
    }
}

TEST(StableAOPatternTests, NoisePatternProvidesManyUniqueRotationVectors)
{
    const auto& noise = GetStableAONoisePattern();
    std::unordered_set<std::string> uniqueDirections;

    for (const auto& sample : noise)
    {
        uniqueDirections.insert(std::to_string(static_cast<int>(sample[0] * 1000.0f)) + ":" +
                                std::to_string(static_cast<int>(sample[1] * 1000.0f)));
    }

    EXPECT_GE(uniqueDirections.size(), 48u);
}

TEST(StableAOPatternTests, UploadContractUsesDeclaredHelperSizes)
{
    EXPECT_EQ(GetStableAOKernelSampleCount(), 16u);
    EXPECT_EQ(GetStableAOKernelFloatCount(), 64u);
    EXPECT_EQ(GetStableAOKernelByteCount(), 256u);

    EXPECT_EQ(GetStableAONoiseWidth(), 8u);
    EXPECT_EQ(GetStableAONoiseHeight(), 8u);
    EXPECT_EQ(GetStableAONoiseTexelCount(), 64u);
    EXPECT_EQ(GetStableAONoiseFloatCount(), 256u);
    EXPECT_EQ(GetStableAONoiseByteCount(), 1024u);
}
