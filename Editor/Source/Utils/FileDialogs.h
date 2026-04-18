// =============================================================================
// Dot Engine - File Dialogs
// =============================================================================
// Platform-specific file open/save dialogs.
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

/// File filter for dialogs
struct FileFilter
{
    const char* description; // e.g. "Scene Files"
    const char* extension;   // e.g. "*.dotscene"
};

/// Platform-specific file dialog utilities
class FileDialogs
{
public:
    /// Opens a file dialog for opening a file
    /// @param filter File type filter
    /// @return Selected file path, or empty string if cancelled
    static std::string OpenFile(const FileFilter& filter);

    /// Opens a file dialog for saving a file
    /// @param filter File type filter
    /// @param defaultName Default filename
    /// @return Selected file path, or empty string if cancelled
    static std::string SaveFile(const FileFilter& filter, const std::string& defaultName = "");
};

} // namespace Dot
