// =============================================================================
// Dot Engine - Scripting Workspace Implementation
// =============================================================================

#include "ScriptingWorkspace.h"

#include "Core/Log.h"
#include "Core/Scripting/ScriptRuntime.h"
#include "Core/Scripting/ScriptSystem.h"

#include "Panels/ConsolePanel.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <imgui.h>
#include <sstream>

namespace Dot
{

// Lua keywords for syntax highlighting
static const char* s_LuaKeywords[] = {"and",      "break",  "do",   "else", "elseif", "end",  "false", "for",
                                      "function", "goto",   "if",   "in",   "local",  "nil",  "not",   "or",
                                      "repeat",   "return", "then", "true", "until",  "while"};

// Lua built-in functions
static const char* s_LuaBuiltins[] = {"print",  "type",         "tostring",     "tonumber", "pairs",  "ipairs",
                                      "next",   "setmetatable", "getmetatable", "rawget",   "rawset", "rawequal",
                                      "select", "unpack",       "pack",         "error",    "assert", "pcall",
                                      "xpcall", "require",      "load",         "loadfile", "dofile"};

// Colors for syntax highlighting (VS Code-inspired dark theme)
static const ImU32 kColorKeyword = IM_COL32(197, 134, 192, 255); // Purple
static const ImU32 kColorString = IM_COL32(206, 145, 120, 255);  // Orange
static const ImU32 kColorComment = IM_COL32(106, 153, 85, 255);  // Green
static const ImU32 kColorNumber = IM_COL32(181, 206, 168, 255);  // Light green
static const ImU32 kColorBuiltin = IM_COL32(220, 220, 170, 255); // Yellow
static const ImU32 kColorDefault = IM_COL32(212, 212, 212, 255); // Light gray
static const ImU32 kColorLineNum = IM_COL32(133, 133, 133, 255); // Gray
static const ImU32 kColorBackground = IM_COL32(30, 30, 30, 255); // Dark

ScriptingWorkspace::ScriptingWorkspace() : Workspace("Scripting", WorkspaceType::Scripting)
{
}

ScriptingWorkspace::~ScriptingWorkspace() = default;

void ScriptingWorkspace::SetScriptsPath(const std::filesystem::path& path)
{
    m_ScriptsPath = path;
    RefreshFileTree();
}

void ScriptingWorkspace::OnActivate()
{
    RefreshFileTree();
    DOT_LOG_INFO("Scripting workspace activated");
}

void ScriptingWorkspace::OnDeactivate()
{
    DOT_LOG_INFO("Scripting workspace deactivated");
}

void ScriptingWorkspace::OnImGui()
{
    // Create a fullscreen window for the scripting workspace
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    if (ImGui::Begin("##ScriptingWorkspace", nullptr, windowFlags))
    {
        // Menu bar
        DrawMenuBar();

        // Main content area with splitters
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float codeEditorWidth = contentSize.x - m_FileBrowserWidth - 8.0f;
        float codeEditorHeight = contentSize.y - m_ConsoleHeight - 8.0f;

        // Left panel: File browser
        ImGui::BeginChild("##FileBrowser", ImVec2(m_FileBrowserWidth, contentSize.y - m_ConsoleHeight - 4.0f), true);
        DrawFileBrowser();
        ImGui::EndChild();

        // Splitter for file browser width
        ImGui::SameLine();
        ImGui::Button("##VSplitter", ImVec2(4.0f, contentSize.y - m_ConsoleHeight - 4.0f));
        if (ImGui::IsItemActive())
        {
            m_FileBrowserWidth += ImGui::GetIO().MouseDelta.x;
            m_FileBrowserWidth = std::clamp(m_FileBrowserWidth, 100.0f, 400.0f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine();

        // Right panel: Code editor
        ImGui::BeginGroup();
        {
            // Tab bar for open files
            DrawOpenFileTabs();

            // Debug toolbar with Validate, Run buttons
            DrawDebugToolbar();

            // Code editor
            ImGui::BeginChild("##CodeEditor", ImVec2(codeEditorWidth, codeEditorHeight - 60.0f), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            DrawCodeEditor();
            ImGui::EndChild();
        }
        ImGui::EndGroup();

        // Horizontal splitter for console height
        ImGui::Button("##HSplitter", ImVec2(contentSize.x, 4.0f));
        if (ImGui::IsItemActive())
        {
            m_ConsoleHeight -= ImGui::GetIO().MouseDelta.y;
            m_ConsoleHeight = std::clamp(m_ConsoleHeight, 50.0f, 400.0f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

        // Bottom panel: Console
        ImGui::BeginChild("##Console", ImVec2(contentSize.x, m_ConsoleHeight), true);
        DrawConsole();
        ImGui::EndChild();
    }
    ImGui::End();

    // Draw Save As popup modal (must be outside the main window)
    if (m_ShowSaveAsPopup)
    {
        ImGui::OpenPopup("Save Script As");
    }

    if (ImGui::BeginPopupModal("Save Script As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Save script to Assets/Scripts:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##Filename", m_SaveAsFilename, sizeof(m_SaveAsFilename));

        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100, 0)))
        {
            if (m_ActiveFileIndex >= 0 && m_ActiveFileIndex < static_cast<int>(m_OpenFiles.size()))
            {
                auto& file = m_OpenFiles[m_ActiveFileIndex];

                // Ensure .lua extension
                std::string fname = m_SaveAsFilename;
                if (fname.find(".lua") == std::string::npos)
                    fname += ".lua";

                // Create full path
                std::filesystem::path savePath = m_ScriptsPath / fname;

                // Create Scripts directory if needed
                if (!std::filesystem::exists(m_ScriptsPath))
                {
                    std::filesystem::create_directories(m_ScriptsPath);
                }

                std::ofstream ofs(savePath);
                if (ofs.is_open())
                {
                    std::string content = file.editor.GetText();
                    ofs << content;
                    file.path = savePath;
                    file.name = fname;
                    file.originalContent = content;
                    file.isDirty = false;
                    m_ConsoleMessages.push_back("[INFO] Saved: " + savePath.string());
                    RefreshFileTree();
                }
                else
                {
                    m_ConsoleMessages.push_back("[ERROR] Could not save: " + savePath.string());
                }
            }

            m_ShowSaveAsPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 0)))
        {
            m_ShowSaveAsPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Handle Ctrl+Space for autocomplete
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        m_ShowAutoComplete = true;
        memset(m_AutoCompleteFilter, 0, sizeof(m_AutoCompleteFilter));
        m_SelectedAutoComplete = 0;
    }

    // Show autocomplete popup
    if (m_ShowAutoComplete)
    {
        ShowAutoCompletePopup();
    }

    ImGui::PopStyleVar(3);
}

void ScriptingWorkspace::DrawMenuBar()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Script", "Ctrl+N"))
            {
                NewFile();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, m_ActiveFileIndex >= 0))
            {
                SaveCurrentFile();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, m_ActiveFileIndex >= 0))
            {
                SaveCurrentFileAs();
            }
            if (ImGui::MenuItem("Reload", nullptr, false,
                                m_ActiveFileIndex >= 0 && !m_OpenFiles[m_ActiveFileIndex].path.empty()))
            {
                if (m_ActiveFileIndex >= 0)
                {
                    OpenFile(m_OpenFiles[m_ActiveFileIndex].path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh File Tree"))
            {
                RefreshFileTree();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            ImGui::Checkbox("Show Line Numbers", &m_ShowLineNumbers);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Script"))
        {
            if (ImGui::MenuItem("Run Script", "F5", false, m_ActiveFileIndex >= 0))
            {
                // TODO: Execute current script
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Keyboard shortcuts
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
    {
        NewFile();
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        if (ImGui::GetIO().KeyShift)
            SaveCurrentFileAs();
        else
            SaveCurrentFile();
    }
}

void ScriptingWorkspace::DrawFileBrowser()
{
    // New file button at top
    if (ImGui::Button("+ New Script", ImVec2(-1, 0)))
    {
        NewFile();
    }
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Scripts");
    ImGui::Separator();

    if (m_FileTree.empty())
    {
        ImGui::TextDisabled("No scripts found");
        ImGui::TextDisabled("Click + New Script");
        ImGui::TextDisabled("to create one.");
        return;
    }

    // Recursive lambda to draw file tree
    std::function<void(std::vector<FileEntry>&)> drawTree = [&](std::vector<FileEntry>& entries)
    {
        for (auto& entry : entries)
        {
            if (entry.isDirectory)
            {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
                if (entry.children.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf;

                bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags);
                if (open)
                {
                    drawTree(entry.children);
                    ImGui::TreePop();
                }
            }
            else
            {
                // Check if this file is currently open
                bool isOpen = false;
                for (const auto& of : m_OpenFiles)
                {
                    if (of.path == entry.path)
                    {
                        isOpen = true;
                        break;
                    }
                }

                if (isOpen)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));

                if (ImGui::Selectable(entry.name.c_str()))
                {
                    OpenFile(entry.path);
                }

                if (isOpen)
                    ImGui::PopStyleColor();
            }
        }
    };

    drawTree(m_FileTree);
}

void ScriptingWorkspace::DrawOpenFileTabs()
{
    if (m_OpenFiles.empty())
    {
        ImGui::TextDisabled("No files open. Click a file in the browser to open it.");
        return;
    }

    if (ImGui::BeginTabBar("##OpenFiles", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
    {
        for (int i = 0; i < static_cast<int>(m_OpenFiles.size()); ++i)
        {
            auto& file = m_OpenFiles[i];
            std::string tabLabel = file.name;
            if (file.isDirty)
                tabLabel += " *";

            bool open = true;
            // Don't force SetSelected - let ImGui handle tab selection naturally
            if (ImGui::BeginTabItem((tabLabel + "###Tab" + std::to_string(i)).c_str(), &open))
            {
                // This tab is now active
                m_ActiveFileIndex = i;
                ImGui::EndTabItem();
            }

            if (!open)
            {
                CloseFile(i);
                --i; // Adjust index after removal
            }
        }
        ImGui::EndTabBar();
    }
}

void ScriptingWorkspace::DrawCodeEditor()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
    {
        ImGui::TextDisabled("Select a file to edit");
        return;
    }

    auto& file = m_OpenFiles[m_ActiveFileIndex];

    // Render the TextEditor widget with Lua syntax highlighting
    file.editor.Render("##CodeEditor", ImGui::GetContentRegionAvail(), false);

    // Track if the text has changed for dirty state
    if (file.editor.IsTextChanged())
    {
        file.isDirty = (file.editor.GetText() != file.originalContent);
    }
}

void ScriptingWorkspace::DrawConsole()
{
    // If we have a shared ConsolePanel, use it
    if (m_ConsolePanel)
    {
        m_ConsolePanel->OnImGui();
        return;
    }

    // Fallback to internal console
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Console");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
    if (ImGui::SmallButton("Clear"))
    {
        m_ConsoleMessages.clear();
    }
    ImGui::Separator();

    ImGui::BeginChild("##ConsoleOutput", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& msg : m_ConsoleMessages)
    {
        ImGui::TextUnformatted(msg.c_str());
    }
    if (m_ConsoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void ScriptingWorkspace::NewFile()
{
    // Create a new untitled file
    static int untitledCount = 1;

    OpenedFile newFile;
    newFile.path = ""; // Empty path indicates unsaved file
    newFile.name = "Untitled" + std::to_string(untitledCount++) + ".lua";

    // Configure TextEditor with Lua syntax highlighting
    newFile.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    newFile.editor.SetPalette(TextEditor::GetDarkPalette());

    std::string defaultContent =
        "-- " + newFile.name +
        "\n-- New Lua script\n\nfunction OnStart()\n    -- Called when the script "
        "starts\nend\n\nfunction OnUpdate(dt)\n    -- Called every frame\nend\n\nfunction OnDestroy()\n  "
        "  -- Called when the script is destroyed\nend\n";
    newFile.editor.SetText(defaultContent);
    newFile.originalContent = "";
    newFile.isDirty = true;

    m_OpenFiles.push_back(std::move(newFile));
    m_ActiveFileIndex = static_cast<int>(m_OpenFiles.size()) - 1;

    m_ConsoleMessages.push_back("[INFO] Created new script");
}

void ScriptingWorkspace::OpenFile(const std::filesystem::path& path)
{
    // Check if already open
    for (int i = 0; i < static_cast<int>(m_OpenFiles.size()); ++i)
    {
        if (m_OpenFiles[i].path == path)
        {
            m_ActiveFileIndex = i;
            return;
        }
    }

    // Load file
    std::ifstream file(path);
    if (!file.is_open())
    {
        m_ConsoleMessages.push_back("[ERROR] Could not open: " + path.string());
        return;
    }

    OpenedFile of;
    of.path = path;
    of.name = path.filename().string();

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Configure TextEditor with Lua syntax highlighting
    of.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    of.editor.SetPalette(TextEditor::GetDarkPalette());
    of.editor.SetText(content);
    of.originalContent = content;
    of.isDirty = false;

    m_OpenFiles.push_back(std::move(of));
    m_ActiveFileIndex = static_cast<int>(m_OpenFiles.size()) - 1;

    m_ConsoleMessages.push_back("[INFO] Opened: " + path.filename().string());
}

bool ScriptingWorkspace::OpenScriptAsset(const std::filesystem::path& path)
{
    OpenFile(path);
    return m_ActiveFileIndex >= 0 && m_ActiveFileIndex < static_cast<int>(m_OpenFiles.size()) &&
           m_OpenFiles[m_ActiveFileIndex].path == path;
}

void ScriptingWorkspace::SaveCurrentFile()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        return;

    auto& file = m_OpenFiles[m_ActiveFileIndex];

    // If it's a new file without a path, use Save As
    if (file.path.empty())
    {
        SaveCurrentFileAs();
        return;
    }

    std::ofstream ofs(file.path);
    if (!ofs.is_open())
    {
        m_ConsoleMessages.push_back("[ERROR] Could not save: " + file.path.string());
        return;
    }

    std::string content = file.editor.GetText();
    ofs << content;
    file.originalContent = content;
    file.isDirty = false;

    m_ConsoleMessages.push_back("[INFO] Saved: " + file.name);

    // Trigger hot reload if in play mode
    // The FileWatcher will handle this automatically
}

bool ScriptingWorkspace::SaveActiveFile()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        return false;

    if (m_OpenFiles[m_ActiveFileIndex].path.empty())
        return false;

    SaveCurrentFile();
    return !m_OpenFiles[m_ActiveFileIndex].isDirty;
}

bool ScriptingWorkspace::HasActiveFile() const
{
    return m_ActiveFileIndex >= 0 && m_ActiveFileIndex < static_cast<int>(m_OpenFiles.size());
}

const std::filesystem::path& ScriptingWorkspace::GetActiveFilePath() const
{
    static const std::filesystem::path kEmptyPath;
    return HasActiveFile() ? m_OpenFiles[m_ActiveFileIndex].path : kEmptyPath;
}

bool ScriptingWorkspace::IsActiveFileDirty() const
{
    return HasActiveFile() && m_OpenFiles[m_ActiveFileIndex].isDirty;
}

void ScriptingWorkspace::DrawDebugToolbar()
{
    bool hasActiveFile = m_ActiveFileIndex >= 0 && m_ActiveFileIndex < static_cast<int>(m_OpenFiles.size());

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));

    // Validate syntax button
    ImGui::BeginDisabled(!hasActiveFile || !m_ScriptSystem);
    if (ImGui::Button("Validate"))
    {
        ValidateCurrentScript();
    }
    if (ImGui::IsItemHovered() && hasActiveFile)
        ImGui::SetTooltip("Check script for syntax errors (does not execute)");
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Run selection button
    ImGui::BeginDisabled(!hasActiveFile || !m_ScriptSystem || !m_OpenFiles[m_ActiveFileIndex].editor.HasSelection());
    if (ImGui::Button("Run Selection"))
    {
        RunSelection();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Execute the selected code (F5)");
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Run script button
    ImGui::BeginDisabled(!hasActiveFile || !m_ScriptSystem);
    if (ImGui::Button("Run Script"))
    {
        RunCurrentScript();
    }
    if (ImGui::IsItemHovered() && hasActiveFile)
        ImGui::SetTooltip("Execute the entire script (F6)");
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Clear errors button
    ImGui::BeginDisabled(!hasActiveFile);
    if (ImGui::Button("Clear Errors"))
    {
        if (hasActiveFile)
        {
            m_OpenFiles[m_ActiveFileIndex].editor.SetErrorMarkers({});
        }
    }
    ImGui::EndDisabled();

    ImGui::PopStyleColor(3);

    // Keyboard shortcuts
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        if (hasActiveFile && m_ScriptSystem)
            RunSelection();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F6))
    {
        if (hasActiveFile && m_ScriptSystem)
            RunCurrentScript();
    }
}

void ScriptingWorkspace::ValidateCurrentScript()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        return;
    if (!m_ScriptSystem || !m_ScriptSystem->GetRuntime())
        return;

    auto& file = m_OpenFiles[m_ActiveFileIndex];
    std::string code = file.editor.GetText();

    // Use ScriptRuntime's ValidateString to check syntax without executing
    auto* runtime = m_ScriptSystem->GetRuntime();
    bool isValid = runtime->ValidateString(code);

    TextEditor::ErrorMarkers markers;
    if (!isValid)
    {
        std::string errorMsg = runtime->GetLastError();

        // Parse error message for line number (Lua format: [string "..."]:LINE: message)
        int lineNum = 1;
        std::string cleanError = errorMsg;

        // Try to extract line number from error
        size_t colonPos = errorMsg.find(':');
        if (colonPos != std::string::npos)
        {
            size_t secondColon = errorMsg.find(':', colonPos + 1);
            if (secondColon != std::string::npos)
            {
                std::string lineStr = errorMsg.substr(colonPos + 1, secondColon - colonPos - 1);
                try
                {
                    lineNum = std::stoi(lineStr);
                }
                catch (...)
                {
                    lineNum = 1;
                }
                cleanError = errorMsg.substr(secondColon + 1);
            }
        }

        markers[lineNum] = cleanError;
        file.editor.SetErrorMarkers(markers);

        m_ConsoleMessages.push_back("[ERROR] Syntax error at line " + std::to_string(lineNum) + ": " + cleanError);
    }
    else
    {
        file.editor.SetErrorMarkers({}); // Clear errors
        m_ConsoleMessages.push_back("[OK] Syntax check passed - no errors found");
    }
}

