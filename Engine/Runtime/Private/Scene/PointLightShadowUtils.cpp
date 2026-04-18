// =============================================================================
// Dot Engine - Point Light Shadow Utilities
// =============================================================================

#include "Core/Scene/PointLightShadowUtils.h"

#include <algorithm>
#include <cmath>

namespace Dot
{

namespace
{

Mat4 BuildLookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 forward = (target - eye).Normalized();
    const Vec3 right = Vec3::Cross(up, forward).Normalized();
    const Vec3 orthoUp = Vec3::Cross(forward, right);

    Mat4 view(1.0f);
    view[0] = Vec4(right.x, orthoUp.x, forward.x, 0.0f);
    view[1] = Vec4(right.y, orthoUp.y, forward.y, 0.0f);
    view[2] = Vec4(right.z, orthoUp.z, forward.z, 0.0f);
    view[3] = Vec4(-Vec3::Dot(right, eye), -Vec3::Dot(orthoUp, eye), -Vec3::Dot(forward, eye), 1.0f);
    return view;
}

Mat4 BuildPerspectiveFovLH(float fovYRadians, float aspect, float nearPlane, float farPlane)
{
    const float h = 1.0f / std::tan(fovYRadians * 0.5f);
    const float w = h / aspect;

    Mat4 projection(0.0f);
    projection[0].x = w;
    projection[1].y = h;
    projection[2].z = farPlane / (farPlane - nearPlane);
    projection[2].w = 1.0f;
    projection[3].z = -(nearPlane * farPlane) / (farPlane - nearPlane);
    return projection;
}

} // namespace

std::vector<SelectedPointLightShadow> SelectPointLightsForShadows(const std::vector<PointLightShadowCandidate>& candidates,
                                                                  const Vec3& cameraPosition,
                                                                  uint32_t maxShadowedLights)
{
    std::vector<SelectedPointLightShadow> selected;
    selected.reserve(std::min<uint32_t>(maxShadowedLights, static_cast<uint32_t>(candidates.size())));

    for (const PointLightShadowCandidate& candidate : candidates)
    {
        if (!candidate.castShadows || candidate.range <= 0.0f)
            continue;

        const Vec3 delta = candidate.position - cameraPosition;
        SelectedPointLightShadow shadowed;
        shadowed.lightId = candidate.lightId;
        shadowed.sourceIndex = candidate.sourceIndex;
        shadowed.distanceSq = Vec3::Dot(delta, delta);
        selected.push_back(shadowed);
    }

    std::sort(selected.begin(), selected.end(),
              [](const SelectedPointLightShadow& a, const SelectedPointLightShadow& b)
              {
                  if (a.distanceSq != b.distanceSq)
                      return a.distanceSq < b.distanceSq;
                  return a.lightId < b.lightId;
              });

    if (selected.size() > maxShadowedLights)
        selected.resize(maxShadowedLights);

    for (uint32_t i = 0; i < selected.size(); ++i)
        selected[i].shadowBaseSlice = i * kPointShadowFacesPerLight;

    return selected;
}

std::vector<SelectedSpotLightShadow> SelectSpotLightsForShadows(const std::vector<SpotLightShadowCandidate>& candidates,
                                                                const Vec3& cameraPosition,
                                                                uint32_t maxShadowedLights)
{
    std::vector<SelectedSpotLightShadow> selected;
    selected.reserve(std::min<uint32_t>(maxShadowedLights, static_cast<uint32_t>(candidates.size())));

    for (const SpotLightShadowCandidate& candidate : candidates)
    {
        if (!candidate.castShadows || candidate.range <= 0.0f)
            continue;

        const Vec3 delta = candidate.position - cameraPosition;
        SelectedSpotLightShadow shadowed;
        shadowed.lightId = candidate.lightId;
        shadowed.sourceIndex = candidate.sourceIndex;
        shadowed.distanceSq = Vec3::Dot(delta, delta);
        selected.push_back(shadowed);
    }

    std::sort(selected.begin(), selected.end(),
              [](const SelectedSpotLightShadow& a, const SelectedSpotLightShadow& b)
              {
                  if (a.distanceSq != b.distanceSq)
                      return a.distanceSq < b.distanceSq;
                  return a.lightId < b.lightId;
              });

    if (selected.size() > maxShadowedLights)
        selected.resize(maxShadowedLights);

    for (uint32_t i = 0; i < selected.size(); ++i)
        selected[i].shadowSlice = kSpotShadowSliceStart + (i * kSpotShadowFacesPerLight);

    return selected;
}

