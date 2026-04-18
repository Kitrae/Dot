// =============================================================================
// Dot Engine - Rotate Gizmo
// =============================================================================
// Rotation gizmo with XYZ axis circles.
// =============================================================================

#pragma once

#include "Gizmo.h"

namespace Dot
{

/// Rotation gizmo - XYZ axis circles
class RotateGizmo : public Gizmo
{
public:
    RotateGizmo() = default;
    ~RotateGizmo() override = default;

    void Draw(GizmoRenderer& renderer, const Camera& camera) override;
    GizmoHit HitTest(const Ray& ray, const Camera& camera) override;
    void BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera) override;
    bool UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ) override;
    void EndDrag() override;

private:
    // Thickness of the circle hit region
    static constexpr float PICK_THICKNESS = 0.15f;

    // Number of segments for circle drawing
    static constexpr int CIRCLE_SEGMENTS = 32;

    // Store start angle for rotation delta
    float m_StartAngle = 0;
    float m_LastAngle = 0;
};

} // namespace Dot