void ScriptingWorkspace::RunSelection()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        return;
    if (!m_ScriptSystem || !m_ScriptSystem->GetRuntime())
        return;

    auto& file = m_OpenFiles[m_ActiveFileIndex];
    std::string selectedText = file.editor.GetSelectedText();

    if (selectedText.empty())
    {
        m_ConsoleMessages.push_back("[WARN] No code selected");
        return;
    }

    m_ConsoleMessages.push_back("[RUN] Executing selection...");

    auto& runtime = *m_ScriptSystem->GetRuntime();
    bool success = runtime.ExecuteString(selectedText);

    if (!success)
    {
        m_ConsoleMessages.push_back("[ERROR] " + runtime.GetLastError());
    }
}

void ScriptingWorkspace::RunCurrentScript()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        return;
    if (!m_ScriptSystem || !m_ScriptSystem->GetRuntime())
        return;

    auto& file = m_OpenFiles[m_ActiveFileIndex];
    std::string code = file.editor.GetText();

    m_ConsoleMessages.push_back("[RUN] Executing script: " + file.name);

    auto& runtime = *m_ScriptSystem->GetRuntime();
    bool success = runtime.ExecuteString(code);

    if (success)
    {
        m_ConsoleMessages.push_back("[OK] Script executed successfully");
    }
    else
    {
        std::string error = runtime.GetLastError();
        m_ConsoleMessages.push_back("[ERROR] " + error);

        // Try to set error marker
        TextEditor::ErrorMarkers markers;
        int lineNum = 1;
        size_t colonPos = error.find(':');
        if (colonPos != std::string::npos)
        {
            size_t secondColon = error.find(':', colonPos + 1);
            if (secondColon != std::string::npos)
            {
                std::string lineStr = error.substr(colonPos + 1, secondColon - colonPos - 1);
                try
                {
                    lineNum = std::stoi(lineStr);
                }
                catch (...)
                {
                }
            }
        }
        markers[lineNum] = error;
        file.editor.SetErrorMarkers(markers);
    }
}

