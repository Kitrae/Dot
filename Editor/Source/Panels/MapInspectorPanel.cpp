#include "MapInspectorPanel.h"
#include "PanelChrome.h"

#include "Core/Assets/AssetManager.h"

#include "../Commands/CommandRegistry.h"
#include "../Commands/MapCommands.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>

namespace Dot
{

namespace
{

bool SameSelection(const MapSelection& a, const MapSelection& b)
{
    return a.brushId == b.brushId && a.faceIndex == b.faceIndex && a.vertexIndex == b.vertexIndex &&
           a.edgeVertexA == b.edgeVertexA && a.edgeVertexB == b.edgeVertexB;
}

bool SameSelections(const std::vector<MapSelection>& a, const std::vector<MapSelection>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!SameSelection(a[i], b[i]))
            return false;
    }
    return true;
}

bool SameFaceUv(const MapFaceUV& a, const MapFaceUV& b)
{
    return a.projectionMode == b.projectionMode && a.scaleU == b.scaleU && a.scaleV == b.scaleV && a.offsetU == b.offsetU &&
           a.offsetV == b.offsetV && a.rotationDeg == b.rotationDeg;
}

template <typename Fn>
bool ApplyMapEdit(MapDocument& document, const char* name, Fn&& fn)
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

    auto command = std::make_unique<MapSnapshotCommand>(&document, name, beforeAsset, beforeSelections, beforeSelection,
                                                        beforeHiddenBrushIds, beforeLockedBrushIds, beforeDirty,
                                                        afterAsset, afterSelections, afterSelection,
                                                        afterHiddenBrushIds, afterLockedBrushIds, afterDirty);
    CommandRegistry::Get().PushCommand(std::move(command));
    return true;
}

std::vector<std::string> CollectMaterialAssets()
{
    std::vector<std::string> materials;
    const std::filesystem::path root = std::filesystem::path(AssetManager::Get().GetRootPath()) / "Materials";
    if (!std::filesystem::exists(root))
        return materials;

    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".dotmat")
            continue;
        materials.push_back("Materials/" + entry.path().filename().string());
    }

    std::sort(materials.begin(), materials.end());
    return materials;
}

} // namespace

bool MapInspectorPanel::DrawHalfWidthButton(const char* label, bool sameLine)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = std::max(72.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
    if (sameLine)
        ImGui::SameLine();
    return ImGui::Button(label, ImVec2(width, 0.0f));
}

void MapInspectorPanel::PushRecentMaterial(const std::string& materialPath)
{
    if (materialPath.empty())
        return;

    auto it = std::find(m_RecentMaterials.begin(), m_RecentMaterials.end(), materialPath);
    if (it != m_RecentMaterials.end())
        m_RecentMaterials.erase(it);

    m_RecentMaterials.insert(m_RecentMaterials.begin(), materialPath);
    if (m_RecentMaterials.size() > 6)
        m_RecentMaterials.resize(6);
}

void MapInspectorPanel::SyncUiState(bool force)
{
    if (!m_Document)
        return;

    const MapSelection selection = m_Document->GetSelection();
    const std::vector<MapSelection> selections = m_Document->GetSelections();
    const uint64 revision = m_Document->GetRevision();
    if (!force && SameSelection(selection, m_LastSelection) && SameSelections(selections, m_LastSelections) &&
        revision == m_LastRevision)
        return;

    m_LastSelection = selection;
    m_LastSelections = selections;
    m_LastRevision = revision;

    std::fill(m_BrushNameBuffer.begin(), m_BrushNameBuffer.end(), '\0');
    std::fill(m_MaterialBuffer.begin(), m_MaterialBuffer.end(), '\0');
    m_BrushDelta[0] = m_BrushDelta[1] = m_BrushDelta[2] = 0.0f;
    m_EdgeDelta[0] = m_EdgeDelta[1] = m_EdgeDelta[2] = 0.0f;
    m_VertexDelta[0] = m_VertexDelta[1] = m_VertexDelta[2] = 0.0f;

    if (MapBrush* brush = m_Document->GetSelectedBrush())
    {
        strncpy(m_BrushNameBuffer.data(), brush->name.c_str(), m_BrushNameBuffer.size() - 1);
    }

    if (MapFace* face = m_Document->GetSelectedFace())
    {
        strncpy(m_MaterialBuffer.data(), face->materialPath.c_str(), m_MaterialBuffer.size() - 1);
        m_FaceUv = face->uv;
    }
    else
    {
        m_FaceUv = {};
    }
}

