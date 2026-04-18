// =============================================================================
// Dot Engine - Viewport Overlay Renderer
// =============================================================================

#pragma once

#include "Core/ECS/World.h"
#include "Core/Navigation/NavigationSystem.h"
#include "Core/Scene/Components.h"

#include "../Gizmos/Gizmo.h"
#include "../Map/MapDocument.h"
#include "../Rendering/Camera.h"
#include "../Rendering/GizmoRenderer.h"
#include "../Rendering/GridRenderer.h"

#include <imgui.h>
#include <vector>

namespace Dot
{

class RHISwapChain;

struct ViewportOverlayPassContext
{
    GizmoRenderer* gizmoRenderer = nullptr;
    GizmoRenderer* overlayGizmoRenderer = nullptr;
    GridRenderer* gridRenderer = nullptr;
    World* world = nullptr;
    const Camera* camera = nullptr;
    RHISwapChain* swapChain = nullptr;

    bool playMode = false;
    bool showSelectionGizmo = false;
    bool showNavMeshGizmo = false;
    bool mapEditingEnabled = false;
    bool mapClipToolActive = false;
    bool mapClipPreviewFlipPlane = false;
    bool mapClipPreviewHovered = false;
    bool mapClipDragging = false;
    bool mapExtrudeToolActive = false;
    bool mapExtrudePreviewHovered = false;
    bool mapExtrudeDragging = false;
    bool mapHollowPreviewActive = false;
    bool entityMarqueePending = false;
    bool entityMarqueeActive = false;
    bool mapMarqueePending = false;
    bool mapMarqueeActive = false;
    bool mapSelectionValid = false;
    bool useLocalTransformSpace = false;

    float mapClipPreviewOffset = 0.0f;
    float mapExtrudePreviewDistance = 0.0f;
    float mapHollowPreviewThickness = 0.0f;
    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;

    ImVec2 entityMarqueeStart = ImVec2(0.0f, 0.0f);
    ImVec2 entityMarqueeEnd = ImVec2(0.0f, 0.0f);
    ImVec2 mapMarqueeStart = ImVec2(0.0f, 0.0f);
    ImVec2 mapMarqueeEnd = ImVec2(0.0f, 0.0f);

    Entity selectedEntity = kNullEntity;
    const std::vector<Entity>* selectedEntities = nullptr;
    Gizmo* activeGizmo = nullptr;
    Gizmo* mapGizmo = nullptr;
    Vec3 mapGizmoPivot = Vec3::Zero();
    MapDocument* mapDocument = nullptr;
    const std::vector<NavigationDebugLine>* navMeshDebugLines = nullptr;
};

void RenderViewportOverlayPass(const ViewportOverlayPassContext& context);

} // namespace Dot
