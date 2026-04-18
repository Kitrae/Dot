// =============================================================================
// Dot Engine - Point Light Shadow Utilities
// =============================================================================

#pragma once

#include "Core/Math/Mat4.h"
#include "Core/Math/Vec3.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Dot
{

struct PointLightShadowCandidate
{
    uint32_t lightId = 0;
    int sourceIndex = -1;
    Vec3 position = Vec3::Zero();
    float range = 0.0f;
    bool castShadows = false;
};

struct SelectedPointLightShadow
{
    uint32_t lightId = 0;
    int sourceIndex = -1;
    float distanceSq = 0.0f;
    uint32_t shadowBaseSlice = 0;
};

struct PointLightShadowFace
{
    Vec3 forward = Vec3::UnitZ();
    Vec3 up = Vec3::UnitY();
    Mat4 viewProjection = Mat4::Identity();
    float nearPlane = 0.05f;
    float farPlane = 1.0f;
};

constexpr uint32_t kMaxShadowedPointLights = 4;
constexpr uint32_t kPointShadowFacesPerLight = 6;
constexpr uint32_t kMaxPointShadowSlices = kMaxShadowedPointLights * kPointShadowFacesPerLight;
constexpr uint32_t kMaxShadowedSpotLights = 4;
constexpr uint32_t kSpotShadowFacesPerLight = 1;
constexpr uint32_t kSpotShadowSliceStart = kMaxPointShadowSlices;
constexpr uint32_t kMaxLocalShadowSlices =
    kMaxPointShadowSlices + (kMaxShadowedSpotLights * kSpotShadowFacesPerLight);
constexpr uint32_t kLocalShadowResolution = 1024;

struct SpotLightShadowCandidate
{
    uint32_t lightId = 0;
    int sourceIndex = -1;
    Vec3 position = Vec3::Zero();
    bool castShadows = false;
    float range = 0.0f;
};

struct SelectedSpotLightShadow
{
    uint32_t lightId = 0;
    int sourceIndex = -1;
    float distanceSq = 0.0f;
    uint32_t shadowSlice = 0;
};

struct SpotLightShadowFace
{
    Vec3 forward = Vec3::UnitZ();
    Vec3 up = Vec3::UnitY();
    Mat4 viewProjection = Mat4::Identity();
    float nearPlane = 0.05f;
    float farPlane = 1.0f;
    float outerConeAngleRadians = 0.0f;
};

std::vector<SelectedPointLightShadow> SelectPointLightsForShadows(
    const std::vector<PointLightShadowCandidate>& candidates, const Vec3& cameraPosition,
    uint32_t maxShadowedLights = kMaxShadowedPointLights);

std::vector<SelectedSpotLightShadow> SelectSpotLightsForShadows(
    const std::vector<SpotLightShadowCandidate>& candidates, const Vec3& cameraPosition,
    uint32_t maxShadowedLights = kMaxShadowedSpotLights);

std::array<PointLightShadowFace, kPointShadowFacesPerLight> BuildPointLightShadowFaces(const Vec3& lightPosition,
                                                                                       float range,
                                                                                       float nearPlane = 0.05f);

SpotLightShadowFace BuildSpotLightShadowFace(const Vec3& lightPosition, const Vec3& lightDirection, float range,
                                             float outerConeAngleDegrees, float nearPlane = 0.05f);

} // namespace Dot
