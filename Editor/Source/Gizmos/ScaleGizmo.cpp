// =============================================================================
// Dot Engine - Scale Gizmo Implementation
// =============================================================================

#include "ScaleGizmo.h"

#include "../Rendering/Camera.h"
#include "../Rendering/GizmoRenderer.h"

#include <cmath>

namespace Dot
{

void ScaleGizmo::Draw(GizmoRenderer& renderer, const Camera& camera)
{
    (void)camera;

    float size = m_Size;
    float boxSize = size * BOX_SIZE;

    // Highlight colors when hovered/active
    auto getColor = [this](GizmoAxis axis, float baseR, float baseG, float baseB)
    {
        float r = baseR, g = baseG, b = baseB, a = 1.0f;

        if (m_ActiveAxis == axis || m_HoveredAxis == axis)
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.2f; // Yellow highlight
        }

        return std::make_tuple(r, g, b, a);
    };

    // X Axis - Red line with box
    {
        auto [r, g, b, a] = getColor(GizmoAxis::X, 0.9f, 0.2f, 0.2f);
        // Line from center to axis end
        renderer.DrawLine(m_PosX, m_PosY, m_PosZ, m_PosX + size, m_PosY, m_PosZ, r, g, b, a);
        // Box at the end
        renderer.DrawBox(m_PosX + size, m_PosY, m_PosZ, boxSize, boxSize, boxSize, r, g, b, a);
    }

    // Y Axis - Green line with box
    {
        auto [r, g, b, a] = getColor(GizmoAxis::Y, 0.2f, 0.9f, 0.2f);
        renderer.DrawLine(m_PosX, m_PosY, m_PosZ, m_PosX, m_PosY + size, m_PosZ, r, g, b, a);
        renderer.DrawBox(m_PosX, m_PosY + size, m_PosZ, boxSize, boxSize, boxSize, r, g, b, a);
    }

    // Z Axis - Blue line with box
    {
        auto [r, g, b, a] = getColor(GizmoAxis::Z, 0.2f, 0.2f, 0.9f);
        renderer.DrawLine(m_PosX, m_PosY, m_PosZ, m_PosX, m_PosY, m_PosZ + size, r, g, b, a);
        renderer.DrawBox(m_PosX, m_PosY, m_PosZ + size, boxSize, boxSize, boxSize, r, g, b, a);
    }

    // Center box for uniform scale (All axis)
    {
        auto [r, g, b, a] = getColor(GizmoAxis::All, 0.8f, 0.8f, 0.8f);
        float centerSize = boxSize * 0.8f;
        renderer.DrawBox(m_PosX, m_PosY, m_PosZ, centerSize, centerSize, centerSize, r, g, b, a);
    }
}

GizmoHit ScaleGizmo::HitTest(const Ray& ray, const Camera& camera)
{
    (void)camera;

    GizmoHit result;
    float size = m_Size;
    float boxSize = size * BOX_SIZE;
    float pickRadius = size * PICK_RADIUS;
    float bestDistance = 1e9f;

    // Test center box first (uniform scale)
    {
        AABB centerBox;
        centerBox.minX = m_PosX - boxSize * 0.5f;
        centerBox.minY = m_PosY - boxSize * 0.5f;
        centerBox.minZ = m_PosZ - boxSize * 0.5f;
        centerBox.maxX = m_PosX + boxSize * 0.5f;
        centerBox.maxY = m_PosY + boxSize * 0.5f;
        centerBox.maxZ = m_PosZ + boxSize * 0.5f;

        float tMin, tMax;
        if (RayAABBIntersect(ray, centerBox, tMin, tMax) && tMin < bestDistance)
        {
            bestDistance = tMin;
            result.hit = true;
            result.axis = GizmoAxis::All;
            result.distance = tMin;
        }
    }

    // Test X axis box
    {
        AABB xBox;
        xBox.minX = m_PosX + size - boxSize;
        xBox.minY = m_PosY - boxSize * 0.5f;
        xBox.minZ = m_PosZ - boxSize * 0.5f;
        xBox.maxX = m_PosX + size + boxSize;
        xBox.maxY = m_PosY + boxSize * 0.5f;
        xBox.maxZ = m_PosZ + boxSize * 0.5f;

        float tMin, tMax;
        if (RayAABBIntersect(ray, xBox, tMin, tMax) && tMin < bestDistance)
        {
            bestDistance = tMin;
            result.hit = true;
            result.axis = GizmoAxis::X;
            result.distance = tMin;
        }
    }

    // Test Y axis box
    {
        AABB yBox;
        yBox.minX = m_PosX - boxSize * 0.5f;
        yBox.minY = m_PosY + size - boxSize;
        yBox.minZ = m_PosZ - boxSize * 0.5f;
        yBox.maxX = m_PosX + boxSize * 0.5f;
        yBox.maxY = m_PosY + size + boxSize;
        yBox.maxZ = m_PosZ + boxSize * 0.5f;

        float tMin, tMax;
        if (RayAABBIntersect(ray, yBox, tMin, tMax) && tMin < bestDistance)
        {
            bestDistance = tMin;
            result.hit = true;
            result.axis = GizmoAxis::Y;
            result.distance = tMin;
        }
    }

    // Test Z axis box
    {
        AABB zBox;
        zBox.minX = m_PosX - boxSize * 0.5f;
        zBox.minY = m_PosY - boxSize * 0.5f;
        zBox.minZ = m_PosZ + size - boxSize;
        zBox.maxX = m_PosX + boxSize * 0.5f;
        zBox.maxY = m_PosY + boxSize * 0.5f;
        zBox.maxZ = m_PosZ + size + boxSize;

        float tMin, tMax;
        if (RayAABBIntersect(ray, zBox, tMin, tMax) && tMin < bestDistance)
        {
            bestDistance = tMin;
            result.hit = true;
            result.axis = GizmoAxis::Z;
            result.distance = tMin;
        }
    }

    // Also test axis lines for easier picking
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

void ScaleGizmo::BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera)
{
    (void)camera;

    m_IsDragging = true;
    m_ActiveAxis = axis;

    m_DragStartX = m_PosX;
    m_DragStartY = m_PosY;
    m_DragStartZ = m_PosZ;

    // Find initial intersection point
    Plane constraintPlane;

    switch (axis)
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
        case GizmoAxis::All:
            constraintPlane = Plane::FromPointNormal(m_PosX, m_PosY, m_PosZ, 0, 0, 1);
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

bool ScaleGizmo::UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ)
{
    (void)camera;

    if (!m_IsDragging)
    {
        deltaX = deltaY = deltaZ = 0;
        return false;
    }

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
        case GizmoAxis::All:
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

    float dX = hitX - m_LastDragX;
    float dY = hitY - m_LastDragY;
    float dZ = hitZ - m_LastDragZ;

    // For scale, we want movement along axis to change scale
    // Positive movement = increase scale, negative = decrease
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
        case GizmoAxis::All:
            // Uniform scale - use average of movements
            {
                float avg = (dX + dY) * 0.5f;
                deltaX = avg;
                deltaY = avg;
                deltaZ = avg;
            }
            break;
        default:
            deltaX = deltaY = deltaZ = 0;
            return false;
    }

    m_LastDragX = hitX;
    m_LastDragY = hitY;
    m_LastDragZ = hitZ;

    return true;
}

void ScaleGizmo::EndDrag()
{
    m_IsDragging = false;
    m_ActiveAxis = GizmoAxis::None;
}

} // namespace Dot