void ScriptingWorkspace::SaveCurrentFileAs()
{
    if (m_ActiveFileIndex < 0 || m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        return;

    auto& file = m_OpenFiles[m_ActiveFileIndex];

    // Set up the filename with current file name
    if (!file.name.empty() && file.name.find("Untitled") == std::string::npos)
    {
        strncpy(m_SaveAsFilename, file.name.c_str(), sizeof(m_SaveAsFilename) - 1);
    }
    else
    {
        strncpy(m_SaveAsFilename, "new_script.lua", sizeof(m_SaveAsFilename) - 1);
    }

    m_ShowSaveAsPopup = true;
}

void ScriptingWorkspace::CloseFile(int index)
{
    if (index < 0 || index >= static_cast<int>(m_OpenFiles.size()))
        return;

    // TODO: Prompt to save if dirty
    m_OpenFiles.erase(m_OpenFiles.begin() + index);

    if (m_ActiveFileIndex >= static_cast<int>(m_OpenFiles.size()))
        m_ActiveFileIndex = static_cast<int>(m_OpenFiles.size()) - 1;
}

void ScriptingWorkspace::DrawHighlightedLine(const std::string& line, int lineNum)
{
    (void)lineNum; // Reserved for future use (e.g., error markers)

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float charWidth = ImGui::CalcTextSize("A").x; // Monospace assumed
    float xOffset = 0.0f;

    size_t i = 0;
    bool inComment = false;
    bool inString = false;
    char stringChar = 0;

    while (i < line.size())
    {
        // Check for comment start
        if (!inString && !inComment && i + 1 < line.size() && line[i] == '-' && line[i + 1] == '-')
        {
            // Draw rest of line as comment
            std::string rest = line.substr(i);
            drawList->AddText(ImVec2(pos.x + xOffset, pos.y), kColorComment, rest.c_str());
            break;
        }

        // Check for string start/end
        if (!inComment && (line[i] == '"' || line[i] == '\''))
        {
            if (inString && line[i] == stringChar)
            {
                // End of string (include the closing quote)
                std::string token(1, line[i]);
                drawList->AddText(ImVec2(pos.x + xOffset, pos.y), kColorString, token.c_str());
                xOffset += charWidth;
                inString = false;
                i++;
                continue;
            }
            else if (!inString)
            {
                // Start of string
                inString = true;
                stringChar = line[i];
            }
        }

        if (inString)
        {
            // Draw character as string
            std::string token(1, line[i]);
            drawList->AddText(ImVec2(pos.x + xOffset, pos.y), kColorString, token.c_str());
            xOffset += charWidth;
            i++;
            continue;
        }

        // Check for number
        if (std::isdigit(line[i]) || (line[i] == '.' && i + 1 < line.size() && std::isdigit(line[i + 1])))
        {
            size_t numStart = i;
            while (i < line.size() && (std::isdigit(line[i]) || line[i] == '.' || line[i] == 'e' || line[i] == 'E'))
                i++;
            std::string numStr = line.substr(numStart, i - numStart);
            drawList->AddText(ImVec2(pos.x + xOffset, pos.y), kColorNumber, numStr.c_str());
            xOffset += charWidth * numStr.size();
            continue;
        }

        // Check for identifier (keyword, builtin, or regular)
        if (std::isalpha(line[i]) || line[i] == '_')
        {
            size_t wordStart = i;
            while (i < line.size() && (std::isalnum(line[i]) || line[i] == '_'))
                i++;
            std::string word = line.substr(wordStart, i - wordStart);

            ImU32 color = kColorDefault;
            if (IsLuaKeyword(word))
                color = kColorKeyword;
            else if (IsLuaBuiltin(word))
                color = kColorBuiltin;

            drawList->AddText(ImVec2(pos.x + xOffset, pos.y), color, word.c_str());
            xOffset += charWidth * word.size();
            continue;
        }

        // Default: draw single character
        std::string token(1, line[i]);
        drawList->AddText(ImVec2(pos.x + xOffset, pos.y), kColorDefault, token.c_str());
        xOffset += charWidth;
        i++;
    }
}

void ScriptingWorkspace::RefreshFileTree()
{
    m_FileTree.clear();

    if (m_ScriptsPath.empty() || !std::filesystem::exists(m_ScriptsPath))
        return;

    // Recursive lambda to build file tree
    std::function<void(const std::filesystem::path&, std::vector<FileEntry>&)> buildTree =
        [&](const std::filesystem::path& dir, std::vector<FileEntry>& entries)
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            FileEntry fe;
            fe.path = entry.path();
            fe.name = entry.path().filename().string();
            fe.isDirectory = entry.is_directory();
            fe.isExpanded = false;

            if (fe.isDirectory)
            {
                buildTree(entry.path(), fe.children);
                // Only add if has .lua files
                if (!fe.children.empty())
                    entries.push_back(std::move(fe));
            }
            else if (entry.path().extension() == ".lua")
            {
                entries.push_back(std::move(fe));
            }
        }

        // Sort: directories first, then alphabetical
        std::sort(entries.begin(), entries.end(),
                  [](const FileEntry& a, const FileEntry& b)
                  {
                      if (a.isDirectory != b.isDirectory)
                          return a.isDirectory > b.isDirectory;
                      return a.name < b.name;
                  });
    };

    buildTree(m_ScriptsPath, m_FileTree);
}

