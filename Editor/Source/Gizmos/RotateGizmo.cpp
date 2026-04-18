// =============================================================================
// Dot Engine - Rotate Gizmo Implementation
// =============================================================================

#include "RotateGizmo.h"

#include "../Rendering/Camera.h"
#include "../Rendering/GizmoRenderer.h"

#include <cmath>

namespace Dot
{

void RotateGizmo::Draw(GizmoRenderer& renderer, const Camera& camera)
{
    (void)camera;

    float size = m_Size;

    // Highlight colors when hovered/active
    auto getColor = [this](GizmoAxis axis, float baseR, float baseG, float baseB)
    {
        float r = baseR, g = baseG, b = baseB, a = 1.0f;

        if (m_ActiveAxis == axis || m_HoveredAxis == axis)
        {
            // Brighten when hovered/active
            r = 1.0f;
            g = 1.0f;
            b = 0.2f; // Yellow highlight
        }

        return std::make_tuple(r, g, b, a);
    };

    // X Axis - Red circle (rotation around X = YZ plane)
    {
        auto [r, g, b, a] = getColor(GizmoAxis::X, 0.9f, 0.2f, 0.2f);
        renderer.DrawCircle(m_PosX, m_PosY, m_PosZ, 1, 0, 0, size, r, g, b, a);
    }

    // Y Axis - Green circle (rotation around Y = XZ plane)
    {
        auto [r, g, b, a] = getColor(GizmoAxis::Y, 0.2f, 0.9f, 0.2f);
        renderer.DrawCircle(m_PosX, m_PosY, m_PosZ, 0, 1, 0, size, r, g, b, a);
    }

    // Z Axis - Blue circle (rotation around Z = XY plane)
    {
        auto [r, g, b, a] = getColor(GizmoAxis::Z, 0.2f, 0.2f, 0.9f);
        renderer.DrawCircle(m_PosX, m_PosY, m_PosZ, 0, 0, 1, size, r, g, b, a);
    }
}

GizmoHit RotateGizmo::HitTest(const Ray& ray, const Camera& camera)
{
    (void)camera;

    GizmoHit result;
    float size = m_Size;
    float thickness = size * PICK_THICKNESS;
    float bestDistance = 1e9f;

    // Test each ring by checking if ray passes near the circle
    // For each axis, we test if the ray intersects the plane and the hit point is near the circle

    // X axis ring (YZ plane)
    {
        Plane plane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 1, 0, 0);
        float t = RayPlaneIntersect(ray, plane);
        if (t > 0 && t < bestDistance)
        {
            float hitX, hitY, hitZ;
            ray.GetPoint(t, hitX, hitY, hitZ);

            // Distance from gizmo center in the plane
            float dy = hitY - m_PosY;
            float dz = hitZ - m_PosZ;
            float distFromCenter = std::sqrt(dy * dy + dz * dz);

            // Check if near the circle edge
            if (std::fabs(distFromCenter - size) < thickness)
            {
                bestDistance = t;
                result.hit = true;
                result.axis = GizmoAxis::X;
                result.distance = t;
            }
        }
    }

    // Y axis ring (XZ plane)
    {
        Plane plane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
        float t = RayPlaneIntersect(ray, plane);
        if (t > 0 && t < bestDistance)
        {
            float hitX, hitY, hitZ;
            ray.GetPoint(t, hitX, hitY, hitZ);

            float dx = hitX - m_PosX;
            float dz = hitZ - m_PosZ;
            float distFromCenter = std::sqrt(dx * dx + dz * dz);

            if (std::fabs(distFromCenter - size) < thickness)
            {
                bestDistance = t;
                result.hit = true;
                result.axis = GizmoAxis::Y;
                result.distance = t;
            }
        }
    }

    // Z axis ring (XY plane)
    {
        Plane plane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 0, 1);
        float t = RayPlaneIntersect(ray, plane);
        if (t > 0 && t < bestDistance)
        {
            float hitX, hitY, hitZ;
            ray.GetPoint(t, hitX, hitY, hitZ);

            float dx = hitX - m_PosX;
            float dy = hitY - m_PosY;
            float distFromCenter = std::sqrt(dx * dx + dy * dy);

            if (std::fabs(distFromCenter - size) < thickness)
            {
                bestDistance = t;
                result.hit = true;
                result.axis = GizmoAxis::Z;
                result.distance = t;
            }
        }
    }

    return result;
}

