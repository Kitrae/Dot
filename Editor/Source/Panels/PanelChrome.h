// =============================================================================
// Dot Engine - Shared Panel Chrome
// =============================================================================

#pragma once

#include <algorithm>

#include <imgui.h>

namespace Dot
{

inline void DrawPanelChromeForCurrentWindow()
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!drawList)
        return;

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    if (windowSize.x <= 4.0f || windowSize.y <= 4.0f)
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 innerMin(windowPos.x + 1.0f, windowPos.y + 1.0f);
    const ImVec2 innerMax(windowPos.x + windowSize.x - 1.0f, windowPos.y + windowSize.y - 1.0f);
    const float headerHeight = (std::min)(windowSize.y - 2.0f, ImGui::GetFrameHeight() + style.WindowPadding.y * 1.4f);
    const float glowHeight = (std::min)(windowSize.y - 2.0f, (std::max)(72.0f, windowSize.y * 0.38f));

    drawList->AddRectFilledMultiColor(innerMin, innerMax, IM_COL32(84, 94, 112, 26), IM_COL32(74, 84, 102, 22),
                                      IM_COL32(20, 22, 28, 4), IM_COL32(24, 26, 32, 6));
    drawList->AddRectFilledMultiColor(innerMin, ImVec2(innerMax.x, innerMin.y + glowHeight), IM_COL32(104, 118, 144, 30),
                                      IM_COL32(92, 106, 132, 24), IM_COL32(38, 42, 52, 0),
                                      IM_COL32(44, 48, 58, 0));
    drawList->AddRectFilledMultiColor(innerMin, ImVec2(innerMax.x, innerMin.y + headerHeight),
                                      IM_COL32(118, 132, 160, 20), IM_COL32(104, 118, 146, 16), IM_COL32(46, 50, 60, 0),
                                      IM_COL32(50, 54, 64, 0));

    const ImU32 topLineColor = IM_COL32(176, 196, 228, 44);
    const ImU32 headerLineColor = IM_COL32(255, 255, 255, 10);
    const ImU32 bottomLineColor = IM_COL32(0, 0, 0, 56);
    drawList->AddLine(ImVec2(innerMin.x, innerMin.y), ImVec2(innerMax.x, innerMin.y), topLineColor, 1.0f);
    drawList->AddLine(ImVec2(innerMin.x, innerMin.y + headerHeight), ImVec2(innerMax.x, innerMin.y + headerHeight),
                      headerLineColor, 1.0f);
    drawList->AddLine(ImVec2(innerMin.x, innerMax.y), ImVec2(innerMax.x, innerMax.y), bottomLineColor, 1.0f);
}

inline bool BeginChromeWindow(const char* name, bool* open, ImGuiWindowFlags flags = 0)
{
    const bool visible = ImGui::Begin(name, open, flags);
    DrawPanelChromeForCurrentWindow();
    return visible;
}

inline void EndChromeWindow()
{
    ImGui::End();
}

} // namespace Dot