std::array<PointLightShadowFace, kPointShadowFacesPerLight> BuildPointLightShadowFaces(const Vec3& lightPosition,
                                                                                       float range,
                                                                                       float nearPlane)
{
    const float clampedRange = std::max(range, nearPlane + 0.001f);
    const float clampedNear = std::max(0.01f, std::min(nearPlane, clampedRange * 0.5f));
    const float fov = 3.14159265f * 0.5f;

    struct FaceBasis
    {
        Vec3 forward;
        Vec3 up;
    };

    const std::array<FaceBasis, kPointShadowFacesPerLight> bases = {{
        {Vec3::UnitX(), Vec3::UnitY()},
        {-Vec3::UnitX(), Vec3::UnitY()},
        {Vec3::UnitY(), -Vec3::UnitZ()},
        {-Vec3::UnitY(), Vec3::UnitZ()},
        {Vec3::UnitZ(), Vec3::UnitY()},
        {-Vec3::UnitZ(), Vec3::UnitY()},
    }};

    std::array<PointLightShadowFace, kPointShadowFacesPerLight> faces = {};
    const Mat4 projection = BuildPerspectiveFovLH(fov, 1.0f, clampedNear, clampedRange);
    for (size_t i = 0; i < faces.size(); ++i)
    {
        faces[i].forward = bases[i].forward;
        faces[i].up = bases[i].up;
        faces[i].nearPlane = clampedNear;
        faces[i].farPlane = clampedRange;
        const Mat4 view = BuildLookAtLH(lightPosition, lightPosition + bases[i].forward, bases[i].up);
        faces[i].viewProjection = projection * view;
    }

    return faces;
}

SpotLightShadowFace BuildSpotLightShadowFace(const Vec3& lightPosition, const Vec3& lightDirection, float range,
                                             float outerConeAngleDegrees, float nearPlane)
{
    SpotLightShadowFace face = {};
    const float clampedRange = std::max(range, nearPlane + 0.001f);
    const float clampedNear = std::max(0.01f, std::min(nearPlane, clampedRange * 0.5f));
    const float outerAngleRadians = std::clamp(outerConeAngleDegrees, 1.0f, 89.0f) * (3.14159265f / 180.0f);
    const float fov = std::min(outerAngleRadians * 2.0f, 3.12413936f);

    Vec3 forward = lightDirection.Normalized();
    if (forward.LengthSquared() < 0.0001f)
        forward = Vec3::UnitZ();

    Vec3 referenceUp = std::abs(Vec3::Dot(forward, Vec3::UnitY())) > 0.99f ? Vec3::UnitZ() : Vec3::UnitY();
    Vec3 right = Vec3::Cross(referenceUp, forward).Normalized();
    Vec3 up = Vec3::Cross(forward, right).Normalized();

    face.forward = forward;
    face.up = up;
    face.nearPlane = clampedNear;
    face.farPlane = clampedRange;
    face.outerConeAngleRadians = outerAngleRadians;

    const Mat4 projection = BuildPerspectiveFovLH(fov, 1.0f, clampedNear, clampedRange);
    const Mat4 view = BuildLookAtLH(lightPosition, lightPosition + forward, up);
    face.viewProjection = projection * view;
    return face;
}

} // namespace Dot
