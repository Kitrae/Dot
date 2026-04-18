// =============================================================================
// Dot Engine - File Watcher
// =============================================================================
// Monitors a directory for file changes, used for hot-reloading scripts.
// Windows implementation using ReadDirectoryChangesW with overlapped I/O.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Dot
{

/// Callback invoked when a watched file is modified
using FileChangeCallback = std::function<void(const std::string& path)>;

/// File watcher for detecting script modifications (hot-reloading)
class DOT_CORE_API FileWatcher
{
public:
    FileWatcher();
    ~FileWatcher();

    // Non-copyable
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    /// Start watching a directory for changes
    /// @param directory The directory path to watch
    /// @param recursive Whether to watch subdirectories
    /// @return true if watching started successfully
    bool Start(const std::string& directory, bool recursive = true);

    /// Stop watching
    void Stop();

    /// Set the callback for file changes
    void SetCallback(FileChangeCallback callback) { m_Callback = callback; }

    /// Poll for pending file changes (call each frame from main thread)
    /// This processes queued changes and invokes callbacks
    void PollChanges();

    /// Check if watcher is active
    bool IsWatching() const { return m_Watching; }

    /// Get the watched directory
    const std::string& GetDirectory() const { return m_Directory; }

private:
    struct PendingChange
    {
        std::string path;
        double timestamp; // When the change was detected
    };

    void ProcessOverlappedResult();

    FileChangeCallback m_Callback;
    std::string m_Directory;
    bool m_Recursive = true;
    bool m_Watching = false;
    std::vector<PendingChange> m_PendingChanges;

    // Platform-specific implementation data (Windows handles)
    struct Impl;
    std::unique_ptr<Impl> m_Impl;

    // Debounce settings
    static constexpr double kDebounceTimeMs = 100.0; // Coalesce rapid saves
};

} // namespace Dot
