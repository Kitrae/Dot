#include "MapOutlinerPanel.h"
#include "PanelChrome.h"

#include "../Commands/CommandRegistry.h"
#include "../Commands/MapCommands.h"

#include <cstdio>
#include <imgui.h>

namespace Dot
{

namespace
{

struct MapBrushClipboard
{
    bool hasData = false;
    bool wasCut = false;
    MapBrush brush;
};

MapBrushClipboard& GetMapBrushClipboard()
{
    static MapBrushClipboard clipboard;
    return clipboard;
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

    auto command =
        std::make_unique<MapSnapshotCommand>(&document, name, beforeAsset, beforeSelections, beforeSelection,
                                             beforeHiddenBrushIds, beforeLockedBrushIds, beforeDirty,
                                             document.GetAsset(), document.GetSelections(), document.GetSelection(),
                                             document.GetHiddenBrushIds(), document.GetLockedBrushIds(), document.IsDirty());
    CommandRegistry::Get().PushCommand(std::move(command));
    return true;
}

bool CopySelectedBrush(MapDocument& document, bool wasCut)
{
    const MapBrush* selectedBrush = document.GetSelectedBrush();
    if (!selectedBrush)
        return false;

    MapBrushClipboard& clipboard = GetMapBrushClipboard();
    clipboard.brush = *selectedBrush;
    clipboard.hasData = true;
    clipboard.wasCut = wasCut;
    return true;
}

bool PasteClipboardBrush(MapDocument& document)
{
    MapBrushClipboard& clipboard = GetMapBrushClipboard();
    if (!clipboard.hasData)
        return false;

    const bool pasted = ApplyMapEdit(document, clipboard.wasCut ? "Move Brush" : "Paste Brush",
                                     [&]() { return document.PasteBrush(clipboard.brush, Vec3(1.0f, 0.0f, 0.0f), !clipboard.wasCut) != 0; });
    if (pasted && clipboard.wasCut)
        clipboard = {};
    return pasted;
}

} // namespace

void MapOutlinerPanel::OnImGui()
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

    const bool outlinerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool canHandleShortcuts = outlinerFocused && !ImGui::IsAnyItemActive();
    const bool hasBrushSelection = (m_Document->GetSelectedBrush() != nullptr);

    if (ImGui::Button("Hide Selected"))
        ApplyMapEdit(*m_Document, "Hide Selected Brushes", [&]() { return m_Document->HideSelectedBrushes(); });
    ImGui::SameLine();
    if (ImGui::Button("Isolate"))
        ApplyMapEdit(*m_Document, "Isolate Selected Brushes", [&]() { return m_Document->IsolateSelectedBrushes(); });
    ImGui::SameLine();
    if (ImGui::Button("Unhide All"))
        ApplyMapEdit(*m_Document, "Unhide All Brushes", [&]() { return m_Document->UnhideAllBrushes(); });
    if (ImGui::Button("Lock Selected"))
        ApplyMapEdit(*m_Document, "Lock Selected Brushes", [&]() { return m_Document->LockSelectedBrushes(); });
    ImGui::SameLine();
    if (ImGui::Button("Unlock All"))
        ApplyMapEdit(*m_Document, "Unlock All Brushes", [&]() { return m_Document->UnlockAllBrushes(); });

    ImGui::Separator();

    if (canHandleShortcuts)
    {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            CopySelectedBrush(*m_Document, false);

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && CopySelectedBrush(*m_Document, true))
            ApplyMapEdit(*m_Document, "Cut Brush", [&]() { return m_Document->DeleteSelectedBrush(); });

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
            PasteClipboardBrush(*m_Document);

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            ApplyMapEdit(*m_Document, "Duplicate Brush", [&]() { return m_Document->DuplicateSelectedBrush(); });

