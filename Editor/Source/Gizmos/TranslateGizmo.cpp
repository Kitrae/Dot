// =============================================================================
// Dot Engine - Translate Gizmo Implementation
// =============================================================================

#include "TranslateGizmo.h"

#include "../Rendering/Camera.h"
#include "../Rendering/GizmoRenderer.h"

namespace Dot
{

void TranslateGizmo::Draw(GizmoRenderer& renderer, const Camera& camera)
{
    (void)camera; // May be used for screen-space sizing later

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

    // Get local axis directions (rotated if in local mode)
    float xDirX = 1, xDirY = 0, xDirZ = 0;
    float yDirX = 0, yDirY = 1, yDirZ = 0;
    float zDirX = 0, zDirY = 0, zDirZ = 1;

    TransformByRotation(xDirX, xDirY, xDirZ);
    TransformByRotation(yDirX, yDirY, yDirZ);
    TransformByRotation(zDirX, zDirY, zDirZ);

    // X Axis - Red
    {
        auto [r, g, b, a] = getColor(GizmoAxis::X, 0.9f, 0.2f, 0.2f);
        renderer.DrawArrow(m_PosX, m_PosY, m_PosZ, xDirX, xDirY, xDirZ, size, r, g, b, a);
    }

    // Y Axis - Green
    {
        auto [r, g, b, a] = getColor(GizmoAxis::Y, 0.2f, 0.9f, 0.2f);
        renderer.DrawArrow(m_PosX, m_PosY, m_PosZ, yDirX, yDirY, yDirZ, size, r, g, b, a);
    }

    // Z Axis - Blue
    {
        auto [r, g, b, a] = getColor(GizmoAxis::Z, 0.2f, 0.2f, 0.9f);
        renderer.DrawArrow(m_PosX, m_PosY, m_PosZ, zDirX, zDirY, zDirZ, size, r, g, b, a);
    }
}

GizmoHit TranslateGizmo::HitTest(const Ray& ray, const Camera& camera)
{
    (void)camera;

    GizmoHit result;
    float size = m_Size;
    float pickRadius = size * PICK_RADIUS;
    float bestDistance = 1e9f;

    // Test X axis (line from origin to +X)
    {
        float rayT, segT;
        float dist = RayLineSegmentDistance(ray, m_PosX, m_PosY, m_PosZ, m_PosX + size, m_PosY, m_PosZ, rayT, segT);

        if (dist < pickRadius && rayT < bestDistance && segT > 0.1f)
        {
            bestDistance = rayT;
            result.hit = true;
            result.axis = GizmoAxis::X;
            result.distance = rayT;
        }
    }

    // Test Y axis
    {
        float rayT, segT;
        float dist = RayLineSegmentDistance(ray, m_PosX, m_PosY, m_PosZ, m_PosX, m_PosY + size, m_PosZ, rayT, segT);

        if (dist < pickRadius && rayT < bestDistance && segT > 0.1f)
        {
            bestDistance = rayT;
            result.hit = true;
            result.axis = GizmoAxis::Y;
            result.distance = rayT;
        }
    }

    // Test Z axis
    {
        float rayT, segT;
        float dist = RayLineSegmentDistance(ray, m_PosX, m_PosY, m_PosZ, m_PosX, m_PosY, m_PosZ + size, rayT, segT);

        if (dist < pickRadius && rayT < bestDistance && segT > 0.1f)
        {
            bestDistance = rayT;
            result.hit = true;
            result.axis = GizmoAxis::Z;
            result.distance = rayT;
        }
    }

    return result;
}

void TranslateGizmo::BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera)
{
    (void)camera;

    m_IsDragging = true;
    m_ActiveAxis = axis;

    // Store starting position
    m_DragStartX = m_PosX;
    m_DragStartY = m_PosY;
    m_DragStartZ = m_PosZ;

    // Find initial intersection point on the constraint plane
    Plane constraintPlane;

    switch (axis)
    {
        case GizmoAxis::X:
            // Constrain to plane containing X axis, perpendicular to view direction
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
        case GizmoAxis::Y:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 0, 1);
            break;
        case GizmoAxis::Z:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
        default:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
    }

    float t = RayPlaneIntersect(ray, constraintPlane);
    if (t > 0)
    {
        ray.GetPoint(t, m_LastDragX, m_LastDragY, m_LastDragZ);
    }
    else
    {
        m_LastDragX = m_PosX;
        m_LastDragY = m_PosY;
        m_LastDragZ = m_PosZ;
    }
}

bool TranslateGizmo::UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ)
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
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
            break;
        case GizmoAxis::Y:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 0, 1);
            break;
        case GizmoAxis::Z:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 1, 0);
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

    // Calculate delta from last position
    float dX = hitX - m_LastDragX;
    float dY = hitY - m_LastDragY;
    float dZ = hitZ - m_LastDragZ;

    // Constrain to active axis
    switch (m_ActiveAxis)
    {
        case GizmoAxis::X:
            deltaX = dX;
            deltaY = 0;
            deltaZ = 0;
            break;
        case GizmoAxis::Y:
            deltaX = 0;
            deltaY = dY;
            deltaZ = 0;
            break;
        case GizmoAxis::Z:
            deltaX = 0;
            deltaY = 0;
            deltaZ = dZ;
            break;
        default:
            deltaX = deltaY = deltaZ = 0;
            return false;
    }

    // Update last drag position
    m_LastDragX = hitX;
    m_LastDragY = hitY;
    m_LastDragZ = hitZ;

    return true;
}

void TranslateGizmo::EndDrag()
{
    m_IsDragging = false;
    m_ActiveAxis = GizmoAxis::None;
}

} // namespace Dot
