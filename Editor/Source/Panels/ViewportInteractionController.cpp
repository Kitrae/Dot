#include "ViewportInteractionController.h"

#include "MapViewportToolController.h"
#include "ViewportPanel.h"
#include "ViewportSelectionUtils.h"

#include "../Commands/CommandRegistry.h"
#include "../Commands/CreateEntityCommands.h"
#include "../Settings/EditorSettings.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Dot
{

namespace
{

float SnapScalar(float value, float step)
{
    if (step <= 0.0f)
        return value;
    return std::round(value / step) * step;
}

Vec3 SnapVector(const Vec3& value, float step)
{
    return Vec3(SnapScalar(value.x, step), SnapScalar(value.y, step), SnapScalar(value.z, step));
}

} // namespace

void ViewportInteractionController::HandleLayoutInput(ViewportPanel& panel, const Ray& ray, bool canInteract,
                                                      bool showSelectionGizmo, ImGuiIO& io)
{
    if (!io.WantCaptureKeyboard && !panel.m_MouseDragging && !panel.m_IsOrbiting && !panel.m_IsPanning)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W))
            panel.m_GizmoMode = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E))
            panel.m_GizmoMode = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            panel.m_GizmoMode = GizmoMode::Scale;
    }

    const std::vector<Entity> transformSelection = panel.GetValidSelectedEntities();
    const bool hasTransformSelection = !transformSelection.empty();

    Gizmo* activeGizmo = panel.GetActiveGizmo();
    if (showSelectionGizmo && hasTransformSelection)
    {
        panel.UpdateEntityGizmoPlacement();
        const bool isLocal =
            (panel.m_TransformSpace == ViewportPanel::TransformSpace::Local) && transformSelection.size() == 1;
        activeGizmo->SetLocalSpace(isLocal);
        if (isLocal)
        {
            auto* transform = panel.m_World->GetComponent<TransformComponent>(transformSelection.front());
            activeGizmo->SetRotation(transform->rotation.x, transform->rotation.y, transform->rotation.z);
        }
        else
        {
            activeGizmo->SetRotation(0.0f, 0.0f, 0.0f);
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canInteract)
    {
        ViewportPanel::SelectionInteraction selectionInteraction = ViewportPanel::SelectionInteraction::Replace;
        if (io.KeyCtrl)
            selectionInteraction = ViewportPanel::SelectionInteraction::Toggle;
        else if (io.KeyShift)
            selectionInteraction = ViewportPanel::SelectionInteraction::Add;

        const bool wantsMultiSelectionModifier = selectionInteraction != ViewportPanel::SelectionInteraction::Replace;
        if (showSelectionGizmo && hasTransformSelection)
        {
            const GizmoHit hit = activeGizmo->HitTest(ray, panel.m_Camera);
            if (!wantsMultiSelectionModifier && hit.hit)
            {
                activeGizmo->BeginDrag(hit.axis, ray, panel.m_Camera);
                panel.m_MouseDragging = true;

                panel.m_DragStartTransforms.clear();
                panel.m_DragStartTransforms.reserve(transformSelection.size());
                for (Entity entity : transformSelection)
                {
                    auto* transform = panel.m_World->GetComponent<TransformComponent>(entity);
                    if (!transform)
                        continue;

                    panel.m_DragStartTransforms.push_back(
                        {entity, transform->position, transform->rotation, transform->scale});
                }
                panel.m_DragStartPivot = panel.ComputeSelectionPivot();
                if (transformSelection.size() == 1 && !panel.m_DragStartTransforms.empty())
                {
                    panel.m_DragStartPosition = panel.m_DragStartTransforms.front().position;
                    panel.m_DragStartRotation = panel.m_DragStartTransforms.front().rotation;
                    panel.m_DragStartScale = panel.m_DragStartTransforms.front().scale;
                }
                panel.m_EntityDragAccumulatedDelta = Vec3::Zero();
                panel.m_EntityDragAppliedDelta = Vec3::Zero();
                return;
            }
        }

        panel.m_EntityMarqueePending = true;
        panel.m_EntityMarqueeActive = false;
        panel.m_EntityMarqueeInteraction = selectionInteraction;
        panel.m_EntityMarqueeStart = io.MousePos;
        panel.m_EntityMarqueeEnd = io.MousePos;
        return;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && panel.m_EntityMarqueePending)
    {
        panel.m_EntityMarqueeEnd = io.MousePos;
        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (!panel.m_EntityMarqueeActive && (std::abs(dragDelta.x) >= 6.0f || std::abs(dragDelta.y) >= 6.0f))
            panel.m_EntityMarqueeActive = true;
        return;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && panel.m_EntityMarqueePending)
    {
        const bool marqueeActive = panel.m_EntityMarqueeActive;
        const auto interaction = panel.m_EntityMarqueeInteraction;
        const ImVec2 marqueeStart = panel.m_EntityMarqueeStart;
        const ImVec2 marqueeEnd = panel.m_EntityMarqueeEnd;
        panel.m_EntityMarqueePending = false;
        panel.m_EntityMarqueeActive = false;

        if (marqueeActive)
        {
            const std::vector<Entity> entities =
                Dot::CollectEntitiesInScreenRect(panel.m_Camera, *panel.m_World, marqueeStart, marqueeEnd,
                                                 panel.m_ViewportX, panel.m_ViewportY, panel.m_ViewportWidth,
                                                 panel.m_ViewportHeight);
            if (interaction == ViewportPanel::SelectionInteraction::Replace || !entities.empty())
                panel.ApplyEntitySelectionInteraction(entities, interaction);
        }
        else
        {
            panel.TrySelectEntity(ray, interaction);
        }
        return;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && panel.m_MouseDragging)
    {
        if (!showSelectionGizmo)
        {
            panel.m_MouseDragging = false;
            return;
        }

        float deltaX = 0.0f;
        float deltaY = 0.0f;
        float deltaZ = 0.0f;
        if (activeGizmo->UpdateDrag(ray, panel.m_Camera, deltaX, deltaY, deltaZ))
        {
            const std::vector<Entity> dragSelection = panel.GetValidSelectedEntities();
            if (!dragSelection.empty())
            {
                const auto& settings = EditorSettings::Get();
                const bool isMultiSelection = dragSelection.size() > 1;
                const bool isLocal =
                    (panel.m_TransformSpace == ViewportPanel::TransformSpace::Local) && !isMultiSelection;
                const Vec3 pivot = panel.ComputeSelectionPivot();
                panel.m_EntityDragAccumulatedDelta += Vec3(deltaX, deltaY, deltaZ);

                if (panel.m_GizmoMode == GizmoMode::Translate)
                {
                    const Vec3 targetDelta = settings.layoutTranslationSnapEnabled
                                                 ? SnapVector(panel.m_EntityDragAccumulatedDelta,
                                                              settings.layoutTranslationSnapStep)
                                                 : panel.m_EntityDragAccumulatedDelta;
                    const Vec3 stepDelta = targetDelta - panel.m_EntityDragAppliedDelta;
                    if (stepDelta.ApproxEqual(Vec3::Zero(), 0.0001f))
                        return;

                    Vec3 appliedDelta = stepDelta;
                    if (isLocal)
                        activeGizmo->TransformByRotation(appliedDelta.x, appliedDelta.y, appliedDelta.z);

                    for (Entity entity : dragSelection)
                    {
                        auto* transform = panel.m_World->GetComponent<TransformComponent>(entity);
                        if (!transform)
                            continue;
                        transform->position.x += appliedDelta.x;
                        transform->position.y += appliedDelta.y;
                        transform->position.z += appliedDelta.z;
                    }
                    panel.m_EntityDragAppliedDelta = targetDelta;
                }
                else if (panel.m_GizmoMode == GizmoMode::Rotate)
                {
                    const Vec3 targetDelta = settings.layoutRotationSnapEnabled
                                                 ? SnapVector(panel.m_EntityDragAccumulatedDelta,
                                                              settings.layoutRotationSnapStep)
                                                 : panel.m_EntityDragAccumulatedDelta;
                    const Vec3 stepDelta = targetDelta - panel.m_EntityDragAppliedDelta;
                    if (stepDelta.ApproxEqual(Vec3::Zero(), 0.0001f))
                        return;

                    const float deg2rad = 3.14159265f / 180.0f;
                    const Mat4 rotationMatrix = Mat4::RotationZ(stepDelta.z * deg2rad) *
                                                Mat4::RotationY(stepDelta.y * deg2rad) *
                                                Mat4::RotationX(stepDelta.x * deg2rad);

                    for (Entity entity : dragSelection)
                    {
                        auto* transform = panel.m_World->GetComponent<TransformComponent>(entity);
                        if (!transform)
                            continue;

                        Vec3 relative = transform->position - pivot;
                        relative = rotationMatrix.TransformDirection(relative);
                        transform->position = pivot + relative;
                        transform->rotation.x += stepDelta.x;
                        transform->rotation.y += stepDelta.y;
                        transform->rotation.z += stepDelta.z;
                    }
                    panel.m_EntityDragAppliedDelta = targetDelta;
                }
                else if (panel.m_GizmoMode == GizmoMode::Scale)
                {
                    const Vec3 targetDelta = settings.layoutScaleSnapEnabled
                                                 ? SnapVector(panel.m_EntityDragAccumulatedDelta,
                                                              settings.layoutScaleSnapStep)
                                                 : panel.m_EntityDragAccumulatedDelta;
                    const Vec3 stepDelta = targetDelta - panel.m_EntityDragAppliedDelta;
                    if (stepDelta.ApproxEqual(Vec3::Zero(), 0.0001f))
                        return;

                    const Vec3 scaleFactor(std::max(0.1f, 1.0f + stepDelta.x), std::max(0.1f, 1.0f + stepDelta.y),
                                           std::max(0.1f, 1.0f + stepDelta.z));

                    for (Entity entity : dragSelection)
                    {
                        auto* transform = panel.m_World->GetComponent<TransformComponent>(entity);
                        if (!transform)
                            continue;

                        Vec3 relative = transform->position - pivot;
                        relative = relative * scaleFactor;
                        transform->position = pivot + relative;
                        transform->scale.x = std::max(0.1f, transform->scale.x + stepDelta.x);
                        transform->scale.y = std::max(0.1f, transform->scale.y + stepDelta.y);
                        transform->scale.z = std::max(0.1f, transform->scale.z + stepDelta.z);
                    }
                    panel.m_EntityDragAppliedDelta = targetDelta;
                }

                panel.UpdateEntityGizmoPlacement();
            }
        }
        return;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && panel.m_MouseDragging)
    {
        if (showSelectionGizmo)
            activeGizmo->EndDrag();
        panel.m_MouseDragging = false;
        panel.m_EntityDragAccumulatedDelta = Vec3::Zero();
        panel.m_EntityDragAppliedDelta = Vec3::Zero();

        const std::vector<Entity> endSelection = panel.GetValidSelectedEntities();
        if (!endSelection.empty())
        {
            std::vector<EntityTransformState> beforeStates;
            std::vector<EntityTransformState> afterStates;
            beforeStates.reserve(panel.m_DragStartTransforms.size());
            afterStates.reserve(panel.m_DragStartTransforms.size());

            for (const ViewportPanel::TransformSelectionState& startState : panel.m_DragStartTransforms)
            {
                auto* transform = panel.m_World->GetComponent<TransformComponent>(startState.entity);
                if (!transform)
                    continue;

                beforeStates.push_back({startState.entity, startState.position, startState.rotation, startState.scale});
                afterStates.push_back({startState.entity, transform->position, transform->rotation, transform->scale});
            }

            const bool changed = std::any_of(
                beforeStates.begin(), beforeStates.end(),
                [&](const EntityTransformState& beforeState)
                {
                    auto it = std::find_if(afterStates.begin(), afterStates.end(),
                                           [&](const EntityTransformState& afterState)
                                           { return afterState.entity == beforeState.entity; });
                    return it != afterStates.end() &&
                           (beforeState.position != it->position || beforeState.rotation != it->rotation ||
                            beforeState.scale != it->scale);
                });

            if (changed)
            {
                if (beforeStates.size() == 1 && afterStates.size() == 1)
                {
                    auto cmd = std::make_unique<TransformCommand>(panel.m_World.get(), beforeStates.front().entity,
                                                                  beforeStates.front().position,
                                                                  beforeStates.front().rotation,
                                                                  beforeStates.front().scale,
                                                                  afterStates.front().position,
                                                                  afterStates.front().rotation,
                                                                  afterStates.front().scale);
                    CommandRegistry::Get().PushCommand(std::move(cmd));
                }
                else
                {
                    CommandRegistry::Get().PushCommand(
                        std::make_unique<MultiTransformCommand>(panel.m_World.get(), beforeStates, afterStates));
                }
            }
        }
        return;
    }

    if (showSelectionGizmo && !panel.m_MouseDragging && hasTransformSelection)
    {
        const GizmoHit hit = activeGizmo->HitTest(ray, panel.m_Camera);
        activeGizmo->SetHoveredAxis(hit.axis);
    }
}

