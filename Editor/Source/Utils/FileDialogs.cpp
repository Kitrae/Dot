// =============================================================================
// Dot Engine - File Dialogs Implementation (Windows)
// =============================================================================

#include "FileDialogs.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <commdlg.h>
    #include <shellapi.h>
#endif

#include <cstring>

namespace Dot
{

std::string FileDialogs::OpenFile(const FileFilter& filter)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);

    // Build filter string: "Description\0*.ext\0All Files\0*.*\0\0"
    // Adding All Files as a fallback option
    char filterStr[512] = {0};
    size_t descLen = std::strlen(filter.description);
    size_t extLen = std::strlen(filter.extension);
    size_t pos = 0;

    // First filter: user-specified
    std::memcpy(filterStr + pos, filter.description, descLen);
    pos += descLen + 1; // +1 for null terminator
    std::memcpy(filterStr + pos, filter.extension, extLen);
    pos += extLen + 1; // +1 for null terminator

    // Second filter: All Files (fallback)
    const char* allFilesDesc = "All Files";
    const char* allFilesExt = "*.*";
    std::memcpy(filterStr + pos, allFilesDesc, std::strlen(allFilesDesc));
    pos += std::strlen(allFilesDesc) + 1;
    std::memcpy(filterStr + pos, allFilesExt, std::strlen(allFilesExt));
    pos += std::strlen(allFilesExt) + 1;
    filterStr[pos] = '\0'; // Double null terminator

    ofn.lpstrFilter = filterStr;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE)
    {
        return std::string(szFile);
    }
#endif

    return "";
}

std::string FileDialogs::SaveFile(const FileFilter& filter, const std::string& defaultName)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = {0};

    // Copy default name if provided
    if (!defaultName.empty())
    {
        std::strncpy(szFile, defaultName.c_str(), sizeof(szFile) - 1);
    }

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);

    // Build filter string
    char filterStr[256] = {0};
    size_t descLen = std::strlen(filter.description);
    size_t extLen = std::strlen(filter.extension);
    std::memcpy(filterStr, filter.description, descLen);
    filterStr[descLen] = '\0';
    std::memcpy(filterStr + descLen + 1, filter.extension, extLen);
    filterStr[descLen + 1 + extLen] = '\0';
    filterStr[descLen + 1 + extLen + 1] = '\0';

    ofn.lpstrFilter = filterStr;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    // Default extension (without the *)
    std::string defExt;
    const char* extPtr = filter.extension;
    if (extPtr[0] == '*' && extPtr[1] == '.')
    {
        defExt = extPtr + 2;
        ofn.lpstrDefExt = defExt.c_str();
    }

    if (GetSaveFileNameA(&ofn) == TRUE)
    {
        return std::string(szFile);
    }
#endif

    return "";
}

} // namespace Dot
