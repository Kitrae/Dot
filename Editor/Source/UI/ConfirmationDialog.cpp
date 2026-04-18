// =============================================================================
// Dot Engine - Reusable Confirmation Dialog Implementation
// =============================================================================

#include "ConfirmationDialog.h"

#include <imgui.h>

namespace Dot
{

void ConfirmationDialog::Open(std::string title, std::string message, Button first, Button second, Button third)
{
    m_Title = std::move(title);
    m_Message = std::move(message);
    m_Buttons = {std::move(first), std::move(second), std::move(third)};
    m_QueuedOpen = true;
    m_IsOpen = true;
}

void ConfirmationDialog::Draw()
{
    if (m_Title.empty())
        return;

    if (m_QueuedOpen)
    {
        ImGui::OpenPopup(m_Title.c_str());
        m_QueuedOpen = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(m_Title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(400.0f);
        ImGui::TextUnformatted(m_Message.c_str());
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        constexpr float kButtonWidth = 110.0f;
        const float totalWidth = kButtonWidth * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        const float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
        if (startX > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        for (size_t i = 0; i < m_Buttons.size(); ++i)
        {
            Button& button = m_Buttons[i];

            if (button.primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.38f, 0.62f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.47f, 0.75f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.33f, 0.54f, 1.0f));
            }
            else if (button.danger)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.43f, 0.20f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.25f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.37f, 0.17f, 0.17f, 1.0f));
            }

            if (ImGui::Button(button.label.c_str(), ImVec2(kButtonWidth, 0.0f)))
            {
                bool shouldClose = true;
                if (button.onClick)
                    shouldClose = button.onClick();

                if (shouldClose)
                {
                    ImGui::CloseCurrentPopup();
                    m_IsOpen = false;
                }
            }

            if (button.primary || button.danger)
                ImGui::PopStyleColor(3);

            if (i + 1 < m_Buttons.size())
                ImGui::SameLine();
        }

        ImGui::EndPopup();
    }
    else if (m_IsOpen && !ImGui::IsPopupOpen(m_Title.c_str()))
    {
        m_IsOpen = false;
    }
}

} // namespace Dot
