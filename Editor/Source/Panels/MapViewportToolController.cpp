#include "MapViewportToolController.h"

#include "ViewportPanel.h"
#include "ViewportSelectionUtils.h"

#include "../Map/MapDocument.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Dot
{

namespace
{

bool ClosestRayParameterToAxis(const Ray& ray, const Vec3& axisOrigin, const Vec3& axisDirection, float& outAxisT,
                               float* outDistance = nullptr)
{
    const Vec3 dir = axisDirection.Normalized();
    if (dir.LengthSquared() <= 1e-8f)
        return false;

    const Vec3 diff = ray.origin - axisOrigin;
    const float a = Vec3::Dot(ray.direction, ray.direction);
    const float b = Vec3::Dot(ray.direction, dir);
    const float c = Vec3::Dot(dir, dir);
    const float d = Vec3::Dot(ray.direction, diff);
    const float e = Vec3::Dot(dir, diff);
    const float denom = (a * c) - (b * b);

    float rayT = 0.0f;
    float axisT = 0.0f;
    if (std::abs(denom) > 1e-6f)
    {
        rayT = ((b * e) - (c * d)) / denom;
        axisT = ((a * e) - (b * d)) / denom;
    }
    else
    {
        axisT = e / c;
    }

    if (rayT < 0.0f)
    {
        rayT = 0.0f;
        axisT = e / c;
    }

    const Vec3 pointOnRay = ray.origin + (ray.direction * rayT);
    const Vec3 pointOnAxis = axisOrigin + (dir * axisT);
    if (outDistance)
        *outDistance = Vec3::Distance(pointOnRay, pointOnAxis);
    outAxisT = axisT;
    return true;
}

} // namespace

bool MapViewportToolController::HandleInput(ViewportPanel& panel, const Ray& ray, bool canInteract, bool showSelectionGizmo,
                                            ImGuiIO& io)
{
    panel.HandleMapKeyboardShortcuts();
    const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if ((enterPressed || escapePressed) && (panel.m_MapClipDragging || panel.m_MapExtrudeDragging))
    {
        if (panel.m_MapClipDragging)
        {
            if (escapePressed)
                panel.m_MapClipPreviewOffset = panel.m_MapClipDragStartOffset;
            panel.m_MapClipDragging = false;
        }
        if (panel.m_MapExtrudeDragging)
        {
            if (escapePressed)
                panel.m_MapExtrudePreviewDistance = panel.m_MapExtrudeDragStartDistance;
            panel.m_MapExtrudeDragging = false;
        }
        return true;
    }

    const MapSelectionMode mapSelectionMode =
        panel.m_MapDocument ? panel.m_MapDocument->GetSelectionMode() : MapSelectionMode::Brush;
    const bool mapMarqueeSupported =
        panel.m_MapDocument &&
        (mapSelectionMode == MapSelectionMode::Brush || mapSelectionMode == MapSelectionMode::Face ||
         mapSelectionMode == MapSelectionMode::Edge || mapSelectionMode == MapSelectionMode::Vertex) &&
        !panel.m_MapClipToolActive && !panel.m_MapExtrudeToolActive && !panel.m_MapHollowPreviewActive;

    std::vector<Vec3> clipPreviewPolygon;
    Vec3 clipPreviewNormal = Vec3::Zero();
    Vec3 clipPreviewCenter = Vec3::Zero();
    Vec3 clipHandleOrigin = Vec3::Zero();
    Vec3 clipHandleNormal = Vec3::Zero();
    bool hasClipHandle = false;
    const MapBrush* selectedClipBrush = (panel.m_MapClipToolActive && panel.m_MapDocument)
                                            ? panel.m_MapDocument->GetSelectedBrush()
                                            : nullptr;
    const MapFace* selectedClipFace =
        (panel.m_MapClipToolActive && panel.m_MapDocument) ? panel.m_MapDocument->GetSelectedFace() : nullptr;
    const bool hasClipPreview =
        panel.m_MapClipToolActive && panel.m_MapDocument &&
        panel.m_MapDocument->BuildSelectedBrushClipPreview(panel.m_MapClipPreviewOffset, panel.m_MapClipPreviewFlipPlane,
                                                           clipPreviewPolygon, clipPreviewNormal);
    if (hasClipPreview)
    {
        for (const Vec3& point : clipPreviewPolygon)
            clipPreviewCenter += point;
        clipPreviewCenter /= static_cast<float>(clipPreviewPolygon.size());
        clipHandleOrigin = clipPreviewCenter;
        clipHandleNormal = clipPreviewNormal;
        hasClipHandle = true;
    }
    else if (selectedClipBrush && selectedClipFace && !selectedClipFace->vertexIndices.empty())
    {
        for (uint32 vertexIndex : selectedClipFace->vertexIndices)
            clipPreviewCenter += selectedClipBrush->vertices[vertexIndex];
        clipPreviewCenter /= static_cast<float>(selectedClipFace->vertexIndices.size());
        const Vec3 baseNormal = ComputeMapFaceNormal(*selectedClipBrush, *selectedClipFace);
        clipHandleOrigin = clipPreviewCenter + (baseNormal * panel.m_MapClipPreviewOffset);
        clipHandleNormal = panel.m_MapClipPreviewFlipPlane ? (baseNormal * -1.0f) : baseNormal;
        hasClipHandle = clipHandleNormal.LengthSquared() > 1e-8f;
    }

    std::vector<Vec3> extrudePreviewPolygon;
    Vec3 extrudePreviewNormal = Vec3::Zero();
    Vec3 extrudePreviewCenter = Vec3::Zero();
    Vec3 extrudeHandleOrigin = Vec3::Zero();
    Vec3 extrudeHandleNormal = Vec3::Zero();
    bool hasExtrudeHandle = false;
    const MapBrush* selectedExtrudeBrush = (panel.m_MapExtrudeToolActive && panel.m_MapDocument)
                                               ? panel.m_MapDocument->GetSelectedBrush()
                                               : nullptr;
    const MapFace* selectedExtrudeFace =
        (panel.m_MapExtrudeToolActive && panel.m_MapDocument) ? panel.m_MapDocument->GetSelectedFace() : nullptr;
    const bool hasExtrudePreview =
        panel.m_MapExtrudeToolActive && panel.m_MapDocument &&
        panel.m_MapDocument->BuildSelectedFaceExtrudePreview(panel.m_MapExtrudePreviewDistance, extrudePreviewPolygon,
                                                             extrudePreviewNormal);
    if (hasExtrudePreview)
    {
        for (const Vec3& point : extrudePreviewPolygon)
            extrudePreviewCenter += point;
        extrudePreviewCenter /= static_cast<float>(extrudePreviewPolygon.size());
        extrudeHandleOrigin = extrudePreviewCenter;
        extrudeHandleNormal = extrudePreviewNormal;
        hasExtrudeHandle = true;
    }
    else if (selectedExtrudeBrush && selectedExtrudeFace && !selectedExtrudeFace->vertexIndices.empty())
    {
        for (uint32 vertexIndex : selectedExtrudeFace->vertexIndices)
            extrudePreviewCenter += selectedExtrudeBrush->vertices[vertexIndex];
        extrudePreviewCenter /= static_cast<float>(selectedExtrudeFace->vertexIndices.size());
        const Vec3 baseNormal = ComputeMapFaceNormal(*selectedExtrudeBrush, *selectedExtrudeFace);
        extrudeHandleOrigin = extrudePreviewCenter + (baseNormal * panel.m_MapExtrudePreviewDistance);
        extrudeHandleNormal = baseNormal;
        hasExtrudeHandle = extrudeHandleNormal.LengthSquared() > 1e-8f;
    }

    panel.m_MapClipPreviewHovered = false;
    if (hasClipHandle)
    {
        float axisT = 0.0f;
        float axisDistance = 0.0f;
        if (ClosestRayParameterToAxis(ray, clipHandleOrigin, clipHandleNormal, axisT, &axisDistance))
        {
            const float cameraDistance = Vec3::Distance(clipHandleOrigin, ray.origin);
            const float hitThreshold = std::max(0.12f, cameraDistance * 0.025f);
            panel.m_MapClipPreviewHovered = axisDistance <= hitThreshold && axisT >= -0.15f && axisT <= 0.9f;
        }
    }

    panel.m_MapExtrudePreviewHovered = false;
    if (hasExtrudeHandle)
    {
        float axisT = 0.0f;
        float axisDistance = 0.0f;
        if (ClosestRayParameterToAxis(ray, extrudeHandleOrigin, extrudeHandleNormal, axisT, &axisDistance))
        {
            const float cameraDistance = Vec3::Distance(extrudeHandleOrigin, ray.origin);
            const float hitThreshold = std::max(0.12f, cameraDistance * 0.025f);
            panel.m_MapExtrudePreviewHovered = axisDistance <= hitThreshold && axisT >= -0.15f && axisT <= 0.9f;
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canInteract)
    {
        if (hasClipHandle)
        {
            float axisT = 0.0f;
            float axisDistance = 0.0f;
            if (ClosestRayParameterToAxis(ray, clipHandleOrigin, clipHandleNormal, axisT, &axisDistance))
            {
                const float cameraDistance = Vec3::Distance(clipHandleOrigin, ray.origin);
                const float hitThreshold = std::max(0.12f, cameraDistance * 0.025f);
                if (axisDistance <= hitThreshold && axisT >= -0.15f && axisT <= 0.9f)
                {
                    panel.m_MapClipDragging = true;
                    panel.m_MapClipPreviewHovered = true;
                    panel.m_MapClipDragStartOffset = panel.m_MapClipPreviewOffset;
                    panel.m_MapClipDragStartAxisT = axisT;
                    panel.m_MapClipDragAxisOrigin = clipHandleOrigin;
                    panel.m_MapClipDragAxisDirection = clipHandleNormal;
                    return true;
                }
            }
        }

        if (hasExtrudeHandle)
        {
            float axisT = 0.0f;
            float axisDistance = 0.0f;
            if (ClosestRayParameterToAxis(ray, extrudeHandleOrigin, extrudeHandleNormal, axisT, &axisDistance))
            {
                const float cameraDistance = Vec3::Distance(extrudeHandleOrigin, ray.origin);
                const float hitThreshold = std::max(0.12f, cameraDistance * 0.025f);
                if (axisDistance <= hitThreshold && axisT >= -0.15f && axisT <= 0.9f)
                {
                    panel.m_MapExtrudeDragging = true;
                    panel.m_MapExtrudePreviewHovered = true;
                    panel.m_MapExtrudeDragStartDistance = panel.m_MapExtrudePreviewDistance;
                    panel.m_MapExtrudeDragStartAxisT = axisT;
                    panel.m_MapExtrudeDragAxisOrigin = extrudeHandleOrigin;
                    panel.m_MapExtrudeDragAxisDirection = extrudeHandleNormal;
                    return true;
                }
            }
        }

        ViewportPanel::SelectionInteraction selectionInteraction = ViewportPanel::SelectionInteraction::Replace;
        if (io.KeyCtrl)
            selectionInteraction = ViewportPanel::SelectionInteraction::Toggle;
        else if (io.KeyShift)
            selectionInteraction = ViewportPanel::SelectionInteraction::Add;

        const bool wantsMultiSelectionModifier = selectionInteraction != ViewportPanel::SelectionInteraction::Replace;
        if (!wantsMultiSelectionModifier && showSelectionGizmo && panel.BeginMapGizmoDrag(ray))
            return true;

        if (mapMarqueeSupported)
        {
            panel.m_MapMarqueePending = true;
            panel.m_MapMarqueeActive = false;
            panel.m_MapMarqueeInteraction = selectionInteraction;
            panel.m_MapMarqueeStart = io.MousePos;
            panel.m_MapMarqueeEnd = io.MousePos;
            return true;
        }

        panel.TrySelectMapElement(ray, selectionInteraction);
        return true;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && panel.m_MapMarqueePending)
    {
        panel.m_MapMarqueeEnd = io.MousePos;
        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (!panel.m_MapMarqueeActive && (std::abs(dragDelta.x) >= 6.0f || std::abs(dragDelta.y) >= 6.0f))
            panel.m_MapMarqueeActive = true;
        return true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && panel.m_MapMarqueePending)
    {
        const bool marqueeActive = panel.m_MapMarqueeActive;
        const auto interaction = panel.m_MapMarqueeInteraction;
        const ImVec2 marqueeStart = panel.m_MapMarqueeStart;
        const ImVec2 marqueeEnd = panel.m_MapMarqueeEnd;
        panel.m_MapMarqueePending = false;
        panel.m_MapMarqueeActive = false;

        if (marqueeActive)
        {
            const std::vector<MapSelection> selections =
                Dot::CollectMapSelectionsInScreenRect(panel.m_Camera, *panel.m_MapDocument, marqueeStart, marqueeEnd,
                                                      panel.m_ViewportX, panel.m_ViewportY, panel.m_ViewportWidth,
                                                      panel.m_ViewportHeight);
            if (interaction == ViewportPanel::SelectionInteraction::Replace)
            {
                if (!selections.empty())
                    panel.m_MapDocument->SetSelections(selections);
                else
                    panel.m_MapDocument->ClearSelection();
            }
            else
            {
                for (const MapSelection& selection : selections)
                {
                    if (interaction == ViewportPanel::SelectionInteraction::Add)
                        panel.m_MapDocument->AddSelection(selection);
                    else if (interaction == ViewportPanel::SelectionInteraction::Toggle)
                        panel.m_MapDocument->ToggleSelection(selection);
                }
            }
            panel.UpdateMapGizmoPlacement();
        }
        else
        {
            panel.TrySelectMapElement(ray, interaction);
        }
        return true;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && panel.m_MapClipDragging)
    {
        float axisT = 0.0f;
        if (ClosestRayParameterToAxis(ray, panel.m_MapClipDragAxisOrigin, panel.m_MapClipDragAxisDirection, axisT))
            panel.m_MapClipPreviewOffset = panel.m_MapClipDragStartOffset + (axisT - panel.m_MapClipDragStartAxisT);
        return true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && panel.m_MapClipDragging)
    {
        panel.m_MapClipDragging = false;
        return true;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && panel.m_MapExtrudeDragging)
    {
        float axisT = 0.0f;
        if (ClosestRayParameterToAxis(ray, panel.m_MapExtrudeDragAxisOrigin, panel.m_MapExtrudeDragAxisDirection, axisT))
        {
            panel.m_MapExtrudePreviewDistance =
                panel.m_MapExtrudeDragStartDistance + (axisT - panel.m_MapExtrudeDragStartAxisT);
        }
        return true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && panel.m_MapExtrudeDragging)
    {
        panel.m_MapExtrudeDragging = false;
        return true;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && panel.m_MouseDragging)
    {
        panel.UpdateMapGizmoDrag(ray);
        return true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && panel.m_MouseDragging)
    {
        panel.EndMapGizmoDrag();
        return true;
    }

    if (showSelectionGizmo && !panel.m_MouseDragging && !panel.m_MapClipDragging && !panel.m_MapExtrudeDragging &&
        panel.HasMapSelection() && !panel.m_MapClipToolActive && !panel.m_MapExtrudeToolActive &&
        !panel.m_MapHollowPreviewActive)
    {
        panel.UpdateMapGizmoPlacement();
        const GizmoHit hit = panel.GetMapGizmo()->HitTest(ray, panel.m_Camera);
        panel.GetMapGizmo()->SetHoveredAxis(hit.axis);
    }

    return true;
}

} // namespace Dot