bool ScriptingWorkspace::IsLuaKeyword(const std::string& word) const
{
    for (const char* kw : s_LuaKeywords)
    {
        if (word == kw)
            return true;
    }
    return false;
}

bool ScriptingWorkspace::IsLuaBuiltin(const std::string& word) const
{
    for (const char* bi : s_LuaBuiltins)
    {
        if (word == bi)
            return true;
    }
    return false;
}

void ScriptingWorkspace::ShowAutoCompletePopup()
{
    // Filter entries
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

    // Position popup
    ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##ScriptAutoComplete", &m_ShowAutoComplete, flags))
    {
        ImGui::Text("API Reference (Ctrl+Space)");
        ImGui::Separator();

        // Filter input
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##filter", m_AutoCompleteFilter, sizeof(m_AutoCompleteFilter));
        ImGui::SameLine();
        ImGui::TextDisabled("Type to filter, Esc to close");

        ImGui::Separator();

        // Left: List of suggestions
        ImGui::BeginChild("##suggestions", ImVec2(220, -1), true);
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
            else if (entry->category == "Navigation")
                color = ImVec4(0.5f, 0.9f, 0.7f, 1.0f);
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
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right: Documentation panel
        ImGui::BeginChild("##docs", ImVec2(-1, -1), true);
        if (!m_FilteredEntries.empty())
        {
            auto* entry = m_FilteredEntries[m_SelectedAutoComplete];

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", entry->keyword.c_str());
            if (!entry->signature.empty())
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", entry->signature.c_str());
            }
            ImGui::Separator();
            ImGui::TextWrapped("%s", entry->description.c_str());
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", entry->category.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();

    // Handle Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_ShowAutoComplete = false;
}

} // namespace Dot
