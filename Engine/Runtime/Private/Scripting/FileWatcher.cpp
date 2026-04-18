// =============================================================================
// Dot Engine - File Watcher Implementation
// =============================================================================
// Windows implementation using ReadDirectoryChangesW with overlapped I/O.
// =============================================================================

#include "Core/Scripting/FileWatcher.h"

#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <algorithm>
#include <chrono>


namespace Dot
{

// Platform-specific implementation data
struct FileWatcher::Impl
{
    HANDLE directoryHandle = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped = {};
    alignas(DWORD) char buffer[4096] = {};
    bool pendingRead = false;
};

// Get current time in milliseconds
static double GetTimeMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

FileWatcher::FileWatcher() : m_Impl(std::make_unique<Impl>()) {}

FileWatcher::~FileWatcher()
{
    Stop();
}

bool FileWatcher::Start(const std::string& directory, bool recursive)
{
    if (m_Watching)
        Stop();

    m_Directory = directory;
    m_Recursive = recursive;

    // Open directory for monitoring
    m_Impl->directoryHandle =
        CreateFileA(directory.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (m_Impl->directoryHandle == INVALID_HANDLE_VALUE)
    {
        DOT_LOG_ERROR("FileWatcher: Failed to open directory '{}' (error {})", directory, GetLastError());
        return false;
    }

    // Create event for overlapped I/O
    m_Impl->overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_Impl->overlapped.hEvent)
    {
        CloseHandle(m_Impl->directoryHandle);
        m_Impl->directoryHandle = INVALID_HANDLE_VALUE;
        DOT_LOG_ERROR("FileWatcher: Failed to create event (error {})", GetLastError());
        return false;
    }

    // Start first async read
    DWORD bytesReturned = 0;
    BOOL success = ReadDirectoryChangesW(
        m_Impl->directoryHandle, m_Impl->buffer, sizeof(m_Impl->buffer), m_Recursive ? TRUE : FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &bytesReturned, &m_Impl->overlapped, nullptr);

    if (!success)
    {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
        {
            CloseHandle(m_Impl->overlapped.hEvent);
            CloseHandle(m_Impl->directoryHandle);
            m_Impl->directoryHandle = INVALID_HANDLE_VALUE;
            DOT_LOG_ERROR("FileWatcher: ReadDirectoryChangesW failed (error {})", error);
            return false;
        }
    }

    m_Impl->pendingRead = true;
    m_Watching = true;

    DOT_LOG_INFO("FileWatcher: Started watching '{}'", directory);
    return true;
}

void FileWatcher::Stop()
{
    if (!m_Watching)
        return;

    if (m_Impl->overlapped.hEvent)
    {
        CancelIo(m_Impl->directoryHandle);
        CloseHandle(m_Impl->overlapped.hEvent);
        m_Impl->overlapped.hEvent = nullptr;
    }

    if (m_Impl->directoryHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_Impl->directoryHandle);
        m_Impl->directoryHandle = INVALID_HANDLE_VALUE;
    }

    m_Impl->pendingRead = false;
    m_Watching = false;
    m_PendingChanges.clear();

    DOT_LOG_INFO("FileWatcher: Stopped watching");
}

void FileWatcher::PollChanges()
{
    if (!m_Watching)
        return;

    // Check if overlapped operation completed
    ProcessOverlappedResult();

    // Process pending changes that have passed debounce time
    double now = GetTimeMs();
    auto it = m_PendingChanges.begin();
    while (it != m_PendingChanges.end())
    {
        if (now - it->timestamp >= kDebounceTimeMs)
        {
            // Fire callback
            if (m_Callback)
            {
                DOT_LOG_INFO("FileWatcher: File changed: {}", it->path);
                m_Callback(it->path);
            }
            it = m_PendingChanges.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void FileWatcher::ProcessOverlappedResult()
{
    if (!m_Impl->pendingRead)
        return;

    // Check if there's a result ready (non-blocking)
    DWORD bytesTransferred = 0;
    BOOL result = GetOverlappedResult(m_Impl->directoryHandle, &m_Impl->overlapped, &bytesTransferred,
                                      FALSE // Don't wait
    );

    if (!result)
    {
        DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE)
            return; // Still pending, check next frame

        DOT_LOG_ERROR("FileWatcher: GetOverlappedResult failed (error {})", error);
        Stop();
        return;
    }

    // Process the buffer
    if (bytesTransferred > 0)
    {
        double now = GetTimeMs();
        FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(m_Impl->buffer);

        while (true)
        {
            // Only handle modifications (not creates/deletes for now)
            if (info->Action == FILE_ACTION_MODIFIED)
            {
                // Convert wide string to narrow
                int nameLen = info->FileNameLength / sizeof(WCHAR);
                std::wstring wideName(info->FileName, nameLen);

                // Convert to UTF-8
                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), nameLen, nullptr, 0, nullptr, nullptr);
                std::string filename(utf8Len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), nameLen, filename.data(), utf8Len, nullptr, nullptr);

                // Only track .lua files
                if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".lua")
                {
                    std::string fullPath = m_Directory + "/" + filename;

                    // Replace backslashes with forward slashes for consistency
                    std::replace(fullPath.begin(), fullPath.end(), '\\', '/');

                    // Check if already pending (update timestamp to debounce)
                    bool found = false;
                    for (auto& pending : m_PendingChanges)
                    {
                        if (pending.path == fullPath)
                        {
                            pending.timestamp = now;
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        m_PendingChanges.push_back({fullPath, now});
                    }
                }
            }

            // Move to next entry
            if (info->NextEntryOffset == 0)
                break;
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<char*>(info) + info->NextEntryOffset);
        }
    }

    // Reset event and start next read
    ResetEvent(m_Impl->overlapped.hEvent);

    DWORD bytesReturned = 0;
    BOOL success = ReadDirectoryChangesW(
        m_Impl->directoryHandle, m_Impl->buffer, sizeof(m_Impl->buffer), m_Recursive ? TRUE : FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &bytesReturned, &m_Impl->overlapped, nullptr);

    if (!success && GetLastError() != ERROR_IO_PENDING)
    {
        DOT_LOG_ERROR("FileWatcher: Failed to restart directory watch (error {})", GetLastError());
        Stop();
    }
}

} // namespace Dot
