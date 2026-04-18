// =============================================================================
// Dot Engine - Text Editor Panel
// =============================================================================
// Built-in text editor for editing text files.
// =============================================================================

#pragma once

#include "EditorPanel.h"
#include "Scripting/LuaAutoComplete.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Dot
{

/// Simple text editor panel
class TextEditorPanel : public EditorPanel
{
public:
    TextEditorPanel();
    ~TextEditorPanel() override = default;

    void OnImGui() override;

    /// Open a file for editing
    void OpenFile(const std::filesystem::path& path);

    /// Close the current file
    void CloseFile();

    /// Check if a file is currently open
    bool HasOpenFile() const { return !m_FilePath.empty(); }

    /// Get the currently open file path
    const std::filesystem::path& GetFilePath() const { return m_FilePath; }

private:
    void LoadFile();
    void SaveFile();
    void ShowAutoComplete();
    std::string GetWordBeforeCursor();

    // File state
    std::filesystem::path m_FilePath;
    std::string m_Content;
    std::string m_OriginalContent;
    bool m_IsDirty = false;

    // Editor state
    bool m_ShowSaveConfirmation = false;
    bool m_WrapText = true;
    bool m_ShowLineNumbers = true;

    // Autocomplete state
    std::vector<const LuaAutoCompleteEntry*> m_FilteredEntries;
    bool m_ShowAutoComplete = false;
    int m_SelectedAutoComplete = 0;
    char m_AutoCompleteFilter[256] = {};
};

} // namespace Dot
