// =============================================================================
// Dot Engine - Scale Gizmo
// =============================================================================
// Scale gizmo with XYZ axis handles.
// =============================================================================

#pragma once

#include "Gizmo.h"

namespace Dot
{

/// Scale gizmo - XYZ axis boxes
class ScaleGizmo : public Gizmo
{
public:
    ScaleGizmo() = default;
    ~ScaleGizmo() override = default;

    void Draw(GizmoRenderer& renderer, const Camera& camera) override;
    GizmoHit HitTest(const Ray& ray, const Camera& camera) override;
    void BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera) override;
    bool UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ) override;
    void EndDrag() override;

private:
    // Size of the box handle hit region
    static constexpr float PICK_RADIUS = 0.2f;
    static constexpr float BOX_SIZE = 0.15f;

    // Last world position during drag
    float m_LastDragX = 0, m_LastDragY = 0, m_LastDragZ = 0;
};

} // namespace Dot