void ViewportInteractionController::HandleInput(ViewportPanel& panel)
{
    const int frameCount = ImGui::GetFrameCount();
    if (panel.m_LastHandledMouseInputFrame == frameCount)
        return;
    panel.m_LastHandledMouseInputFrame = frameCount;

    ImGuiIO& io = ImGui::GetIO();
    const bool showSelectionGizmo = EditorSettings::Get().showSelectionGizmo;

    const float mouseX = io.MousePos.x;
    const float mouseY = io.MousePos.y;
    const float windowWidth = io.DisplaySize.x;
    const float windowHeight = io.DisplaySize.y;
    const Ray ray = RayPicker::ScreenToWorldRay(mouseX, mouseY, windowWidth, windowHeight, panel.m_Camera);

    const bool canInteract = panel.m_ViewportHovered || !io.WantCaptureMouse;

    if (canInteract)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            panel.m_IsOrbiting = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            panel.m_IsPanning = true;
    }

    if (panel.m_IsOrbiting)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            panel.m_Camera.Orbit(-delta.x * 0.005f, -delta.y * 0.005f);
        }
        else
        {
            panel.m_IsOrbiting = false;
        }
    }

    if (panel.m_IsPanning)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
            panel.m_Camera.Pan(-delta.x * 0.01f, delta.y * 0.01f);
        }
        else
        {
            panel.m_IsPanning = false;
        }
    }

    if (canInteract && io.MouseWheel != 0.0f)
        panel.m_Camera.Zoom(io.MouseWheel * 0.5f);

    const std::vector<Entity> transformSelection = panel.GetValidSelectedEntities();
    const bool hasTransformSelection = !transformSelection.empty();
    if (ImGui::IsKeyPressed(ImGuiKey_F) && hasTransformSelection)
    {
        const Vec3 pivot = panel.ComputeSelectionPivot();
        panel.m_Camera.Focus(pivot.x, pivot.y, pivot.z, 5.0f);
    }

    if (panel.m_MapEditingEnabled)
    {
        if (MapViewportToolController::HandleInput(panel, ray, canInteract, showSelectionGizmo, io))
            return;
    }

    HandleLayoutInput(panel, ray, canInteract, showSelectionGizmo, io);
}

} // namespace Dot
