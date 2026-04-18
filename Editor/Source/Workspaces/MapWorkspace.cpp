#include "MapWorkspace.h"

#include "../Commands/CommandRegistry.h"
#include "../Commands/MapCommands.h"
#include "../Map/MapDocument.h"
#include "../Panels/AssetManagerPanel.h"
#include "../Panels/MapInspectorPanel.h"
#include "../Panels/MapOutlinerPanel.h"
#include "../Panels/SettingsPanel.h"
#include "../Panels/ViewportPanel.h"
#include "../Settings/EditorSettings.h"

#include <filesystem>
#include <algorithm>
#include <imgui.h>

namespace Dot
{

namespace
{

constexpr float kMapToolbarRailWidth = 84.0f;

template <typename Fn>
bool ApplyWorkspaceMapEdit(MapDocument& document, const char* name, Fn&& fn)
{
    const MapAsset beforeAsset = document.GetAsset();
    const std::vector<MapSelection> beforeSelections = document.GetSelections();
    const MapSelection beforeSelection = document.GetSelection();
    const std::unordered_set<uint32> beforeHiddenBrushIds = document.GetHiddenBrushIds();
    const std::unordered_set<uint32> beforeLockedBrushIds = document.GetLockedBrushIds();
    const bool beforeDirty = document.IsDirty();
    if (!fn())
        return false;

    const MapAsset afterAsset = document.GetAsset();
    const std::vector<MapSelection> afterSelections = document.GetSelections();
    const MapSelection afterSelection = document.GetSelection();
    const std::unordered_set<uint32> afterHiddenBrushIds = document.GetHiddenBrushIds();
    const std::unordered_set<uint32> afterLockedBrushIds = document.GetLockedBrushIds();
    const bool afterDirty = document.IsDirty();
    CommandRegistry::Get().PushCommand(
        std::make_unique<MapSnapshotCommand>(&document, name, beforeAsset, beforeSelections, beforeSelection,
                                             beforeHiddenBrushIds, beforeLockedBrushIds, beforeDirty, afterAsset,
                                             afterSelections, afterSelection, afterHiddenBrushIds, afterLockedBrushIds,
                                             afterDirty));
    return true;
}

const char* GetSelectionModeTitle(MapSelectionMode mode)
{
    switch (mode)
    {
    case MapSelectionMode::Brush:
        return "Brush";
    case MapSelectionMode::Face:
        return "Face";
    case MapSelectionMode::Edge:
        return "Edge";
    case MapSelectionMode::Vertex:
        return "Vertex";
    default:
        return "Unknown";
    }
}

} // namespace

MapWorkspace::MapWorkspace() : Workspace("Map", WorkspaceType::Map) {}

void MapWorkspace::SetPanels(MapOutlinerPanel* outliner, MapInspectorPanel* inspector, ViewportPanel* viewport,
                             AssetManagerPanel* assetManager)
{
    m_OutlinerPanel = outliner;
    m_InspectorPanel = inspector;
    m_ViewportPanel = viewport;
    m_AssetManagerPanel = assetManager;
}

void MapWorkspace::SetDocument(MapDocument* document)
{
    m_Document = document;
    if (m_OutlinerPanel)
        m_OutlinerPanel->SetDocument(document);
    if (m_InspectorPanel)
        m_InspectorPanel->SetDocument(document);
    if (m_ViewportPanel)
        m_ViewportPanel->SetMapDocument(document);
}

void MapWorkspace::OnActivate()
{
    if (m_ViewportPanel)
        m_ViewportPanel->SetMapEditingEnabled(true);
}

void MapWorkspace::OnDeactivate()
{
    if (m_ViewportPanel)
        m_ViewportPanel->SetMapEditingEnabled(false);
}

void MapWorkspace::OnImGui()
{
    if (m_ViewportPanel)
    {
        auto& editorSettings = EditorSettings::Get();
        if (editorSettings.mapTranslationSnapStep <= 0.0f)
            editorSettings.mapTranslationSnapStep = std::max(0.125f, editorSettings.gridSpacing);
        if (m_ActiveTool == ToolbarTool::Clip)
        {
            m_ClipOffset = m_ViewportPanel->GetMapClipPreviewOffset();
            m_ClipFlipPlane = m_ViewportPanel->GetMapClipPreviewFlipPlane();
        }
        if (m_ActiveTool == ToolbarTool::Extrude)
            m_ExtrudeDistance = m_ViewportPanel->GetMapExtrudePreviewDistance();
        m_ViewportPanel->SetMapClipToolActive(m_ActiveTool == ToolbarTool::Clip);
        m_ViewportPanel->SetMapExtrudeToolActive(m_ActiveTool == ToolbarTool::Extrude);
        m_ViewportPanel->SetMapHollowPreviewActive(m_ShowHollowBoxSettings);
        m_ViewportPanel->SetMapHollowPreviewThickness(m_HollowWallThickness);
        m_ViewportPanel->SetMapTranslationSnap(editorSettings.mapTranslationSnapEnabled, editorSettings.mapTranslationSnapStep);
        if (!m_ViewportPanel->IsMapClipDragging())
            m_ViewportPanel->SetMapClipPreview(m_ClipOffset, m_ClipFlipPlane);
        if (!m_ViewportPanel->IsMapExtrudeDragging())
            m_ViewportPanel->SetMapExtrudePreviewDistance(m_ExtrudeDistance);
    }

    DrawToolbar();
    HandleToolKeyboardShortcuts();
    DrawToolSettings();

    if (m_OutlinerPanel)
        m_OutlinerPanel->OnImGui();
    if (m_InspectorPanel)
        m_InspectorPanel->OnImGui();
    if (m_AssetManagerPanel)
        m_AssetManagerPanel->OnImGui();
    SettingsPanel::OnImGui();
    if (m_ViewportPanel)
        m_ViewportPanel->OnImGui();
}

void MapWorkspace::DrawToolbar()
{
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;
    ImGui::SetNextWindowPos(ImVec2(workPos.x + 8.0f, workPos.y + 56.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kMapToolbarRailWidth, std::max(320.0f, workSize.y - 72.0f)), ImGuiCond_Always);
    if (!ImGui::Begin("##MapWorkspaceToolbar", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.15f, 0.96f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::BeginChild("##MapToolShelf", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (m_Document)
    {
        if (m_ActiveTool != ToolbarTool::Clip && m_ActiveTool != ToolbarTool::Extrude)
        {
            switch (m_Document->GetSelectionMode())
            {
            case MapSelectionMode::Brush:
                m_ActiveTool = ToolbarTool::Select;
                break;
            case MapSelectionMode::Face:
                m_ActiveTool = ToolbarTool::Select;
                break;
            case MapSelectionMode::Edge:
                m_ActiveTool = ToolbarTool::Edge;
                break;
            case MapSelectionMode::Vertex:
                m_ActiveTool = ToolbarTool::Vertex;
                break;
            }
        }

        if (DrawToolButton("SelectTool", "Brush", "Select", "Select and manipulate whole brushes.", "1",
                           m_ActiveTool == ToolbarTool::Select, true, ToolGlyph::Brush))
        {
            m_ActiveTool = ToolbarTool::Select;
            m_Document->SetSelectionMode(MapSelectionMode::Brush);
        }

        if (DrawToolButton("EdgeTool", "Edge", "Edge", "Select and move brush edges.", "3",
                           m_ActiveTool == ToolbarTool::Edge, true, ToolGlyph::Edge))
        {
            m_ActiveTool = ToolbarTool::Edge;
            m_Document->SetSelectionMode(MapSelectionMode::Edge);
        }

        if (DrawToolButton("VertexTool", "Vertex", "Vertex", "Select and move individual brush vertices.", "4",
                           m_ActiveTool == ToolbarTool::Vertex, true, ToolGlyph::Vertex))
        {
            m_ActiveTool = ToolbarTool::Vertex;
            m_Document->SetSelectionMode(MapSelectionMode::Vertex);
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (DrawToolButton("ClipTool", "Clip", "Clip", "Clip the selected brush using the selected face plane.", "C",
                           m_ActiveTool == ToolbarTool::Clip, true, ToolGlyph::Clip))
        {
            m_ActiveTool = ToolbarTool::Clip;
            if (m_ViewportPanel)
            {
                m_ClipOffset = m_ViewportPanel->GetMapClipPreviewOffset();
                m_ClipFlipPlane = m_ViewportPanel->GetMapClipPreviewFlipPlane();
            }
            m_Document->SetSelectionMode(MapSelectionMode::Face);
            BeginClipTool();
        }
        if (DrawToolButton("ExtrudeTool", "Extrude", "Extrude", "Pull new volume from a selected face.", "E",
                           m_ActiveTool == ToolbarTool::Extrude, true, ToolGlyph::Extrude))
        {
            m_ActiveTool = ToolbarTool::Extrude;
            if (m_ViewportPanel)
                m_ExtrudeDistance = m_ViewportPanel->GetMapExtrudePreviewDistance();
            m_Document->SetSelectionMode(MapSelectionMode::Face);
            BeginExtrudeTool();
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (DrawToolButton("AddBrush", "New Box Brush", "New Box Brush", "Create a new brush at the origin.",
                           "Shift+A", false, true, ToolGlyph::AddBrush))
        {
            ApplyWorkspaceMapEdit(*m_Document, "Create Box Brush",
                                  [&]() { m_Document->CreateBoxBrush(Vec3::Zero(), Vec3(0.5f, 0.5f, 0.5f)); return true; });
        }

        const bool canHollowBrush = m_Document->GetSelectedBrush() != nullptr;
        if (DrawToolButton("HollowBox", "Hollow Box", "Hollow Box",
                           "Replace the selected convex brush with inward shell brushes to form a room.", "Shift+H", false,
                           canHollowBrush, ToolGlyph::Hollow))
        {
            m_ShowHollowBoxSettings = true;
        }

        if (DrawToolButton("SaveMap", "Save Map", "Save Map", "Save the current map to disk.", nullptr, false, true,
                           ToolGlyph::Save))
        {
            bool saved = false;
            if (m_SaveMapCallback)
                saved = m_SaveMapCallback();
            else
                saved = m_Document->Save();

            m_SaveStatusIsError = !saved;
            if (saved)
            {
                m_SaveStatus = "Saved " + m_Document->GetPath().filename().string();
            }
            else
            {
                m_SaveStatus = m_Document->GetLastError().empty() ? "Failed to save map" : m_Document->GetLastError();
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void MapWorkspace::HandleToolKeyboardShortcuts()
{
    if (!m_Document || !m_ViewportPanel)
        return;

    if (ImGui::GetIO().WantTextInput)
        return;

    const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (!enterPressed && !escapePressed)
        return;

    switch (m_ActiveTool)
    {
    case ToolbarTool::Clip:
        if (enterPressed)
        {
            if (ApplyWorkspaceMapEdit(*m_Document, "Clip Brush",
                                      [&]() { return m_Document->ClipSelectedBrush(m_ClipOffset, m_ClipFlipPlane); }))
            {
                BeginClipTool();
            }
        }
        if (escapePressed)
        {
            ResetClipToolPreview();
        }
        break;
    case ToolbarTool::Extrude:
        if (enterPressed)
        {
            if (ApplyWorkspaceMapEdit(*m_Document, "Extrude Face",
                                      [&]() { return m_Document->ExtrudeSelectedFace(m_ExtrudeDistance); }))
            {
                BeginExtrudeTool();
            }
        }
        if (escapePressed)
        {
            ResetExtrudeToolPreview();
        }
        break;
    default:
        break;
    }
}

void MapWorkspace::BeginClipTool()
{
    m_ClipBaseOffset = m_ClipOffset;
    m_ClipBaseFlipPlane = m_ClipFlipPlane;
}

void MapWorkspace::BeginExtrudeTool()
{
    m_ExtrudeBaseDistance = m_ExtrudeDistance;
}

void MapWorkspace::ResetClipToolPreview()
{
    m_ClipOffset = m_ClipBaseOffset;
    m_ClipFlipPlane = m_ClipBaseFlipPlane;
    if (m_ViewportPanel)
        m_ViewportPanel->SetMapClipPreview(m_ClipOffset, m_ClipFlipPlane);
}

void MapWorkspace::ResetExtrudeToolPreview()
{
    m_ExtrudeDistance = m_ExtrudeBaseDistance;
    if (m_ViewportPanel)
        m_ViewportPanel->SetMapExtrudePreviewDistance(m_ExtrudeDistance);
}

void MapWorkspace::DrawToolSettings()
{
    const bool showClip = m_ActiveTool == ToolbarTool::Clip;
    const bool showExtrude = m_ActiveTool == ToolbarTool::Extrude;
    const bool showHollow = m_ShowHollowBoxSettings;
    if (!showClip && !showExtrude && !showHollow)
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport->WorkPos;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
    const float panelHeight = showClip ? 216.0f : (showExtrude ? 204.0f : 216.0f);
    ImGui::SetNextWindowPos(ImVec2(workPos.x + kMapToolbarRailWidth + 16.0f, workPos.y + 224.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(292.0f, panelHeight), ImGuiCond_Always);
    if (!ImGui::Begin("##MapToolSettings", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    if (showClip)
        DrawClipToolSettings();
    else if (showExtrude)
        DrawExtrudeToolSettings();
    else
        DrawHollowBoxToolSettings();

    ImGui::End();
}

void MapWorkspace::DrawClipToolSettings()
{
    ImGui::TextUnformatted("Clip Tool");
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Select a face to define the clip plane, then apply the cut to the selected brush.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const MapFace* selectedFace = m_Document ? m_Document->GetSelectedFace() : nullptr;
    const MapBrush* selectedBrush = m_Document ? m_Document->GetSelectedBrush() : nullptr;
    const bool canClip = selectedBrush && selectedFace;

    if (!canClip)
    {
        ImGui::TextDisabled("Face selection required.");
        if (ImGui::Button("Use Face Mode"))
        {
            if (m_Document)
                m_Document->SetSelectionMode(MapSelectionMode::Face);
        }
        return;
    }

    ImGui::DragFloat("Plane Offset", &m_ClipOffset, 0.05f, -64.0f, 64.0f, "%.2f");
    ImGui::Checkbox("Flip Side", &m_ClipFlipPlane);
    ImGui::Spacing();

    if (ImGui::Button("Apply Clip", ImVec2(-1.0f, 32.0f)))
    {
        if (ApplyWorkspaceMapEdit(*m_Document, "Clip Brush",
                                  [&]() { return m_Document->ClipSelectedBrush(m_ClipOffset, m_ClipFlipPlane); }))
        {
            BeginClipTool();
        }
    }
    if (ImGui::Button("Cancel", ImVec2(-1.0f, 28.0f)))
        ResetClipToolPreview();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.69f, 0.77f, 1.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Enter applies the clip. Esc restores the last preview values.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void MapWorkspace::DrawExtrudeToolSettings()
{
    ImGui::TextUnformatted("Extrude Tool");
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Select a face, drag the viewport handle or adjust the distance, then apply the extrusion.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const MapFace* selectedFace = m_Document ? m_Document->GetSelectedFace() : nullptr;
    const MapBrush* selectedBrush = m_Document ? m_Document->GetSelectedBrush() : nullptr;
    const bool canExtrude = selectedBrush && selectedFace;

    if (!canExtrude)
    {
        ImGui::TextDisabled("Face selection required.");
        if (ImGui::Button("Use Face Mode"))
        {
            if (m_Document)
                m_Document->SetSelectionMode(MapSelectionMode::Face);
        }
        return;
    }

    ImGui::DragFloat("Distance", &m_ExtrudeDistance, 0.05f, -64.0f, 64.0f, "%.2f");
    ImGui::Spacing();
    if (ImGui::Button("Apply Extrude", ImVec2(-1.0f, 32.0f)))
    {
        if (ApplyWorkspaceMapEdit(*m_Document, "Extrude Face",
                                  [&]() { return m_Document->ExtrudeSelectedFace(m_ExtrudeDistance); }))
        {
            BeginExtrudeTool();
        }
    }
    if (ImGui::Button("Cancel", ImVec2(-1.0f, 28.0f)))
        ResetExtrudeToolPreview();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.69f, 0.77f, 1.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Enter applies the extrude. Esc restores the last preview distance.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void MapWorkspace::DrawHollowBoxToolSettings()
{
    ImGui::TextUnformatted("Hollow Box");
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Turns the selected convex brush into inward shell brushes using the chosen wall thickness.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const MapBrush* selectedBrush = m_Document ? m_Document->GetSelectedBrush() : nullptr;
    if (!selectedBrush)
    {
        ImGui::TextDisabled("Brush selection required.");
        if (ImGui::Button("Use Brush Mode"))
        {
            if (m_Document)
                m_Document->SetSelectionMode(MapSelectionMode::Brush);
        }
        if (ImGui::Button("Close", ImVec2(-1.0f, 28.0f)))
            m_ShowHollowBoxSettings = false;
        return;
    }

    ImGui::DragFloat("Wall Thickness", &m_HollowWallThickness, 0.05f, 0.05f, 64.0f, "%.2f");
    ImGui::Spacing();

    if (ImGui::Button("Apply Hollow Box", ImVec2(-1.0f, 32.0f)))
    {
        if (ApplyWorkspaceMapEdit(*m_Document, "Hollow Box Brush",
                                  [&]() { return m_Document->HollowSelectedBrush(m_HollowWallThickness); }))
        {
            m_ShowHollowBoxSettings = false;
        }
    }

    if (ImGui::Button("Close", ImVec2(-1.0f, 28.0f)))
        m_ShowHollowBoxSettings = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.69f, 0.77f, 1.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Works on convex brushes. The original brush is replaced by one shell brush per outer face.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool MapWorkspace::DrawToolButton(const char* id, const char* shortLabel, const char* title, const char* description,
                                  const char* shortcut, bool active, bool enabled, ToolGlyph glyph)
{
    ImGui::PushID(id);
    (void)shortLabel;

    const ImVec2 buttonSize(56.0f, 56.0f);
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    if (contentWidth > buttonSize.x)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentWidth - buttonSize.x) * 0.5f);

    ImVec4 buttonColor = active ? ImVec4(0.18f, 0.34f, 0.56f, 1.0f) : ImVec4(0.15f, 0.17f, 0.21f, 1.0f);
    ImVec4 hoveredColor = active ? ImVec4(0.22f, 0.40f, 0.66f, 1.0f) : ImVec4(0.20f, 0.23f, 0.29f, 1.0f);
    ImVec4 activeColor = active ? ImVec4(0.15f, 0.30f, 0.50f, 1.0f) : ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
    if (!enabled)
    {
        buttonColor = ImVec4(0.11f, 0.12f, 0.14f, 0.78f);
        hoveredColor = buttonColor;
        activeColor = buttonColor;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
    if (!enabled)
        ImGui::BeginDisabled();

    const bool clicked = ImGui::Button("##toolbutton", buttonSize);
    if (!enabled)
        ImGui::EndDisabled();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cx = (min.x + max.x) * 0.5f;
    const float topY = min.y + 14.0f;

    const ImVec4 accent = active ? ImVec4(0.56f, 0.82f, 0.98f, 1.0f) : ImVec4(0.50f, 0.56f, 0.64f, 1.0f);
    const ImVec4 body = enabled ? ImVec4(0.90f, 0.93f, 0.98f, 1.0f) : ImVec4(0.46f, 0.49f, 0.54f, 1.0f);
    const ImU32 accentU32 = ImGui::ColorConvertFloat4ToU32(accent);
    const ImU32 bodyU32 = ImGui::ColorConvertFloat4ToU32(body);
    const float iconLeft = min.x + 15.0f;
    const float iconRight = max.x - 15.0f;
    const float iconTop = min.y + 12.0f;
    const float iconBottom = min.y + 30.0f;

    switch (glyph)
    {
    case ToolGlyph::Brush:
        drawList->AddRect(ImVec2(iconLeft, iconTop), ImVec2(iconRight, iconBottom), accentU32, 3.0f, 0, 1.8f);
        drawList->AddLine(ImVec2(iconLeft + 5.0f, iconTop + 4.0f), ImVec2(iconRight + 1.0f, iconTop + 4.0f), accentU32, 1.4f);
        drawList->AddLine(ImVec2(iconRight, iconTop), ImVec2(iconRight, iconBottom - 4.0f), accentU32, 1.4f);
        break;
    case ToolGlyph::Edge:
        drawList->AddLine(ImVec2(iconLeft, (iconTop + iconBottom) * 0.5f),
                          ImVec2(iconRight, (iconTop + iconBottom) * 0.5f), accentU32, 2.5f);
        drawList->AddCircleFilled(ImVec2(iconLeft, (iconTop + iconBottom) * 0.5f), 2.5f, bodyU32);
        drawList->AddCircleFilled(ImVec2(iconRight, (iconTop + iconBottom) * 0.5f), 2.5f, bodyU32);
        break;
    case ToolGlyph::Vertex:
        drawList->AddCircleFilled(ImVec2(cx, topY + 6.0f), 5.0f, accentU32);
        drawList->AddCircle(ImVec2(cx, topY + 6.0f), 8.0f, bodyU32, 0, 1.0f);
        break;
    case ToolGlyph::Clip:
        drawList->AddLine(ImVec2(iconLeft, iconBottom), ImVec2(iconRight, iconTop), accentU32, 2.0f);
        drawList->AddLine(ImVec2(iconLeft + 2.0f, iconTop + 2.0f), ImVec2(iconLeft + 8.0f, iconTop + 8.0f), bodyU32, 1.4f);
        drawList->AddLine(ImVec2(iconRight - 8.0f, iconBottom - 8.0f), ImVec2(iconRight - 2.0f, iconBottom - 2.0f), bodyU32, 1.4f);
        break;
    case ToolGlyph::Extrude:
        drawList->AddRect(ImVec2(iconLeft, iconTop + 4.0f), ImVec2(iconRight - 7.0f, iconBottom - 2.0f), accentU32, 2.0f, 0, 1.6f);
        drawList->AddLine(ImVec2(iconRight - 10.0f, (iconTop + iconBottom) * 0.5f),
                          ImVec2(iconRight + 2.0f, (iconTop + iconBottom) * 0.5f), accentU32, 2.0f);
        drawList->AddTriangleFilled(ImVec2(iconRight + 2.0f, (iconTop + iconBottom) * 0.5f),
                                    ImVec2(iconRight - 3.0f, (iconTop + iconBottom) * 0.5f - 4.0f),
                                    ImVec2(iconRight - 3.0f, (iconTop + iconBottom) * 0.5f + 4.0f), accentU32);
        break;
    case ToolGlyph::Hollow:
        drawList->AddRect(ImVec2(iconLeft, iconTop), ImVec2(iconRight, iconBottom), accentU32, 3.0f, 0, 1.5f);
        drawList->AddRect(ImVec2(iconLeft + 4.0f, iconTop + 4.0f), ImVec2(iconRight - 4.0f, iconBottom - 4.0f), bodyU32,
                          2.0f, 0, 1.4f);
        drawList->AddLine(ImVec2(iconLeft + 4.0f, iconTop + 4.0f), ImVec2(iconRight - 4.0f, iconBottom - 4.0f), accentU32,
                          1.2f);
        drawList->AddLine(ImVec2(iconRight - 4.0f, iconTop + 4.0f), ImVec2(iconLeft + 4.0f, iconBottom - 4.0f), accentU32,
                          1.2f);
        break;
    case ToolGlyph::AddBrush:
        drawList->AddRect(ImVec2(iconLeft + 2.0f, iconTop + 3.0f), ImVec2(iconRight - 2.0f, iconBottom + 1.0f), accentU32, 3.0f, 0, 1.5f);
        drawList->AddLine(ImVec2(cx, iconTop - 1.0f), ImVec2(cx, iconBottom + 5.0f), bodyU32, 1.8f);
        drawList->AddLine(ImVec2(iconLeft + 6.0f, topY + 8.0f), ImVec2(iconRight - 6.0f, topY + 8.0f), bodyU32, 1.8f);
        break;
    case ToolGlyph::Save:
        drawList->AddRect(ImVec2(iconLeft, iconTop), ImVec2(iconRight, iconBottom), accentU32, 3.0f, 0, 1.6f);
        drawList->AddRectFilled(ImVec2(iconLeft + 4.0f, iconTop + 3.0f), ImVec2(iconRight - 4.0f, iconTop + 9.0f), accentU32, 1.5f);
        drawList->AddRect(ImVec2(iconLeft + 6.0f, iconTop + 12.0f), ImVec2(iconRight - 6.0f, iconBottom - 3.0f), bodyU32, 1.5f, 0, 1.1f);
        break;
    }

    if (ImGui::IsItemHovered())
    {
        if (ImGui::BeginTooltip())
        {
            ImGui::TextUnformatted(title);
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
            ImGui::TextUnformatted(description);
            if (shortcut && shortcut[0] != '\0')
                ImGui::Text("Shortcut: %s", shortcut);
            if (!enabled)
                ImGui::TextUnformatted("Coming Soon");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return enabled && clicked;
}

} // namespace Dot
