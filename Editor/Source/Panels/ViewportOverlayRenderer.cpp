// =============================================================================
// Dot Engine - Viewport Overlay Renderer
// =============================================================================

#include "ViewportOverlayRenderer.h"

#include "ViewportSelectionUtils.h"

#include <algorithm>

namespace Dot
{

namespace
{

Vec3 GetWorldPosition(const TransformComponent& transform)
{
    return transform.worldMatrix.GetTranslation();
}

void DrawMarqueeBox(GizmoRenderer& renderer, const Camera& camera, float viewportX, float viewportY, float viewportWidth,
                    float viewportHeight, const ImVec2& start, const ImVec2& end)
{
    constexpr float kMarqueePlaneDepth = 0.35f;
    constexpr float kMarqueeR = 0.47f;
    constexpr float kMarqueeG = 0.76f;
    constexpr float kMarqueeB = 1.0f;

    Vec3 cornerA;
    Vec3 cornerB;
    Vec3 cornerC;
    Vec3 cornerD;
    const float minX = std::min(start.x, end.x);
    const float minY = std::min(start.y, end.y);
    const float maxX = std::max(start.x, end.x);
    const float maxY = std::max(start.y, end.y);

    if (!ScreenPointToCameraPlaneWorld(camera, minX, minY, viewportX, viewportY, viewportWidth, viewportHeight,
                                       kMarqueePlaneDepth, cornerA) ||
        !ScreenPointToCameraPlaneWorld(camera, maxX, minY, viewportX, viewportY, viewportWidth, viewportHeight,
                                       kMarqueePlaneDepth, cornerB) ||
        !ScreenPointToCameraPlaneWorld(camera, maxX, maxY, viewportX, viewportY, viewportWidth, viewportHeight,
                                       kMarqueePlaneDepth, cornerC) ||
        !ScreenPointToCameraPlaneWorld(camera, minX, maxY, viewportX, viewportY, viewportWidth, viewportHeight,
                                       kMarqueePlaneDepth, cornerD))
    {
        return;
    }

    renderer.DrawLine(cornerA.x, cornerA.y, cornerA.z, cornerB.x, cornerB.y, cornerB.z, kMarqueeR, kMarqueeG, kMarqueeB,
                      1.0f);
    renderer.DrawLine(cornerB.x, cornerB.y, cornerB.z, cornerC.x, cornerC.y, cornerC.z, kMarqueeR, kMarqueeG, kMarqueeB,
                      1.0f);
    renderer.DrawLine(cornerC.x, cornerC.y, cornerC.z, cornerD.x, cornerD.y, cornerD.z, kMarqueeR, kMarqueeG, kMarqueeB,
                      1.0f);
    renderer.DrawLine(cornerD.x, cornerD.y, cornerD.z, cornerA.x, cornerA.y, cornerA.z, kMarqueeR, kMarqueeG, kMarqueeB,
                      1.0f);
}

} // namespace

void RenderViewportOverlayPass(const ViewportOverlayPassContext& context)
{
    if (context.playMode || !context.gizmoRenderer || !context.overlayGizmoRenderer || !context.camera)
        return;

    context.gizmoRenderer->Begin();

    if (context.showNavMeshGizmo && context.navMeshDebugLines)
    {
        for (const NavigationDebugLine& debugLine : *context.navMeshDebugLines)
        {
            context.gizmoRenderer->DrawLine(debugLine.start.x, debugLine.start.y + 0.05f, debugLine.start.z, debugLine.end.x,
                                            debugLine.end.y + 0.05f, debugLine.end.z, 0.15f, 0.9f, 0.2f, 0.7f);
        }
    }

    context.gizmoRenderer->End(*context.camera, context.swapChain);

    context.overlayGizmoRenderer->Begin();

    if (!context.mapEditingEnabled && context.entityMarqueePending && context.entityMarqueeActive)
    {
        DrawMarqueeBox(*context.overlayGizmoRenderer, *context.camera, context.viewportX, context.viewportY, context.viewportWidth,
                       context.viewportHeight, context.entityMarqueeStart, context.entityMarqueeEnd);
    }

    if (context.mapEditingEnabled && context.mapMarqueePending && context.mapMarqueeActive)
    {
        DrawMarqueeBox(*context.overlayGizmoRenderer, *context.camera, context.viewportX, context.viewportY, context.viewportWidth,
                       context.viewportHeight, context.mapMarqueeStart, context.mapMarqueeEnd);
    }

    if (context.mapEditingEnabled && context.mapClipToolActive && context.mapDocument)
    {
        std::vector<Vec3> clipPreviewPolygon;
        Vec3 clipPreviewNormal = Vec3::Zero();
        const MapBrush* selectedBrush = context.mapDocument->GetSelectedBrush();
        const MapFace* selectedFace = context.mapDocument->GetSelectedFace();
        if (context.mapDocument->BuildSelectedBrushClipPreview(context.mapClipPreviewOffset, context.mapClipPreviewFlipPlane,
                                                               clipPreviewPolygon, clipPreviewNormal))
        {
            const float lineR = (context.mapClipPreviewHovered || context.mapClipDragging) ? 1.0f : 0.98f;
            const float lineG = (context.mapClipPreviewHovered || context.mapClipDragging) ? 0.65f : 0.35f;
            const float lineB = 0.22f;
            const float markerR = 1.0f;
            const float markerG = (context.mapClipPreviewHovered || context.mapClipDragging) ? 0.9f : 0.78f;
            const float markerB = 0.20f;

            for (size_t pointIndex = 0; pointIndex < clipPreviewPolygon.size(); ++pointIndex)
            {
                const Vec3& a = clipPreviewPolygon[pointIndex];
                const Vec3& b = clipPreviewPolygon[(pointIndex + 1) % clipPreviewPolygon.size()];
                context.overlayGizmoRenderer->DrawLine(a.x, a.y, a.z, b.x, b.y, b.z, lineR, lineG, lineB, 1.0f);
            }

            Vec3 centroid = Vec3::Zero();
            for (const Vec3& point : clipPreviewPolygon)
                centroid += point;
            centroid /= static_cast<float>(clipPreviewPolygon.size());

            context.overlayGizmoRenderer->DrawCircle(centroid.x, centroid.y, centroid.z, clipPreviewNormal.x, clipPreviewNormal.y,
                                                     clipPreviewNormal.z, 0.24f, lineR, lineG, lineB, 0.35f, 24);
            context.overlayGizmoRenderer->DrawArrow(centroid.x, centroid.y, centroid.z, clipPreviewNormal.x, clipPreviewNormal.y,
                                                    clipPreviewNormal.z, 0.65f, markerR, markerG, markerB, 1.0f);
            context.overlayGizmoRenderer->DrawCircle(centroid.x, centroid.y, centroid.z, clipPreviewNormal.x, clipPreviewNormal.y,
                                                     clipPreviewNormal.z, 0.18f, markerR, markerG, markerB, 0.85f, 24);
        }
        else if (selectedBrush && selectedFace && !selectedFace->vertexIndices.empty())
        {
            Vec3 faceCenter = Vec3::Zero();
            for (uint32 vertexIndex : selectedFace->vertexIndices)
                faceCenter += selectedBrush->vertices[vertexIndex];
            faceCenter /= static_cast<float>(selectedFace->vertexIndices.size());

            const Vec3 baseNormal = ComputeMapFaceNormal(*selectedBrush, *selectedFace);
            const Vec3 previewNormal = context.mapClipPreviewFlipPlane ? (baseNormal * -1.0f) : baseNormal;
            const Vec3 previewCenter = faceCenter + (baseNormal * context.mapClipPreviewOffset);

            context.overlayGizmoRenderer->DrawArrow(previewCenter.x, previewCenter.y, previewCenter.z, previewNormal.x,
                                                    previewNormal.y, previewNormal.z, 0.55f, 1.0f, 0.2f, 0.2f, 1.0f);
            context.overlayGizmoRenderer->DrawCircle(previewCenter.x, previewCenter.y, previewCenter.z, previewNormal.x,
                                                     previewNormal.y, previewNormal.z, 0.20f, 1.0f, 0.2f, 0.2f, 0.95f, 24);
            context.overlayGizmoRenderer->DrawLine(previewCenter.x - 0.12f, previewCenter.y, previewCenter.z, previewCenter.x + 0.12f,
                                                   previewCenter.y, previewCenter.z, 1.0f, 0.25f, 0.25f, 1.0f);
            context.overlayGizmoRenderer->DrawBillboardText(*context.camera, previewCenter.x, previewCenter.y + 0.22f,
                                                            previewCenter.z, "Clip out of range", 0.12f, 1.0f, 0.3f, 0.3f,
                                                            1.0f, true);
        }
    }

    if (context.mapEditingEnabled && context.mapExtrudeToolActive && context.mapDocument)
    {
        std::vector<Vec3> extrudePreviewPolygon;
        Vec3 extrudePreviewNormal = Vec3::Zero();
        const MapBrush* selectedBrush = context.mapDocument->GetSelectedBrush();
        const MapFace* selectedFace = context.mapDocument->GetSelectedFace();
        if (context.mapDocument->BuildSelectedFaceExtrudePreview(context.mapExtrudePreviewDistance, extrudePreviewPolygon,
                                                                 extrudePreviewNormal))
        {
            const float lineR = (context.mapExtrudePreviewHovered || context.mapExtrudeDragging) ? 0.35f : 0.22f;
            const float lineG = (context.mapExtrudePreviewHovered || context.mapExtrudeDragging) ? 0.95f : 0.82f;
            const float lineB = 1.0f;
            const float markerR = 0.40f;
            const float markerG = 0.95f;
            const float markerB = 1.0f;

            Vec3 capCentroid = Vec3::Zero();
            for (const Vec3& point : extrudePreviewPolygon)
                capCentroid += point;
            capCentroid /= static_cast<float>(extrudePreviewPolygon.size());

            for (size_t pointIndex = 0; pointIndex < extrudePreviewPolygon.size(); ++pointIndex)
            {
                const Vec3& a = extrudePreviewPolygon[pointIndex];
                const Vec3& b = extrudePreviewPolygon[(pointIndex + 1) % extrudePreviewPolygon.size()];
                context.overlayGizmoRenderer->DrawLine(a.x, a.y, a.z, b.x, b.y, b.z, lineR, lineG, lineB, 1.0f);
            }

            if (selectedBrush && selectedFace && selectedFace->vertexIndices.size() == extrudePreviewPolygon.size())
            {
                for (size_t i = 0; i < selectedFace->vertexIndices.size(); ++i)
                {
                    const Vec3& basePoint = selectedBrush->vertices[selectedFace->vertexIndices[i]];
                    const Vec3& capPoint = extrudePreviewPolygon[i];
                    context.overlayGizmoRenderer->DrawLine(basePoint.x, basePoint.y, basePoint.z, capPoint.x, capPoint.y,
                                                           capPoint.z, 0.28f, 0.70f, 0.88f, 0.9f);
                }
            }

            context.overlayGizmoRenderer->DrawArrow(capCentroid.x, capCentroid.y, capCentroid.z, extrudePreviewNormal.x,
                                                    extrudePreviewNormal.y, extrudePreviewNormal.z, 0.70f, markerR, markerG,
                                                    markerB, 1.0f);
            context.overlayGizmoRenderer->DrawCircle(capCentroid.x, capCentroid.y, capCentroid.z, extrudePreviewNormal.x,
                                                     extrudePreviewNormal.y, extrudePreviewNormal.z, 0.22f, markerR, markerG,
                                                     markerB, 0.90f, 24);
        }
        else if (selectedBrush && selectedFace && !selectedFace->vertexIndices.empty())
        {
            Vec3 faceCenter = Vec3::Zero();
            for (uint32 vertexIndex : selectedFace->vertexIndices)
                faceCenter += selectedBrush->vertices[vertexIndex];
            faceCenter /= static_cast<float>(selectedFace->vertexIndices.size());

            const Vec3 faceNormal = ComputeMapFaceNormal(*selectedBrush, *selectedFace);
            const Vec3 previewCenter = faceCenter + (faceNormal * context.mapExtrudePreviewDistance);

            context.overlayGizmoRenderer->DrawArrow(previewCenter.x, previewCenter.y, previewCenter.z, faceNormal.x, faceNormal.y,
                                                    faceNormal.z, 0.55f, 1.0f, 0.2f, 0.2f, 1.0f);
            context.overlayGizmoRenderer->DrawCircle(previewCenter.x, previewCenter.y, previewCenter.z, faceNormal.x, faceNormal.y,
                                                     faceNormal.z, 0.20f, 1.0f, 0.2f, 0.2f, 0.95f, 24);
            context.overlayGizmoRenderer->DrawBillboardText(*context.camera, previewCenter.x, previewCenter.y + 0.22f,
                                                            previewCenter.z, "Extrude invalid", 0.12f, 1.0f, 0.3f, 0.3f,
                                                            1.0f, true);
        }
    }

    if (context.mapEditingEnabled && context.mapHollowPreviewActive && context.mapDocument)
    {
        const MapBrush* selectedBrush = context.mapDocument->GetSelectedBrush();
        if (selectedBrush)
        {
            std::vector<MapBrush> hollowPreviewBrushes;
            if (context.mapDocument->BuildSelectedBrushHollowPreview(context.mapHollowPreviewThickness, hollowPreviewBrushes))
            {
                for (size_t brushIndex = 0; brushIndex < hollowPreviewBrushes.size(); ++brushIndex)
                {
                    const MapBrush& previewBrush = hollowPreviewBrushes[brushIndex];
                    const float tint = static_cast<float>(brushIndex % 3) * 0.08f;
                    const float lineR = 0.92f - tint;
                    const float lineG = 0.70f + tint;
                    const float lineB = 0.24f + (tint * 0.5f);

                    for (const MapFace& face : previewBrush.faces)
                    {
                        if (face.vertexIndices.size() < 2)
                            continue;

                        for (size_t vertexIndex = 0; vertexIndex < face.vertexIndices.size(); ++vertexIndex)
                        {
                            const Vec3& a = previewBrush.vertices[face.vertexIndices[vertexIndex]];
                            const Vec3& b = previewBrush.vertices[face.vertexIndices[(vertexIndex + 1) % face.vertexIndices.size()]];
                            context.overlayGizmoRenderer->DrawLine(a.x, a.y, a.z, b.x, b.y, b.z, lineR, lineG, lineB, 0.9f);
                        }
                    }
                }
            }
            else
            {
                const Vec3 pivot = context.mapDocument->GetSelectionPivot();
                context.overlayGizmoRenderer->DrawBillboardText(*context.camera, pivot.x, pivot.y + 0.24f, pivot.z, "Hollow invalid",
                                                                0.12f, 1.0f, 0.35f, 0.35f, 1.0f, true);
            }
        }
    }

    if (context.showSelectionGizmo && context.mapEditingEnabled && context.mapSelectionValid && !context.mapClipToolActive &&
        !context.mapExtrudeToolActive && !context.mapHollowPreviewActive && context.mapGizmo)
    {
        context.mapGizmo->SetPosition(context.mapGizmoPivot.x, context.mapGizmoPivot.y, context.mapGizmoPivot.z);
        context.mapGizmo->SetLocalSpace(false);
        context.mapGizmo->SetRotation(0.0f, 0.0f, 0.0f);
        context.mapGizmo->Draw(*context.overlayGizmoRenderer, *context.camera);
    }
    else if (context.showSelectionGizmo && context.activeGizmo && context.world)
    {
        std::vector<Entity> validSelection;
        if (context.selectedEntities)
        {
            validSelection.reserve(context.selectedEntities->size());
            for (Entity entity : *context.selectedEntities)
            {
                if (entity.IsValid() && context.world->IsAlive(entity) &&
                    context.world->GetComponent<TransformComponent>(entity))
                {
                    validSelection.push_back(entity);
                }
            }
        }

        if (validSelection.empty() && context.selectedEntity.IsValid() && context.world->IsAlive(context.selectedEntity) &&
            context.world->GetComponent<TransformComponent>(context.selectedEntity))
        {
            validSelection.push_back(context.selectedEntity);
        }

        if (!validSelection.empty())
        {
            Vec3 pivot = Vec3::Zero();
            for (Entity entity : validSelection)
                pivot += GetWorldPosition(*context.world->GetComponent<TransformComponent>(entity));
            pivot /= static_cast<float>(validSelection.size());

            context.activeGizmo->SetPosition(pivot.x, pivot.y, pivot.z);

            const bool isLocal = context.useLocalTransformSpace && validSelection.size() == 1;
            context.activeGizmo->SetLocalSpace(isLocal);
            if (isLocal)
            {
                auto* transform = context.world->GetComponent<TransformComponent>(validSelection.front());
                context.activeGizmo->SetRotation(transform->rotation.x, transform->rotation.y, transform->rotation.z);
            }
            else
            {
                context.activeGizmo->SetRotation(0.0f, 0.0f, 0.0f);
            }

            context.activeGizmo->Draw(*context.overlayGizmoRenderer, *context.camera);
        }
    }

    context.overlayGizmoRenderer->End(*context.camera, context.swapChain);

    if (context.gridRenderer)
        context.gridRenderer->Render(*context.camera, context.swapChain);
}

} // namespace Dot
