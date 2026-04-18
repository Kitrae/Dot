// =============================================================================
// Dot Engine - Project Settings Sections
// =============================================================================

#include "ProjectSettingsSections.h"

#include "../Toolbox/ToolboxManager.h"

#include "ProjectSettingsMetadata.h"

#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/PhysicsSettings.h"

#include "EditorSettings.h"
#include "ViewSettings.h"

#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

namespace Dot::ProjectSettingsSections
{

namespace
{

void DrawEditorGridSettings(bool& settingsDirty);
void DrawEditorAxisIndicatorSettings(bool& settingsDirty);
void DrawEditorSnappingSettings(bool& settingsDirty);
void DrawGizmoGridSettings(bool& settingsDirty);
void DrawGizmoAxisIndicatorSettings(bool& settingsDirty);
void DrawGizmoViewportSettings(bool& settingsDirty);
void DrawGizmoNavigationSettings(bool& settingsDirty);
void DrawGizmoPhysicsSettings(bool& settingsDirty);
void DrawViewRenderingSettings(bool& settingsDirty);
void DrawViewRenderingDebugSettings(bool& settingsDirty);
void DrawPhysicsTimestepSettings(bool& settingsDirty);
void DrawPhysicsWorldSettings(bool& settingsDirty);
void DrawPhysicsSolverSettings(bool& settingsDirty);
void DrawCollisionLayerDefaultsSettings(bool& settingsDirty);
void DrawCollisionLayerSlotsSettings(bool& settingsDirty);
void DrawCollisionMatrixSettings(bool& settingsDirty);

void DrawSectionTitle(const char* title, const char* subtitle)
{
    ImGui::Text("%s", title);
    ImGui::Separator();
    if (subtitle && subtitle[0] != '\0')
    {
        ImGui::TextDisabled("%s", subtitle);
        ImGui::Spacing();
    }
}

void DrawSectionContent(ProjectSettingsMetadata::SectionId section, bool& settingsDirty)
{
    switch (section)
    {
        case ProjectSettingsMetadata::SectionId::EditorSnapping:
            DrawEditorSnappingSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::GizmoGrid:
            DrawGizmoGridSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::GizmoAxisIndicator:
            DrawGizmoAxisIndicatorSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::GizmoViewport:
            DrawGizmoViewportSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::GizmoNavigation:
            DrawGizmoNavigationSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::GizmoPhysics:
            DrawGizmoPhysicsSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::ViewRendering:
            DrawViewRenderingSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::ViewRenderingDebug:
            DrawViewRenderingDebugSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::PhysicsTimestep:
            DrawPhysicsTimestepSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::PhysicsWorld:
            DrawPhysicsWorldSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::PhysicsSolver:
            DrawPhysicsSolverSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::CollisionDefaults:
            DrawCollisionLayerDefaultsSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::CollisionLayerSlots:
            DrawCollisionLayerSlotsSettings(settingsDirty);
            break;
        case ProjectSettingsMetadata::SectionId::CollisionMatrix:
            DrawCollisionMatrixSettings(settingsDirty);
            break;
    }
}

void DrawEditorGridSettings(bool& settingsDirty)
{
    auto& settings = EditorSettings::Get();
    settingsDirty |= ImGui::Checkbox("Show Grid", &settings.showGrid);

    if (settings.showGrid)
    {
        settingsDirty |= ImGui::SliderFloat("Grid Size", &settings.gridSize, 5.0f, 50.0f, "%.0f");
        settingsDirty |= ImGui::SliderFloat("Grid Spacing", &settings.gridSpacing, 0.5f, 5.0f, "%.1f");

        float gridColor[4] = {settings.gridColorR, settings.gridColorG, settings.gridColorB, settings.gridColorA};
        if (ImGui::ColorEdit4("Grid Color", gridColor))
        {
            settingsDirty = true;
            settings.gridColorR = gridColor[0];
            settings.gridColorG = gridColor[1];
            settings.gridColorB = gridColor[2];
            settings.gridColorA = gridColor[3];
        }
    }
}

void DrawGizmoGridSettings(bool& settingsDirty)
{
    DrawEditorGridSettings(settingsDirty);
}

void DrawEditorAxisIndicatorSettings(bool& settingsDirty)
{
    auto& settings = EditorSettings::Get();
    settingsDirty |= ImGui::Checkbox("Show Axis Indicator", &settings.showAxisIndicator);

    if (settings.showAxisIndicator)
        settingsDirty |= ImGui::SliderFloat("Axis Length", &settings.axisLength, 0.5f, 5.0f, "%.1f");
}

void DrawGizmoAxisIndicatorSettings(bool& settingsDirty)
{
    DrawEditorAxisIndicatorSettings(settingsDirty);
}

void DrawEditorSnappingSettings(bool& settingsDirty)
{
    auto& settings = EditorSettings::Get();
    settingsDirty |= ImGui::Checkbox("Layout Translation Snap", &settings.layoutTranslationSnapEnabled);
    ImGui::SetNextItemWidth(220.0f);
    settingsDirty |=
        ImGui::DragFloat("Layout Move Step", &settings.layoutTranslationSnapStep, 0.05f, 0.125f, 16.0f, "%.3f");

    if (ImGui::Button("Use Grid Spacing"))
    {
        settings.layoutTranslationSnapStep = std::max(0.125f, settings.gridSpacing);
        settingsDirty = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Grid: %.2f", settings.gridSpacing);

    settingsDirty |= ImGui::Checkbox("Layout Rotation Snap", &settings.layoutRotationSnapEnabled);
    ImGui::SetNextItemWidth(220.0f);
    settingsDirty |=
        ImGui::DragFloat("Layout Rotate Step", &settings.layoutRotationSnapStep, 1.0f, 1.0f, 90.0f, "%.0f deg");

    settingsDirty |= ImGui::Checkbox("Layout Scale Snap", &settings.layoutScaleSnapEnabled);
    ImGui::SetNextItemWidth(220.0f);
    settingsDirty |=
        ImGui::DragFloat("Layout Scale Step", &settings.layoutScaleSnapStep, 0.01f, 0.01f, 2.0f, "%.2f");

    ImGui::Separator();
    settingsDirty |= ImGui::Checkbox("Map Grid Snap", &settings.mapTranslationSnapEnabled);
    ImGui::SetNextItemWidth(220.0f);
    settingsDirty |= ImGui::DragFloat("Map Snap Step", &settings.mapTranslationSnapStep, 0.05f, 0.125f, 16.0f, "%.3f");

    if (ImGui::Button("Use Grid Spacing##MapSnap"))
    {
        settings.mapTranslationSnapStep = std::max(0.125f, settings.gridSpacing);
        settingsDirty = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Grid: %.2f", settings.gridSpacing);
}

void DrawGizmoViewportSettings(bool& settingsDirty)
{
    auto& settings = EditorSettings::Get();

    settingsDirty |= ImGui::Checkbox("Show Selection Gizmo", &settings.showSelectionGizmo);
    settingsDirty |= ImGui::Checkbox("Show Light Gizmos", &settings.showLightGizmos);
    settingsDirty |= ImGui::Checkbox("Show Camera Frustums", &settings.showCameraFrustums);
    settingsDirty |= ImGui::Checkbox("Show Attachment Sockets", &settings.showAttachmentSockets);

    ImGui::Spacing();
    ImGui::TextDisabled("These overlays are automatically hidden while Play Mode is active.");
}

void DrawGizmoNavigationSettings(bool& settingsDirty)
{
    auto& settings = EditorSettings::Get();
    if (!ToolboxManager::Get().IsNavMeshGizmoEnabled())
    {
        ImGui::BeginDisabled();
        ImGui::Checkbox("Show NavMesh", &settings.showNavMeshGizmo);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Enable the NavMesh Gizmo module in Toolbox to use this overlay.");
        return;
    }

    settingsDirty |= ImGui::Checkbox("Show NavMesh", &settings.showNavMeshGizmo);
    ImGui::TextDisabled("Draw the generated navigation mesh as an editor-only wire overlay.");
}

void DrawViewRenderingSettings(bool& settingsDirty)
{
    auto& settings = ViewSettings::Get();
    if (!IsDebugVisModeAvailable(settings.debugVisMode))
    {
        settings.debugVisMode = SanitizeDebugVisMode(settings.debugVisMode);
        settings.SyncLegacyFromDebugVis();
        settingsDirty = true;
    }

    // Primary visualization mode selector (syncs with viewport dropdown)
    int currentMode = static_cast<int>(settings.debugVisMode);
    const int modeCount = static_cast<int>(DebugVisMode::Count);
    const char* currentName = GetDebugVisModeName(settings.debugVisMode);
    if (ImGui::BeginCombo("Visualization Mode", currentName))
    {
        for (int i = 0; i < modeCount; ++i)
        {
            DebugVisMode mode = static_cast<DebugVisMode>(i);
            if (!IsDebugVisModeAvailable(mode))
                continue;
            const bool isSelected = (i == currentMode);
            if (ImGui::Selectable(GetDebugVisModeName(mode), isSelected))
            {
                settingsDirty = true;
                settings.debugVisMode = mode;
                settings.SyncLegacyFromDebugVis();
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("Also available as a dropdown in the viewport top-left corner.");
}

void DrawViewRenderingDebugSettings(bool& settingsDirty)
{
    auto& settings = ViewSettings::Get();

    ImGui::TextDisabled("Feature toggles");
    settingsDirty |= ImGui::Checkbox("Shadows", &settings.shadowsEnabled);
    settingsDirty |= ImGui::Checkbox("Anti-Aliasing", &settings.antiAliasingEnabled);
    settingsDirty |= ImGui::Checkbox("Frustum Culling", &settings.frustumCullingEnabled);
    settingsDirty |= ImGui::Checkbox("HZB Occlusion Culling", &settings.hzbEnabled);
    settingsDirty |= ImGui::Checkbox("Forward+ Tiled Lighting", &settings.forwardPlusEnabled);
    if (ImGui::Checkbox("LOD Debug Tint", &settings.lodDebugTint))
    {
        settings.SyncDebugVisFromLegacy();
        settingsDirty = true;
    }
    settingsDirty |= ImGui::SliderFloat("LOD Aggressiveness", &settings.lodAggressiveness, 0.25f, 4.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Ambient Occlusion");
    settingsDirty |= ImGui::Checkbox("AO Enabled", &settings.ssaoEnabled);
    settingsDirty |= ImGui::SliderFloat("AO Radius", &settings.ssaoRadius, 0.1f, 2.0f, "%.2f");
    settingsDirty |= ImGui::SliderFloat("AO Intensity", &settings.ssaoIntensity, 0.1f, 2.5f, "%.2f");
    settingsDirty |= ImGui::SliderInt("AO Sample Count", &settings.ssaoSampleCount, 4, 12);
    settingsDirty |= ImGui::Checkbox("AO Half Resolution", &settings.ssaoHalfResolution);
    settingsDirty |=
        ImGui::SliderFloat("AO Blur Depth Threshold", &settings.ssaoBlurDepthThreshold, 0.25f, 12.0f, "%.2f");
    ImGui::TextDisabled("Ambient Occlusion view shows the final blurred AO buffer.");

    ImGui::Spacing();
    ImGui::TextDisabled(
        "Frustum culling uses conservative AABB tests for world draws. HZB and Forward+ are viewport-only debug toggles.");
}

void DrawPhysicsTimestepSettings(bool& settingsDirty)
{
    auto& physics = PhysicsSettings::Get();
    float hz = 1.0f / physics.fixedTimestep;
    if (ImGui::SliderFloat("Simulation Rate (Hz)", &hz, 30.0f, 480.0f, "%.0f"))
    {
        settingsDirty = true;
        physics.fixedTimestep = 1.0f / hz;
    }

    settingsDirty |= ImGui::SliderInt("Max Substeps", &physics.maxSubSteps, 1, 16);
}

void DrawPhysicsWorldSettings(bool& settingsDirty)
{
    auto& physics = PhysicsSettings::Get();
    float gravity[3] = {physics.gravity.x, physics.gravity.y, physics.gravity.z};
    if (ImGui::DragFloat3("Gravity", gravity, 0.1f, -50.0f, 50.0f, "%.2f"))
    {
        settingsDirty = true;
        physics.gravity.x = gravity[0];
        physics.gravity.y = gravity[1];
        physics.gravity.z = gravity[2];
    }

    if (ImGui::Button("Earth"))
    {
        settingsDirty = true;
        physics.gravity = Vec3(0.0f, -9.81f, 0.0f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Moon"))
    {
        settingsDirty = true;
        physics.gravity = Vec3(0.0f, -1.62f, 0.0f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Zero-G"))
    {
        settingsDirty = true;
        physics.gravity = Vec3(0.0f, 0.0f, 0.0f);
    }
}

void DrawPhysicsSolverSettings(bool& settingsDirty)
{
    auto& physics = PhysicsSettings::Get();
    settingsDirty |=
        ImGui::SliderFloat("Position Correction", &physics.positionCorrectionPercent, 0.1f, 1.0f, "%.2f");
    settingsDirty |=
        ImGui::SliderFloat("Penetration Slop", &physics.positionCorrectionSlop, 0.001f, 0.1f, "%.3f");
}

void DrawGizmoPhysicsSettings(bool& settingsDirty)
{
    auto& physics = PhysicsSettings::Get();
    settingsDirty |= ImGui::Checkbox("Show Colliders", &physics.showColliders);
    settingsDirty |= ImGui::Checkbox("Show Contact Points", &physics.showContactPoints);
    ImGui::TextDisabled("Contact points render as small orange crosses with a normal line during physics simulation.");
}

void DrawCollisionLayerDefaultsSettings(bool& settingsDirty)
{
    auto& collisionLayers = CollisionLayers::Get();
    if (ImGui::Button("Reset Collision Defaults"))
    {
        settingsDirty = true;
        collisionLayers.ResetDefaults();
    }
}

void DrawCollisionLayerSlotsSettings(bool& settingsDirty)
{
    auto& collisionLayers = CollisionLayers::Get();
    const bool showLayerSlots = ImGui::BeginChild("CollisionLayerSlots", ImVec2(0, 210), true);
    if (showLayerSlots)
    {
        if (ImGui::BeginTable("CollisionLayerNames", 2,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", static_cast<unsigned>(i));

                ImGui::TableSetColumnIndex(1);
                char nameBuffer[64] = {};
                const std::string currentName = collisionLayers.GetLayerName(i);
                strncpy(nameBuffer, currentName.c_str(), sizeof(nameBuffer) - 1);
                nameBuffer[sizeof(nameBuffer) - 1] = '\0';

                const std::string inputId = "##CollisionLayerName" + std::to_string(i);
                if (ImGui::InputText(inputId.c_str(), nameBuffer, sizeof(nameBuffer)))
                {
                    settingsDirty = true;
                    collisionLayers.SetLayerName(i, nameBuffer);
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void DrawCollisionMatrixSettings(bool& settingsDirty)
{
    auto& collisionLayers = CollisionLayers::Get();
    std::vector<uint8> activeLayers;
    activeLayers.reserve(CollisionLayers::kMaxLayers);
    for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
    {
        if (collisionLayers.IsLayerActive(i))
            activeLayers.push_back(i);
    }

    if (activeLayers.empty())
    {
        ImGui::TextDisabled("Name at least one layer to edit the matrix.");
        return;
    }

    const bool showCollisionMatrix = ImGui::BeginChild("CollisionLayerMatrix", ImVec2(0, 320), true);
    if (showCollisionMatrix)
    {
        const int columnCount = static_cast<int>(activeLayers.size()) + 1;
        if (ImGui::BeginTable("CollisionMatrixTable", columnCount,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Layer");
            for (uint8 layerIndex : activeLayers)
                ImGui::TableSetupColumn(collisionLayers.GetLayerDisplayName(layerIndex).c_str());

            ImGui::TableHeadersRow();

            for (uint8 rowLayer : activeLayers)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", collisionLayers.GetLayerDisplayName(rowLayer).c_str());

                for (int col = 0; col < static_cast<int>(activeLayers.size()); ++col)
                {
                    const uint8 colLayer = activeLayers[col];
                    ImGui::TableSetColumnIndex(col + 1);

                    bool enabled = collisionLayers.ShouldLayersCollide(rowLayer, colLayer);
                    const std::string checkboxId =
                        "##CollisionMatrix_" + std::to_string(rowLayer) + "_" + std::to_string(colLayer);
                    if (ImGui::Checkbox(checkboxId.c_str(), &enabled))
                    {
                        settingsDirty = true;
                        collisionLayers.SetLayersCollide(rowLayer, colLayer, enabled);
                    }
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

} // namespace

bool DrawCategoryWithTitle(SettingsPanel::Category category, std::string_view searchQuery, bool& settingsDirty)
{
    const std::string searchText(searchQuery);
    bool drewAnySection = false;

    for (size_t i = 0; i < ProjectSettingsMetadata::GetSectionCount(category); ++i)
    {
        const auto section = ProjectSettingsMetadata::GetSectionId(category, i);
        drewAnySection |= ProjectSettingsMetadata::MatchesSection(category, section, searchText);
    }

    if (!drewAnySection)
        return false;

    DrawSectionTitle(ProjectSettingsMetadata::GetTitle(category), ProjectSettingsMetadata::GetSubtitle(category));

    for (size_t i = 0; i < ProjectSettingsMetadata::GetSectionCount(category); ++i)
    {
        const auto section = ProjectSettingsMetadata::GetSectionId(category, i);
        if (!ProjectSettingsMetadata::MatchesSection(category, section, searchText))
            continue;

        if (ImGui::CollapsingHeader(ProjectSettingsMetadata::GetSectionLabel(section), ImGuiTreeNodeFlags_DefaultOpen))
            DrawSectionContent(section, settingsDirty);
    }

    return true;
}

} // namespace Dot::ProjectSettingsSections