        if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && hasBrushSelection)
            ApplyMapEdit(*m_Document, "Delete Brush", [&]() { return m_Document->DeleteSelectedBrush(); });
    }

    for (const MapBrush& brush : m_Document->GetAsset().brushes)
    {
        const bool hidden = m_Document->IsBrushHidden(brush.brushId);
        const bool locked = m_Document->IsBrushLocked(brush.brushId);
        const MapSelection brushSelection{brush.brushId};
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (m_Document->IsSelectionSelected(brushSelection))
            flags |= ImGuiTreeNodeFlags_Selected;

        if (hidden)
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(130, 136, 150, 255));
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(brush.brushId)), flags, "%s",
                                            brush.name.c_str());
        if (hidden)
            ImGui::PopStyleColor();
        if (ImGui::IsItemClicked())
        {
            if (ImGui::GetIO().KeyCtrl)
                m_Document->ToggleSelection(brushSelection);
            else
                m_Document->SetSelection(brushSelection);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !m_Document->IsSelectionSelected(brushSelection))
        {
            m_Document->SetSelection(brushSelection);
        }

        if (ImGui::BeginPopupContextItem())
        {
            MapBrushClipboard& clipboard = GetMapBrushClipboard();
            if (ImGui::MenuItem(hidden ? "Unhide" : "Hide"))
                ApplyMapEdit(*m_Document, hidden ? "Unhide Brush" : "Hide Brush",
                             [&]() { return m_Document->SetBrushHidden(brush.brushId, !hidden); });
            if (ImGui::MenuItem(locked ? "Unlock" : "Lock"))
                ApplyMapEdit(*m_Document, locked ? "Unlock Brush" : "Lock Brush",
                             [&]() { return m_Document->SetBrushLocked(brush.brushId, !locked); });
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasBrushSelection))
                CopySelectedBrush(*m_Document, false);
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasBrushSelection) && CopySelectedBrush(*m_Document, true))
                ApplyMapEdit(*m_Document, "Cut Brush", [&]() { return m_Document->DeleteSelectedBrush(); });
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, clipboard.hasData))
                PasteClipboardBrush(*m_Document);
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasBrushSelection))
                ApplyMapEdit(*m_Document, "Duplicate Brush", [&]() { return m_Document->DuplicateSelectedBrush(); });
            if (ImGui::MenuItem("Delete", "Del", false, hasBrushSelection))
                ApplyMapEdit(*m_Document, "Delete Brush", [&]() { return m_Document->DeleteSelectedBrush(); });
            ImGui::EndPopup();
        }

            if (open)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(hidden ? "S" : "H"))
                ApplyMapEdit(*m_Document, hidden ? "Unhide Brush" : "Hide Brush",
                             [&]() { return m_Document->SetBrushHidden(brush.brushId, !hidden); });
            ImGui::SameLine();
            if (ImGui::SmallButton(locked ? "U" : "L"))
                ApplyMapEdit(*m_Document, locked ? "Unlock Brush" : "Lock Brush",
                             [&]() { return m_Document->SetBrushLocked(brush.brushId, !locked); });
            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                MapSelection faceSelection;
                faceSelection.brushId = brush.brushId;
                faceSelection.faceIndex = static_cast<int>(faceIndex);
                const bool selected = m_Document->IsSelectionSelected(faceSelection);
                char label[64];
                snprintf(label, sizeof(label), "Face %zu", faceIndex);
                if (ImGui::Selectable(label, selected))
                {
                    if (ImGui::GetIO().KeyCtrl)
                        m_Document->ToggleSelection(faceSelection);
                    else
                        m_Document->SetSelection(faceSelection);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
                {
                    m_Document->SetSelection(faceSelection);
                }
                if (ImGui::BeginPopupContextItem())
                {
                    MapBrushClipboard& clipboard = GetMapBrushClipboard();
                    if (ImGui::MenuItem("Copy Brush", "Ctrl+C", false, hasBrushSelection))
                        CopySelectedBrush(*m_Document, false);
                    if (ImGui::MenuItem("Cut Brush", "Ctrl+X", false, hasBrushSelection) && CopySelectedBrush(*m_Document, true))
                        ApplyMapEdit(*m_Document, "Cut Brush", [&]() { return m_Document->DeleteSelectedBrush(); });
                    if (ImGui::MenuItem("Paste Brush", "Ctrl+V", false, clipboard.hasData))
                        PasteClipboardBrush(*m_Document);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Duplicate Brush", "Ctrl+D", false, hasBrushSelection))
                        ApplyMapEdit(*m_Document, "Duplicate Brush", [&]() { return m_Document->DuplicateSelectedBrush(); });
                    if (ImGui::MenuItem("Delete Brush", "Del", false, hasBrushSelection))
                        ApplyMapEdit(*m_Document, "Delete Brush", [&]() { return m_Document->DeleteSelectedBrush(); });
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::End();
}

} // namespace Dot
