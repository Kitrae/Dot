// =============================================================================
// Dot Engine - Scripting Workspace
// =============================================================================
// Full-screen script editor workspace with file browser, code editor, console.
// =============================================================================

#pragma once

#include "Workspace.h"
#include "Scripting/LuaAutoComplete.h"

#include <TextEditor.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Dot
{

// Forward declarations
class ScriptSystem;
class ConsolePanel;

/// Scripting Workspace - full-screen script editor
class ScriptingWorkspace : public Workspace
{
public:
    ScriptingWorkspace();
    ~ScriptingWorkspace() override;

    void OnImGui() override;
    void OnActivate() override;
    void OnDeactivate() override;

    /// Set the script system for hot reload integration
    void SetScriptSystem(ScriptSystem* scriptSystem) { m_ScriptSystem = scriptSystem; }

    /// Set the console panel reference for displaying script output
    void SetConsolePanel(ConsolePanel* console) { m_ConsolePanel = console; }

    /// Set the scripts base path
    void SetScriptsPath(const std::filesystem::path& path);
    bool OpenScriptAsset(const std::filesystem::path& path);
    bool SaveActiveFile();
    bool HasActiveFile() const;
    const std::filesystem::path& GetActiveFilePath() const;
    bool IsActiveFileDirty() const;

private:
    // UI Drawing
    void DrawMenuBar();
    void DrawFileBrowser();
    void DrawCodeEditor();
    void DrawConsole();
    void DrawOpenFileTabs();
    void DrawDebugToolbar();

    // File operations
    void NewFile();
    void OpenFile(const std::filesystem::path& path);
    void SaveCurrentFile();
    void SaveCurrentFileAs();
    void CloseFile(int index);
    void RefreshFileTree();

    // Debug operations
    void ValidateCurrentScript();
    void RunSelection();
    void RunCurrentScript();

    // Syntax highlighting (kept for fallback)
    void DrawHighlightedLine(const std::string& line, int lineNum);
    bool IsLuaKeyword(const std::string& word) const;
    bool IsLuaBuiltin(const std::string& word) const;

    // Script system reference (not owned)
    ScriptSystem* m_ScriptSystem = nullptr;

    // Console panel reference (not owned)
    ConsolePanel* m_ConsolePanel = nullptr;

    // File browser state
    std::filesystem::path m_ScriptsPath;
    struct FileEntry
    {
        std::filesystem::path path;
        std::string name;
        bool isDirectory;
        bool isExpanded;
        std::vector<FileEntry> children;
    };
    std::vector<FileEntry> m_FileTree;

    // Open files (tabs)
    struct OpenedFile
    {
        std::filesystem::path path;
        std::string name;
        std::string originalContent;
        bool isDirty;
        TextEditor editor; // Syntax-highlighted editor widget
    };
    std::vector<OpenedFile> m_OpenFiles;
    int m_ActiveFileIndex = -1;

    // Editor state
    int m_CursorLine = 0;
    int m_CursorCol = 0;
    float m_ScrollY = 0.0f;
    bool m_ShowLineNumbers = true;

    // Console state
    std::vector<std::string> m_ConsoleMessages;
    bool m_ConsoleAutoScroll = true;

    // UI sizes
    float m_FileBrowserWidth = 200.0f;
    float m_ConsoleHeight = 150.0f;

    // Save As dialog state
    bool m_ShowSaveAsPopup = false;
    char m_SaveAsFilename[256] = "new_script.lua";

    // Autocomplete
    std::vector<const LuaAutoCompleteEntry*> m_FilteredEntries;
    bool m_ShowAutoComplete = false;
    int m_SelectedAutoComplete = 0;
    char m_AutoCompleteFilter[256] = {};

    void ShowAutoCompletePopup();
};

} // namespace Dot
