// =============================================================================
// Dot Engine - Settings Panel Implementation
// =============================================================================

#include "SettingsPanel.h"
#include "PanelChrome.h"

#include "../Settings/ProjectSettingsMetadata.h"
#include "../Settings/ProjectSettingsSections.h"
#include "../Settings/ProjectSettingsStorage.h"

#include <imgui.h>
#include <string>

namespace Dot
{

bool SettingsPanel::s_IsOpen = false;
SettingsPanel::Category SettingsPanel::s_SelectedCategory = SettingsPanel::Category::Editor;

namespace
{

char g_SearchBuffer[128] = {};
bool g_SettingsDirtyThisFrame = false;

void DrawCategoryButton(const char* label, SettingsPanel::Category category)
{
    const bool isSelected = (SettingsPanel::GetSelectedCategory() == category);
    if (isSelected)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.33f, 0.47f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.19f, 0.24f, 1.0f));

    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.38f, 0.53f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.43f, 0.60f, 1.0f));

    if (ImGui::Button(label, ImVec2(-1.0f, 34.0f)))
        SettingsPanel::SetSelectedCategory(category);

    ImGui::PopStyleColor(3);
}

} // namespace

void SettingsPanel::Open(Category category)
{
    s_SelectedCategory = category;
    g_SearchBuffer[0] = '\0';
    s_IsOpen = true;
}

void SettingsPanel::OnImGui()
{
    if (!s_IsOpen)
        return;

    g_SettingsDirtyThisFrame = false;

    ImGui::SetNextWindowSize(ImVec2(980.0f, 720.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(760.0f, 520.0f), ImVec2(1400.0f, 1000.0f));

    if (!BeginChromeWindow("Project Settings", &s_IsOpen, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ProjectSettingsSearch", "Search settings...", g_SearchBuffer, sizeof(g_SearchBuffer));
    ImGui::Separator();

    const std::string searchQuery = g_SearchBuffer;
    const bool hasSearch = !searchQuery.empty();
    const float sidebarWidth = 190.0f;
    const auto& categories = ProjectSettingsMetadata::GetCategories();

    ImGui::BeginChild("SettingsSidebar", ImVec2(sidebarWidth, 0.0f), true);
    ImGui::TextDisabled("PROJECT");
    ImGui::Spacing();

    bool drewAnyCategory = false;
    for (SettingsPanel::Category category : categories)
    {
        if (!ProjectSettingsMetadata::MatchesCategory(category, searchQuery))
            continue;

        DrawCategoryButton(ProjectSettingsMetadata::GetSidebarLabel(category), category);
        drewAnyCategory = true;
    }

    if (!drewAnyCategory)
        ImGui::TextDisabled("No matches");

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("SettingsContent", ImVec2(0.0f, 0.0f), false);
    if (hasSearch)
    {
        bool drewAnySection = false;
        for (size_t i = 0; i < categories.size(); ++i)
        {
            const SettingsPanel::Category category = categories[i];
            if (!ProjectSettingsMetadata::MatchesCategory(category, searchQuery))
                continue;

            ImGui::PushID(static_cast<int>(i));
            const bool drewCategory =
                ProjectSettingsSections::DrawCategoryWithTitle(category, searchQuery, g_SettingsDirtyThisFrame);
            ImGui::PopID();

            if (!drewCategory)
                continue;

            drewAnySection = true;
            if (i + 1 < categories.size())
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }
        }

        if (!drewAnySection)
            ImGui::TextDisabled("No settings match \"%s\".", g_SearchBuffer);
    }
    else
    {
        ProjectSettingsSections::DrawCategoryWithTitle(s_SelectedCategory, searchQuery, g_SettingsDirtyThisFrame);
    }
    ImGui::EndChild();

    ImGui::End();

    if (g_SettingsDirtyThisFrame)
        ProjectSettingsStorage::Save();
}

SettingsPanel::Category SettingsPanel::GetSelectedCategory()
{
    return s_SelectedCategory;
}

void SettingsPanel::SetSelectedCategory(Category category)
{
    s_SelectedCategory = category;
}

} // namespace Dot
