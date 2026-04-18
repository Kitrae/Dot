// =============================================================================
// Dot Engine - Ray Picker Implementation
// =============================================================================
// Simpler ray generation using camera position and orientation vectors.
// =============================================================================

#include "RayPicker.h"

#include "../Rendering/Camera.h"

#include <cmath>

namespace Dot
{

Ray RayPicker::ScreenToWorldRay(float mouseX, float mouseY, float viewportWidth, float viewportHeight,
                                const Camera& camera)
{
    // Convert screen coordinates to normalized device coordinates (-1 to 1)
    float ndcX = (2.0f * mouseX / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / viewportHeight); // Flip Y

    // Get camera position and target
    float eyeX, eyeY, eyeZ;
    float targetX, targetY, targetZ;
    camera.GetPosition(eyeX, eyeY, eyeZ);
    camera.GetTarget(targetX, targetY, targetZ);

    // Calculate forward vector (normalized)
    float forwardX = targetX - eyeX;
    float forwardY = targetY - eyeY;
    float forwardZ = targetZ - eyeZ;
    float fLen = std::sqrt(forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ);
    forwardX /= fLen;
    forwardY /= fLen;
    forwardZ /= fLen;

    // Calculate right vector (up x forward) for LH coordinate system
    float upX = 0, upY = 1, upZ = 0;
    float rightX = upY * forwardZ - upZ * forwardY;
    float rightY = upZ * forwardX - upX * forwardZ;
    float rightZ = upX * forwardY - upY * forwardX;
    float rLen = std::sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
    rightX /= rLen;
    rightY /= rLen;
    rightZ /= rLen;

    // Calculate actual up vector (forward x right) for LH coordinate system
    float trueUpX = forwardY * rightZ - forwardZ * rightY;
    float trueUpY = forwardZ * rightX - forwardX * rightZ;
    float trueUpZ = forwardX * rightY - forwardY * rightX;

    // Calculate the size of the near plane at distance 1
    float fov = camera.GetFOV();
    float aspect = camera.GetAspect();
    float tanHalfFov = std::tan(fov * 0.5f);

    // Calculate ray direction relative to camera
    float halfHeight = tanHalfFov;
    float halfWidth = halfHeight * aspect;

    // Point on the image plane at distance 1
    float rayDirX = forwardX + rightX * ndcX * halfWidth + trueUpX * ndcY * halfHeight;
    float rayDirY = forwardY + rightY * ndcX * halfWidth + trueUpY * ndcY * halfHeight;
    float rayDirZ = forwardZ + rightZ * ndcX * halfWidth + trueUpZ * ndcY * halfHeight;

    // Normalize ray direction
    float dirLen = std::sqrt(rayDirX * rayDirX + rayDirY * rayDirY + rayDirZ * rayDirZ);
    rayDirX /= dirLen;
    rayDirY /= dirLen;
    rayDirZ /= dirLen;

    // Create ray
    Ray ray;
    ray.origin = Vec3(eyeX, eyeY, eyeZ);
    ray.direction = Vec3(rayDirX, rayDirY, rayDirZ);

    return ray;
}

} // namespace Dot
