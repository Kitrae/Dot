// =============================================================================
// Dot Engine - Gizmo Base Interface
// =============================================================================
// Abstract base class for all editor gizmos.
// =============================================================================

#pragma once

#include "RayMath.h"

#include <cmath>


namespace Dot
{

class GizmoRenderer;
class Camera;

/// Which axis is currently being interacted with
enum class GizmoAxis
{
    None = 0,
    X,
    Y,
    Z,
    XY,   // XY plane
    XZ,   // XZ plane
    YZ,   // YZ plane
    View, // Screen-space
    All   // Uniform (all axes)
};

/// Gizmo interaction result
struct GizmoHit
{
    bool hit = false;
    GizmoAxis axis = GizmoAxis::None;
    float distance = 0;
};

/// Abstract base class for gizmos
class Gizmo
{
public:
    virtual ~Gizmo() = default;

    /// Set the world-space position of the gizmo
    virtual void SetPosition(float x, float y, float z)
    {
        m_PosX = x;
        m_PosY = y;
        m_PosZ = z;
    }

    /// Get the world-space position
    void GetPosition(float& x, float& y, float& z) const
    {
        x = m_PosX;
        y = m_PosY;
        z = m_PosZ;
    }

    /// Set the size/scale of the gizmo visualization
    virtual void SetSize(float size) { m_Size = size; }
    float GetSize() const { return m_Size; }

    /// Draw the gizmo (call between Begin/End on GizmoRenderer)
    virtual void Draw(GizmoRenderer& renderer, const Camera& camera) = 0;

    /// Test if a ray hits this gizmo, returns which axis was hit
    virtual GizmoHit HitTest(const Ray& ray, const Camera& camera) = 0;

    /// Begin interaction with the gizmo
    virtual void BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera) = 0;

    /// Update during drag, returns delta movement in world space
    virtual bool UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ) = 0;

    /// End interaction
    virtual void EndDrag() = 0;

    /// Is currently being dragged?
    bool IsDragging() const { return m_IsDragging; }

    /// Get currently active axis
    GizmoAxis GetActiveAxis() const { return m_ActiveAxis; }

    /// Get currently hovered axis
    GizmoAxis GetHoveredAxis() const { return m_HoveredAxis; }

    /// Set hovered axis (for highlighting)
    void SetHoveredAxis(GizmoAxis axis) { m_HoveredAxis = axis; }

protected:
    float m_PosX = 0, m_PosY = 0, m_PosZ = 0;
    float m_RotX = 0, m_RotY = 0, m_RotZ = 0; // Object rotation (euler angles in degrees)
    float m_Size = 1.0f;
    bool m_UseLocalSpace = false;

    bool m_IsDragging = false;
    GizmoAxis m_ActiveAxis = GizmoAxis::None;
    GizmoAxis m_HoveredAxis = GizmoAxis::None;

    // Drag state
    float m_DragStartX = 0, m_DragStartY = 0, m_DragStartZ = 0;

public:
    /// Set the object's rotation for local-space gizmo orientation (euler angles in degrees)
    virtual void SetRotation(float rx, float ry, float rz)
    {
        m_RotX = rx;
        m_RotY = ry;
        m_RotZ = rz;
    }

    /// Enable/disable local space mode
    void SetLocalSpace(bool local) { m_UseLocalSpace = local; }
    bool IsLocalSpace() const { return m_UseLocalSpace; }

    /// Helper: Transform a world direction by the object's rotation (ZYX euler)
    void TransformByRotation(float& x, float& y, float& z) const
    {
        if (!m_UseLocalSpace)
            return;

        const float deg2rad = 3.14159265f / 180.0f;
        float rx = m_RotX * deg2rad;
        float ry = m_RotY * deg2rad;
        float rz = m_RotZ * deg2rad;

        float cx = std::cos(rx), sx = std::sin(rx);
        float cy = std::cos(ry), sy = std::sin(ry);
        float cz = std::cos(rz), sz = std::sin(rz);

        // Build rotation matrix (ZYX order) and transform vector
        float inX = x, inY = y, inZ = z;

        // Column 0 of rotation matrix
        float r00 = cy * cz;
        float r10 = cy * sz;
        float r20 = -sy;

        // Column 1
        float r01 = sx * sy * cz - cx * sz;
        float r11 = sx * sy * sz + cx * cz;
        float r21 = sx * cy;

        // Column 2
        float r02 = cx * sy * cz + sx * sz;
        float r12 = cx * sy * sz - sx * cz;
        float r22 = cx * cy;

        // Transform (matrix * vector)
        x = r00 * inX + r01 * inY + r02 * inZ;
        y = r10 * inX + r11 * inY + r12 * inZ;
        z = r20 * inX + r21 * inY + r22 * inZ;
    }

    /// Helper: Inverse transform (world to local)
    void InverseTransformByRotation(float& x, float& y, float& z) const
    {
        if (!m_UseLocalSpace)
            return;

        const float deg2rad = 3.14159265f / 180.0f;
        float rx = m_RotX * deg2rad;
        float ry = m_RotY * deg2rad;
        float rz = m_RotZ * deg2rad;

        float cx = std::cos(rx), sx = std::sin(rx);
        float cy = std::cos(ry), sy = std::sin(ry);
        float cz = std::cos(rz), sz = std::sin(rz);

        // Transpose of rotation matrix (inverse for orthonormal)
        float inX = x, inY = y, inZ = z;

        float r00 = cy * cz;
        float r01 = cy * sz;
        float r02 = -sy;

        float r10 = sx * sy * cz - cx * sz;
        float r11 = sx * sy * sz + cx * cz;
        float r12 = sx * cy;

        float r20 = cx * sy * cz + sx * sz;
        float r21 = cx * sy * sz - sx * cz;
        float r22 = cx * cy;

        // Transform (transpose * vector)
        x = r00 * inX + r01 * inY + r02 * inZ;
        y = r10 * inX + r11 * inY + r12 * inZ;
        z = r20 * inX + r21 * inY + r22 * inZ;
    }
};

} // namespace Dot
