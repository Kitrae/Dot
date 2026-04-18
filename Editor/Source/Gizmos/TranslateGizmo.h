// =============================================================================
// Dot Engine - Translate Gizmo
// =============================================================================
// Translation gizmo with XYZ axis arrows.
// =============================================================================

#pragma once

#include "Gizmo.h"

namespace Dot
{

/// Translation gizmo - XYZ axis arrows
class TranslateGizmo : public Gizmo
{
public:
    TranslateGizmo() = default;
    ~TranslateGizmo() override = default;

    void Draw(GizmoRenderer& renderer, const Camera& camera) override;
    GizmoHit HitTest(const Ray& ray, const Camera& camera) override;
    void BeginDrag(GizmoAxis axis, const Ray& ray, const Camera& camera) override;
    bool UpdateDrag(const Ray& ray, const Camera& camera, float& deltaX, float& deltaY, float& deltaZ) override;
    void EndDrag() override;

private:
    // Size of the arrow hit region (radius around the shaft) - larger = easier to pick
    static constexpr float PICK_RADIUS = 0.25f;

    // Last world position during drag
    float m_LastDragX = 0, m_LastDragY = 0, m_LastDragZ = 0;
};

} // namespace Dot
