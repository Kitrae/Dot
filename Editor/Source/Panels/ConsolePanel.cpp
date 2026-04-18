// =============================================================================
// Dot Engine - Console Panel Implementation
// =============================================================================

#include "ConsolePanel.h"
#include "PanelChrome.h"

#include <imgui.h>

namespace Dot
{

ConsolePanel::ConsolePanel() : EditorPanel("Console")
{
    // Copy existing messages
    m_Messages = Log::GetMessages();

    // Register listener for new messages
    Log::AddListener([this](const LogMessage& msg) { OnLogMessage(msg); });
}

ConsolePanel::~ConsolePanel()
{
    // Note: We can't easily remove just our listener, so we clear all on shutdown
    // This is fine since ConsolePanel lives for the entire app lifetime
}

void ConsolePanel::OnLogMessage(const LogMessage& msg)
{
    m_Messages.push_back(msg);
    if (m_AutoScroll)
    {
        m_ScrollToBottom = true;
    }
}

void ConsolePanel::Clear()
{
    m_Messages.clear();
    Log::ClearMessages();
}

void ConsolePanel::OnImGui()
{
    if (!m_Open)
        return;

    BeginChromeWindow(m_Name.c_str(), &m_Open, ImGuiWindowFlags_MenuBar);

    // Menu bar with options
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::Button("Clear"))
        {
            Clear();
        }

        ImGui::Separator();

        // Filter toggles
        ImGui::Checkbox("Trace", &m_ShowTrace);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warning", &m_ShowWarning);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError);

        ImGui::Separator();

        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

        ImGui::EndMenuBar();
    }

    // Message list
    ImGui::BeginChild("ScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& msg : m_Messages)
    {
        // Filter by level
        bool show = false;
        switch (msg.level)
        {
            case LogLevel::Trace:
                show = m_ShowTrace;
                break;
            case LogLevel::Info:
                show = m_ShowInfo;
                break;
            case LogLevel::Warning:
                show = m_ShowWarning;
                break;
            case LogLevel::Error:
            case LogLevel::Fatal:
                show = m_ShowError;
                break;
        }

        if (!show)
            continue;

        // Color based on level
        ImVec4 color;
        switch (msg.level)
        {
            case LogLevel::Trace:
                color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                break;
            case LogLevel::Info:
                color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                break;
            case LogLevel::Warning:
                color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                break;
            case LogLevel::Error:
                color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                break;
            case LogLevel::Fatal:
                color = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
                break;
        }

        // Format: [TIME] [LEVEL] Message
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted("[");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(msg.timestamp.c_str());
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("] [");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(Log::GetLevelName(msg.level));
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("] ");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(msg.message.c_str());

        // Show file:line for warnings and errors
        if (msg.level >= LogLevel::Warning)
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("(%s:%d)", msg.file.c_str(), msg.line);
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleColor();
    }

    // Auto-scroll to bottom
    if (m_ScrollToBottom)
    {
        ImGui::SetScrollHereY(1.0f);
        m_ScrollToBottom = false;
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Dot
