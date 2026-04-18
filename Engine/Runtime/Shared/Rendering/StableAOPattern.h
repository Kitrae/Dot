#pragma once

#include <array>
#include <cstddef>
#include <cmath>

namespace Dot
{

using StableAOSample = std::array<float, 4>;

inline constexpr size_t kStableAOKernelSampleCount = 16;
inline constexpr size_t kStableAONoiseWidth = 8;
inline constexpr size_t kStableAONoiseHeight = 8;
inline constexpr size_t kStableAOChannelsPerSample = 4;

inline constexpr size_t GetStableAOKernelSampleCount()
{
    return kStableAOKernelSampleCount;
}

inline constexpr size_t GetStableAOKernelFloatCount()
{
    return GetStableAOKernelSampleCount() * kStableAOChannelsPerSample;
}

inline constexpr size_t GetStableAOKernelByteCount()
{
    return GetStableAOKernelFloatCount() * sizeof(float);
}

inline constexpr size_t GetStableAONoiseWidth()
{
    return kStableAONoiseWidth;
}

inline constexpr size_t GetStableAONoiseHeight()
{
    return kStableAONoiseHeight;
}

inline constexpr size_t GetStableAONoiseTexelCount()
{
    return GetStableAONoiseWidth() * GetStableAONoiseHeight();
}

inline constexpr size_t GetStableAONoiseFloatCount()
{
    return GetStableAONoiseTexelCount() * kStableAOChannelsPerSample;
}

inline constexpr size_t GetStableAONoiseByteCount()
{
    return GetStableAONoiseFloatCount() * sizeof(float);
}

inline const std::array<StableAOSample, kStableAOKernelSampleCount>& GetStableAOKernel()
{
    static const std::array<StableAOSample, kStableAOKernelSampleCount> kKernel = {{
        {{0.00f, 0.00f, 0.16f, 0.0f}},
        {{0.24f, 0.00f, 0.24f, 0.0f}},
        {{-0.22f, 0.10f, 0.28f, 0.0f}},
        {{0.09f, -0.26f, 0.32f, 0.0f}},
        {{0.30f, 0.18f, 0.38f, 0.0f}},
        {{-0.32f, -0.14f, 0.42f, 0.0f}},
        {{0.14f, 0.34f, 0.46f, 0.0f}},
        {{-0.16f, 0.32f, 0.50f, 0.0f}},
        {{0.40f, -0.04f, 0.56f, 0.0f}},
        {{-0.38f, 0.08f, 0.60f, 0.0f}},
        {{0.24f, -0.34f, 0.64f, 0.0f}},
        {{-0.10f, -0.40f, 0.68f, 0.0f}},
        {{0.42f, 0.26f, 0.74f, 0.0f}},
        {{-0.40f, 0.22f, 0.78f, 0.0f}},
        {{0.04f, 0.48f, 0.84f, 0.0f}},
        {{-0.26f, -0.44f, 0.90f, 0.0f}},
    }};
    return kKernel;
}

inline const std::array<StableAOSample, GetStableAONoiseTexelCount()>& GetStableAONoisePattern()
{
    static const std::array<StableAOSample, GetStableAONoiseTexelCount()> kNoise = []()
    {
        std::array<StableAOSample, GetStableAONoiseTexelCount()> noise{};
        constexpr float kTau = 6.28318530718f;

        for (size_t y = 0; y < GetStableAONoiseHeight(); ++y)
        {
            for (size_t x = 0; x < GetStableAONoiseWidth(); ++x)
            {
                const size_t index = y * GetStableAONoiseWidth() + x;
                const uint32_t hash = static_cast<uint32_t>((x + 1u) * 73856093u) ^
                                      static_cast<uint32_t>((y + 1u) * 19349663u) ^ 0x9E3779B9u;
                const float angle = (static_cast<float>(hash & 8191u) / 8192.0f) * kTau;
                noise[index] = {{std::cos(angle), std::sin(angle), 0.0f, 1.0f}};
            }
        }

        return noise;
    }();
    return kNoise;
}

inline const std::array<StableAOSample, GetStableAONoiseTexelCount()>& GetStableAONoise4x4()
{
    return GetStableAONoisePattern();
}

} // namespace Dot
