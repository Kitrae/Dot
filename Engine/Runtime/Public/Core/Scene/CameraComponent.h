// =============================================================================
// Dot Engine - Camera Component
// =============================================================================
// Entity-based camera component for scene rendering.
// =============================================================================

#pragma once

#include <cstdint>

namespace Dot
{

namespace RenderLayerMask
{
constexpr uint32_t World = 1u << 0;
constexpr uint32_t Viewmodel = 1u << 1;
} // namespace RenderLayerMask

/// Camera Component - Represents a viewable camera in the scene
struct CameraComponent
{
    float fov = 60.0f;                    // Field of view in degrees (vertical)
    float nearPlane = 0.01f;              // Near clipping plane distance
    float farPlane = 1000.0f;             // Far clipping plane distance
    bool isActive = false;                // If true, this is the main camera during play mode
    uint32_t renderMask = RenderLayerMask::World;
    bool enableViewmodelPass = false;
    uint32_t viewmodelMask = RenderLayerMask::Viewmodel;
    float viewmodelFov = 75.0f;
    float viewmodelNearPlane = 0.01f;

    CameraComponent() = default;
    CameraComponent(float fieldOfView, float nearP = 0.01f, float farP = 1000.0f, bool active = false)
        : fov(fieldOfView), nearPlane(nearP), farPlane(farP), isActive(active), renderMask(RenderLayerMask::World),
          enableViewmodelPass(false), viewmodelMask(RenderLayerMask::Viewmodel), viewmodelFov(75.0f),
          viewmodelNearPlane(0.01f)
    {
    }
};

} // namespace Dot
