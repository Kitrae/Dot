// =============================================================================
// Dot Engine - Text Editor Panel Implementation
// =============================================================================

#include "TextEditorPanel.h"
#include "PanelChrome.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <sstream>

namespace Dot
{

TextEditorPanel::TextEditorPanel() : EditorPanel("Text Editor")
{
    m_Open = false; // Start closed, opens when a file is opened
}

void TextEditorPanel::OpenFile(const std::filesystem::path& path)
{
    // If we have unsaved changes, prompt to save first
    if (m_IsDirty)
    {
        m_ShowSaveConfirmation = true;
        return;
    }

    m_FilePath = path;
    m_Name = "Text Editor - " + path.filename().string();
    LoadFile();
    m_Open = true;
}

void TextEditorPanel::CloseFile()
{
    m_FilePath.clear();
    m_Content.clear();
    m_OriginalContent.clear();
    m_IsDirty = false;
    m_Name = "Text Editor";
    m_Open = false;
}

void TextEditorPanel::LoadFile()
{
    std::ifstream file(m_FilePath);
    if (!file.is_open())
    {
        m_Content = "Error: Could not open file";
        m_OriginalContent = m_Content;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    m_Content = buffer.str();
    m_OriginalContent = m_Content;
    m_IsDirty = false;
}

void TextEditorPanel::SaveFile()
{
    std::ofstream file(m_FilePath);
    if (!file.is_open())
    {
        // TODO: Show error
        return;
    }

    file << m_Content;
    m_OriginalContent = m_Content;
    m_IsDirty = false;
}

void TextEditorPanel::OnImGui()
{
    if (!m_Open)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar;
    if (m_IsDirty)
    {
        // Show * in title when dirty
        std::string title = m_Name + " *###TextEditor";
        BeginChromeWindow(title.c_str(), &m_Open, flags);
    }
    else
    {
        BeginChromeWindow((m_Name + "###TextEditor").c_str(), &m_Open, flags);
    }

    // Menu bar
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save", "Ctrl+S", false, m_IsDirty))
            {
                SaveFile();
            }
            if (ImGui::MenuItem("Reload"))
            {
                LoadFile();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close"))
            {
                if (m_IsDirty)
                    m_ShowSaveConfirmation = true;
                else
                    CloseFile();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            ImGui::Checkbox("Word Wrap", &m_WrapText);
            ImGui::Checkbox("Line Numbers", &m_ShowLineNumbers);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Keyboard shortcuts
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        SaveFile();
    }

    // Ctrl+Space to open autocomplete
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        m_ShowAutoComplete = true;
        memset(m_AutoCompleteFilter, 0, sizeof(m_AutoCompleteFilter));
        m_SelectedAutoComplete = 0;
    }

    // Text editor area
    ImGuiInputTextFlags textFlags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;

    ImVec2 size = ImGui::GetContentRegionAvail();

    // Reserve space for the text buffer (grow as needed)
    static size_t bufferSize = 1024 * 64; // 64KB initial
    if (m_Content.size() + 1024 > bufferSize)
    {
        bufferSize = m_Content.size() + 1024 * 16;
    }
    m_Content.resize(bufferSize);

    // Store cursor position for autocomplete popup
    ImVec2 editorPos = ImGui::GetCursorScreenPos();

    if (ImGui::InputTextMultiline("##editor", m_Content.data(), bufferSize, size, textFlags))
    {
        // Trim to actual content length
        size_t actualLen = strlen(m_Content.c_str());
        m_Content.resize(actualLen);

        // Check if content changed
        m_IsDirty = (m_Content != m_OriginalContent);
    }
    else
    {
        // Always trim after editing
        size_t actualLen = strlen(m_Content.c_str());
        m_Content.resize(actualLen);
    }

    // Show autocomplete popup
    if (m_ShowAutoComplete)
    {
        ShowAutoComplete();
    }

    ImGui::End();

    // Save confirmation dialog
    if (m_ShowSaveConfirmation)
    {
        ImGui::OpenPopup("Save Changes?");
    }
    if (ImGui::BeginPopupModal("Save Changes?", &m_ShowSaveConfirmation, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("You have unsaved changes. Save before closing?");
        ImGui::Separator();

        if (ImGui::Button("Save"))
        {
            SaveFile();
            CloseFile();
            m_ShowSaveConfirmation = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save"))
        {
            m_IsDirty = false;
            CloseFile();
            m_ShowSaveConfirmation = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_ShowSaveConfirmation = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::string TextEditorPanel::GetWordBeforeCursor()
{
    // Since ImGui multiline doesn't expose cursor pos easily,
    // we'll use the current filter or return empty
    return m_AutoCompleteFilter;
}

void TextEditorPanel::ShowAutoComplete()
{
    // Filter entries based on current filter
    m_FilteredEntries.clear();

    std::string filterLower = m_AutoCompleteFilter;
    for (char& c : filterLower)
        c = static_cast<char>(tolower(c));

    bool filterEmpty = (m_AutoCompleteFilter[0] == '\0');

    for (const auto& entry : GetLuaAutoCompleteEntries())
    {
        if (!IsLuaAutoCompleteEntryVisible(entry))
            continue;

        std::string keywordLower = entry.keyword;
        for (char& c : keywordLower)
            c = static_cast<char>(tolower(c));

        // Match if filter is empty or keyword contains filter
        if (filterEmpty || keywordLower.find(filterLower) != std::string::npos)
        {
            m_FilteredEntries.push_back(&entry);
        }
    }

    if (m_FilteredEntries.empty())
    {
        m_ShowAutoComplete = false;
        return;
    }

    // Clamp selection
    if (m_SelectedAutoComplete >= static_cast<int>(m_FilteredEntries.size()))
        m_SelectedAutoComplete = static_cast<int>(m_FilteredEntries.size()) - 1;
    if (m_SelectedAutoComplete < 0)
        m_SelectedAutoComplete = 0;

    // Position popup near cursor
    ImVec2 popupPos = ImGui::GetMousePos();
    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(450, 300), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##autocomplete", &m_ShowAutoComplete, flags))
    {
        // Filter input
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##filter", &m_AutoCompleteFilter[0], 256, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            // Insert selected on Enter
            if (!m_FilteredEntries.empty())
            {
                // TODO: Insert text at cursor position
                m_ShowAutoComplete = false;
            }
        }

        // Auto-focus filter on first open
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere(-1);

        ImGui::Separator();

        // List of suggestions
        ImGui::BeginChild("##suggestions", ImVec2(200, -1), true);
        for (int i = 0; i < static_cast<int>(m_FilteredEntries.size()); i++)
        {
            auto* entry = m_FilteredEntries[i];
            bool isSelected = (i == m_SelectedAutoComplete);

            // Color by category
            ImVec4 color = ImVec4(1, 1, 1, 1);
            if (entry->category == "Input")
                color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
            else if (entry->category == "Physics")
                color = ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
            else if (entry->category == "Timer")
                color = ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
            else if (entry->category == "self")
                color = ImVec4(1.0f, 0.9f, 0.5f, 1.0f);
            else if (entry->category == "Lua")
                color = ImVec4(0.7f, 0.7f, 0.9f, 1.0f);
            else if (entry->category == "Lifecycle")
                color = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (ImGui::Selectable(entry->keyword.c_str(), isSelected))
            {
                m_SelectedAutoComplete = i;
            }
            ImGui::PopStyleColor();

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Documentation panel
        ImGui::BeginChild("##docs", ImVec2(-1, -1), true);
        if (!m_FilteredEntries.empty())
        {
            auto* entry = m_FilteredEntries[m_SelectedAutoComplete];

            // Keyword
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", entry->keyword.c_str());

            // Signature
            if (!entry->signature.empty())
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", entry->signature.c_str());
            }

            ImGui::Separator();

            // Description
            ImGui::TextWrapped("%s", entry->description.c_str());

            ImGui::Separator();

            // Category badge
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", entry->category.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();

    // Handle keyboard navigation
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        m_SelectedAutoComplete--;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        m_SelectedAutoComplete++;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_ShowAutoComplete = false;
}

} // namespace Dot