void MapInspectorPanel::OnImGui()
{
    if (!m_Open)
        return;

    BeginChromeWindow(m_Name.c_str(), &m_Open);

    if (!m_Document)
    {
        ImGui::TextDisabled("No map document");
        ImGui::End();
        return;
    }

    SyncUiState();

    const MapSelection& selection = m_Document->GetSelection();
    const size_t selectionCount = m_Document->GetSelections().size();
    MapBrush* brush = m_Document->GetSelectedBrush();
    MapFace* face = m_Document->GetSelectedFace();

    ImGui::Text("Path: %s", m_Document->HasPath() ? m_Document->GetPath().string().c_str() : "(unsaved)");
    ImGui::Text("Brushes: %zu", m_Document->GetAsset().brushes.size());
    if (m_Document->IsDirty())
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Unsaved changes");
    ImGui::Separator();

    ImGui::SeparatorText("Selection Summary");
    const char* modeName = "Brush";
    switch (m_Document->GetSelectionMode())
    {
        case MapSelectionMode::Brush: modeName = "Brush"; break;
        case MapSelectionMode::Face: modeName = "Face"; break;
        case MapSelectionMode::Edge: modeName = "Edge"; break;
        case MapSelectionMode::Vertex: modeName = "Vertex"; break;
    }
    ImGui::Text("Mode: %s", modeName);
    ImGui::Text("Selected: %zu map element%s", selectionCount, selectionCount == 1 ? "" : "s");

    if (!brush)
    {
        ImGui::TextDisabled("No map selection");
        ImGui::End();
        return;
    }

    const bool brushHidden = m_Document->IsBrushHidden(brush->brushId);
    const bool brushLocked = m_Document->IsBrushLocked(brush->brushId);

    ImGui::SeparatorText("Brush");

    ImGui::InputText("Brush Name", m_BrushNameBuffer.data(), m_BrushNameBuffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const std::string newName = m_BrushNameBuffer.data();
        if (newName != brush->name &&
            ApplyMapEdit(*m_Document, "Rename Brush", [&]() { return m_Document->RenameSelectedBrush(newName); }))
        {
            SyncUiState(true);
        }
    }

    Vec3 center = m_Document->GetSelectedBrushCenter();
    ImGui::Text("Center: %.2f %.2f %.2f", center.x, center.y, center.z);
    if (brushHidden || brushLocked)
    {
        ImVec4 stateColor = brushHidden && brushLocked ? ImVec4(0.90f, 0.64f, 0.34f, 1.0f)
                                                       : (brushHidden ? ImVec4(0.76f, 0.78f, 0.84f, 1.0f)
                                                                      : ImVec4(0.92f, 0.72f, 0.40f, 1.0f));
        const char* stateText = brushHidden && brushLocked ? "Hidden and locked" : (brushHidden ? "Hidden" : "Locked");
        ImGui::TextColored(stateColor, "State: %s", stateText);
    }
    else
    {
        ImGui::TextDisabled("State: Visible and editable");
    }
    ImGui::DragFloat3("Brush Delta", m_BrushDelta, 0.1f);
    if (ImGui::Button("Move Brush"))
    {
        if (ApplyMapEdit(*m_Document, "Move Brush",
                         [&]() { return m_Document->TranslateSelectedBrush(Vec3(m_BrushDelta[0], m_BrushDelta[1], m_BrushDelta[2])); }))
        {
            m_BrushDelta[0] = m_BrushDelta[1] = m_BrushDelta[2] = 0.0f;
            SyncUiState(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Brush"))
    {
        if (ApplyMapEdit(*m_Document, "Duplicate Brush", [&]() { return m_Document->DuplicateSelectedBrush(); }))
            SyncUiState(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Brush"))
    {
        if (ApplyMapEdit(*m_Document, "Delete Brush", [&]() { return m_Document->DeleteSelectedBrush(); }))
            SyncUiState(true);
    }

    ImGui::SeparatorText("Brush Actions");
    if (DrawHalfWidthButton(brushHidden ? "Unhide Brush" : "Hide Brush"))
    {
        if (ApplyMapEdit(*m_Document, brushHidden ? "Unhide Brush" : "Hide Brush",
                         [&]() { return m_Document->SetBrushHidden(brush->brushId, !brushHidden); }))
        {
            SyncUiState(true);
        }
    }
    if (DrawHalfWidthButton(brushLocked ? "Unlock Brush" : "Lock Brush", true))
    {
        if (ApplyMapEdit(*m_Document, brushLocked ? "Unlock Brush" : "Lock Brush",
                         [&]() { return m_Document->SetBrushLocked(brush->brushId, !brushLocked); }))
        {
            SyncUiState(true);
        }
    }
    if (DrawHalfWidthButton("Hide Selected"))
    {
        if (ApplyMapEdit(*m_Document, "Hide Selected Brushes", [&]() { return m_Document->HideSelectedBrushes(); }))
            SyncUiState(true);
    }
    if (DrawHalfWidthButton("Isolate", true))
    {
        if (ApplyMapEdit(*m_Document, "Isolate Selected Brushes", [&]() { return m_Document->IsolateSelectedBrushes(); }))
            SyncUiState(true);
    }
    if (DrawHalfWidthButton("Unhide All"))
    {
        if (ApplyMapEdit(*m_Document, "Unhide All Brushes", [&]() { return m_Document->UnhideAllBrushes(); }))
            SyncUiState(true);
    }
    if (DrawHalfWidthButton("Lock Selected", true))
    {
        if (ApplyMapEdit(*m_Document, "Lock Selected Brushes", [&]() { return m_Document->LockSelectedBrushes(); }))
            SyncUiState(true);
    }
    if (DrawHalfWidthButton("Unlock All"))
    {
        if (ApplyMapEdit(*m_Document, "Unlock All Brushes", [&]() { return m_Document->UnlockAllBrushes(); }))
            SyncUiState(true);
    }

    if (face)
    {
        ImGui::SeparatorText("Face");
        ImGui::Text("Face %d", selection.faceIndex);

        ImGui::DragFloat("Face Distance", &m_FaceDistance, 0.1f);
        if (ImGui::Button("Move Face"))
        {
            if (ApplyMapEdit(*m_Document, "Move Face", [&]() { return m_Document->TranslateSelectedFace(m_FaceDistance); }))
                SyncUiState(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Extrude Face"))
        {
            if (ApplyMapEdit(*m_Document, "Extrude Face", [&]() { return m_Document->ExtrudeSelectedFace(m_FaceDistance); }))
                SyncUiState(true);
        }

        ImGui::SeparatorText("Surface");
        ImGui::InputText("Material", m_MaterialBuffer.data(), m_MaterialBuffer.size());
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const std::string materialPath = m_MaterialBuffer.data();
            if (materialPath != face->materialPath &&
                ApplyMapEdit(*m_Document, "Set Face Material", [&]() { return m_Document->SetSelectedFaceMaterial(materialPath); }))
            {
                PushRecentMaterial(materialPath);
                SyncUiState(true);
            }
        }

        const std::vector<std::string> materials = CollectMaterialAssets();
        if (ImGui::BeginCombo("Material Assets", face->materialPath.empty() ? "(none)" : face->materialPath.c_str()))
        {
            for (const std::string& material : materials)
            {
                const bool selectedMaterial = (material == face->materialPath);
                if (ImGui::Selectable(material.c_str(), selectedMaterial))
                {
                    if (ApplyMapEdit(*m_Document, "Set Face Material",
                                     [&]() { return m_Document->SetSelectedFaceMaterial(material); }))
                    {
                        PushRecentMaterial(material);
                        SyncUiState(true);
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (!m_RecentMaterials.empty())
        {
            ImGui::SeparatorText("Recent Materials");
            for (size_t i = 0; i < m_RecentMaterials.size(); ++i)
            {
                if (i % 2 == 1)
                    ImGui::SameLine();
                if (ImGui::SmallButton(m_RecentMaterials[i].c_str()))
                {
                    if (ApplyMapEdit(*m_Document, "Set Face Material",
                                     [&]() { return m_Document->SetSelectedFaceMaterial(m_RecentMaterials[i]); }))
                    {
                        PushRecentMaterial(m_RecentMaterials[i]);
                        SyncUiState(true);
                    }
                }
            }
        }

        ImGui::SeparatorText("Surface Clipboard");
        if (DrawHalfWidthButton("Copy Surface"))
        {
            m_SurfaceClipboardMaterial = face->materialPath;
            m_SurfaceClipboardUv = face->uv;
            m_SurfaceClipboardValid = true;
            PushRecentMaterial(face->materialPath);
        }
        ImGui::BeginDisabled(!m_SurfaceClipboardValid);
        if (DrawHalfWidthButton("Paste Surface", true))
        {
            if (ApplyMapEdit(*m_Document, "Paste Face Surface",
                             [&]() { return m_Document->SetSelectedFaceSurface(m_SurfaceClipboardMaterial, m_SurfaceClipboardUv); }))
            {
                PushRecentMaterial(m_SurfaceClipboardMaterial);
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Paste Material"))
        {
            if (ApplyMapEdit(*m_Document, "Paste Face Material",
                             [&]() { return m_Document->SetSelectedFaceMaterial(m_SurfaceClipboardMaterial); }))
            {
                PushRecentMaterial(m_SurfaceClipboardMaterial);
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Paste UV", true))
        {
            if (ApplyMapEdit(*m_Document, "Paste Face UV",
                             [&]() { return m_Document->SetSelectedFaceUV(m_SurfaceClipboardUv); }))
            {
                SyncUiState(true);
            }
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Selection Tools");
        if (DrawHalfWidthButton("Coplanar"))
        {
            if (ApplyMapEdit(*m_Document, "Select Coplanar Faces",
                             [&]() { return m_Document->SelectAllCoplanarFaces(); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Same Material", true))
        {
            if (ApplyMapEdit(*m_Document, "Select Faces With Same Material",
                             [&]() { return m_Document->SelectFacesWithSameMaterial(); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Linked Faces"))
        {
            if (ApplyMapEdit(*m_Document, "Select Linked Brush Faces",
                             [&]() { return m_Document->SelectLinkedBrushFaces(); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Grow", true))
        {
            if (ApplyMapEdit(*m_Document, "Grow Face Selection",
                             [&]() { return m_Document->GrowFaceSelection(); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Shrink"))
        {
            if (ApplyMapEdit(*m_Document, "Shrink Face Selection",
                             [&]() { return m_Document->ShrinkFaceSelection(); }))
            {
                SyncUiState(true);
            }
        }

        ImGui::SeparatorText("UV");
        bool uvShouldApply = false;
        int projectionMode = static_cast<int>(m_FaceUv.projectionMode);
        if (ImGui::Combo("Projection", &projectionMode, "Auto\0XY\0XZ\0YZ\0"))
            uvShouldApply = true;
        m_FaceUv.projectionMode = static_cast<MapProjectionMode>(projectionMode);
        ImGui::DragFloat2("UV Scale", &m_FaceUv.scaleU, 0.05f);
        uvShouldApply = uvShouldApply || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("UV Offset", &m_FaceUv.offsetU, 0.05f);
        uvShouldApply = uvShouldApply || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("UV Rotation", &m_FaceUv.rotationDeg, 1.0f);
        uvShouldApply = uvShouldApply || ImGui::IsItemDeactivatedAfterEdit();
        if (uvShouldApply && !SameFaceUv(m_FaceUv, face->uv) &&
            ApplyMapEdit(*m_Document, "Set Face UV", [&]() { return m_Document->SetSelectedFaceUV(m_FaceUv); }))
        {
            SyncUiState(true);
        }

        ImGui::SeparatorText("Texture Tools");
        if (DrawHalfWidthButton("Fit Face"))
        {
            if (ApplyMapEdit(*m_Document, "Fit Face UV", [&]() { return m_Document->FitSelectedFaceUV(); }))
                SyncUiState(true);
        }
        if (DrawHalfWidthButton("Rotate -90", true))
        {
            if (ApplyMapEdit(*m_Document, "Rotate Face UV", [&]() { return m_Document->RotateSelectedFaceUV(-90.0f); }))
                SyncUiState(true);
        }
        if (DrawHalfWidthButton("Rotate +90"))
        {
            if (ApplyMapEdit(*m_Document, "Rotate Face UV", [&]() { return m_Document->RotateSelectedFaceUV(90.0f); }))
                SyncUiState(true);
        }
        if (DrawHalfWidthButton("Flip U", true))
        {
            if (ApplyMapEdit(*m_Document, "Flip Face UV", [&]() { return m_Document->FlipSelectedFaceUV(true, false); }))
                SyncUiState(true);
        }
        if (DrawHalfWidthButton("Flip V"))
        {
            if (ApplyMapEdit(*m_Document, "Flip Face UV", [&]() { return m_Document->FlipSelectedFaceUV(false, true); }))
                SyncUiState(true);
        }
        if (DrawHalfWidthButton("Apply To Brush", true))
        {
            if (ApplyMapEdit(*m_Document, "Set Brush Material",
                             [&]() { return m_Document->SetSelectedBrushMaterial(face->materialPath); }))
            {
                SyncUiState(true);
            }
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("Nudge Step", &m_UvNudgeStep, 0.01f, 0.01f, 16.0f, "%.2f");
        if (DrawHalfWidthButton("Left"))
        {
            if (ApplyMapEdit(*m_Document, "Nudge Face UV",
                             [&]() { return m_Document->NudgeSelectedFaceUV(-m_UvNudgeStep, 0.0f); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Right", true))
        {
            if (ApplyMapEdit(*m_Document, "Nudge Face UV",
                             [&]() { return m_Document->NudgeSelectedFaceUV(m_UvNudgeStep, 0.0f); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Up"))
        {
            if (ApplyMapEdit(*m_Document, "Nudge Face UV",
                             [&]() { return m_Document->NudgeSelectedFaceUV(0.0f, m_UvNudgeStep); }))
            {
                SyncUiState(true);
            }
        }
        if (DrawHalfWidthButton("Down", true))
        {
            if (ApplyMapEdit(*m_Document, "Nudge Face UV",
                             [&]() { return m_Document->NudgeSelectedFaceUV(0.0f, -m_UvNudgeStep); }))
            {
                SyncUiState(true);
            }
        }
    }
    else
    {
        ImGui::SeparatorText("Mode Hint");
        ImGui::TextDisabled("Face surface tools appear when a face is selected.");
    }

    if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0)
    {
        ImGui::SeparatorText("Edge");
        ImGui::DragFloat3("Edge Delta", m_EdgeDelta, 0.05f);
        if (ImGui::Button("Move Edge"))
        {
            if (ApplyMapEdit(*m_Document, "Move Edge",
                             [&]() { return m_Document->TranslateSelectedEdge(Vec3(m_EdgeDelta[0], m_EdgeDelta[1], m_EdgeDelta[2])); }))
            {
                m_EdgeDelta[0] = m_EdgeDelta[1] = m_EdgeDelta[2] = 0.0f;
                SyncUiState(true);
            }
        }
    }

    if (selection.vertexIndex >= 0)
    {
        ImGui::SeparatorText("Vertex");
        const Vec3 vertex = brush->vertices[static_cast<size_t>(selection.vertexIndex)];
        ImGui::Text("Vertex: %.2f %.2f %.2f", vertex.x, vertex.y, vertex.z);
        ImGui::DragFloat3("Vertex Delta", m_VertexDelta, 0.05f);
        if (ImGui::Button("Move Vertex"))
        {
            if (ApplyMapEdit(*m_Document, "Move Vertex",
                             [&]() { return m_Document->TranslateSelectedVertex(Vec3(m_VertexDelta[0], m_VertexDelta[1], m_VertexDelta[2])); }))
            {
                m_VertexDelta[0] = m_VertexDelta[1] = m_VertexDelta[2] = 0.0f;
                SyncUiState(true);
            }
        }
    }
    ImGui::End();
}

} // namespace Dot