void RotateGizmo::BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera)
{
    (void)camera;

    m_IsDragging = true;
    m_ActiveAxis = axis;

    // Store starting gizmo position
    m_DragStartX = m_PosX;
    m_DragStartY = m_PosY;
    m_DragStartZ = m_PosZ;

    // Calculate initial angle based on ray intersection
    Plane constraintPlane;

    switch (axis)
    {
        case GizmoAxis::X:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 1, 0, 0);
            break;
        case GizmoAxis::Y:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
        case GizmoAxis::Z:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 0, 1);
            break;
        default:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
    }

    float t = RayPlaneIntersect(ray, constraintPlane);
    if (t > 0)
    {
        float hitX, hitY, hitZ;
        ray.GetPoint(t, hitX, hitY, hitZ);

        // Calculate angle from center to hit point
        switch (axis)
        {
            case GizmoAxis::X:
                m_StartAngle = std::atan2(hitZ - m_PosZ, hitY - m_PosY);
                break;
            case GizmoAxis::Y:
                m_StartAngle = std::atan2(hitZ - m_PosZ, hitX - m_PosX);
                break;
            case GizmoAxis::Z:
                m_StartAngle = std::atan2(hitY - m_PosY, hitX - m_PosX);
                break;
            default:
                m_StartAngle = 0;
                break;
        }
    }
    else
    {
        m_StartAngle = 0;
    }

    m_LastAngle = m_StartAngle;
}

bool RotateGizmo::UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ)
{
    (void)camera;

    if (!m_IsDragging)
    {
        deltaX = deltaY = deltaZ = 0;
        return false;
    }

    // Create constraint plane based on active axis
    Plane constraintPlane;

    switch (m_ActiveAxis)
    {
        case GizmoAxis::X:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 1, 0, 0);
            break;
        case GizmoAxis::Y:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
        case GizmoAxis::Z:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 0, 1);
            break;
        default:
            deltaX = deltaY = deltaZ = 0;
            return false;
    }

    float t = RayPlaneIntersect(ray, constraintPlane);
    if (t <= 0)
    {
        deltaX = deltaY = deltaZ = 0;
        return false;
    }

    float hitX, hitY, hitZ;
    ray.GetPoint(t, hitX, hitY, hitZ);

    // Calculate current angle
    float currentAngle = 0;
    switch (m_ActiveAxis)
    {
        case GizmoAxis::X:
            currentAngle = std::atan2(hitZ - m_PosZ, hitY - m_PosY);
            break;
        case GizmoAxis::Y:
            currentAngle = std::atan2(hitZ - m_PosZ, hitX - m_PosX);
            break;
        case GizmoAxis::Z:
            currentAngle = std::atan2(hitY - m_PosY, hitX - m_PosX);
            break;
        default:
            break;
    }

    // Calculate angle delta (in radians)
    float angleDelta = currentAngle - m_LastAngle;

    // Handle wrap-around
    if (angleDelta > 3.14159f)
        angleDelta -= 6.28318f;
    if (angleDelta < -3.14159f)
        angleDelta += 6.28318f;

    // Convert to degrees for output
    float angleDeltaDeg = angleDelta * (180.0f / 3.14159f);

    // Output: deltaX/Y/Z represent rotation around X/Y/Z axis (in degrees)
    switch (m_ActiveAxis)
    {
        case GizmoAxis::X:
            deltaX = angleDeltaDeg;
            deltaY = 0;
            deltaZ = 0;
            break;
        case GizmoAxis::Y:
            deltaX = 0;
            deltaY = angleDeltaDeg;
            deltaZ = 0;
            break;
        case GizmoAxis::Z:
            deltaX = 0;
            deltaY = 0;
            deltaZ = angleDeltaDeg;
            break;
        default:
            deltaX = deltaY = deltaZ = 0;
            return false;
    }

    m_LastAngle = currentAngle;
    return true;
}

void RotateGizmo::EndDrag()
{
    m_IsDragging = false;
    m_ActiveAxis = GizmoAxis::None;
}

} // namespace Dot
