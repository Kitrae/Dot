// =============================================================================
// Dot Engine - Logging System
// =============================================================================
// Engine-wide logging with levels, console output, and file output.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <cstdarg>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Dot
{

/// Log severity level
enum class LogLevel : uint8
{
    Trace,   // Detailed debug information
    Info,    // General information
    Warning, // Potential issues
    Error,   // Errors that don't stop execution
    Fatal    // Critical errors that will terminate
};

/// Log message structure
struct LogMessage
{
    LogLevel level;
    std::string message;
    std::string timestamp;
    std::string file;
    int line;
};

/// Logging system
class DOT_CORE_API Log
{
public:
    /// Callback for log message listeners (e.g., Console Panel)
    using LogCallback = std::function<void(const LogMessage&)>;

    /// Initialize the logging system
    static void Initialize(const std::string& logFilePath = "DotEngine.log");

    /// Shutdown and flush all logs
    static void Shutdown();

    /// Flush the current log file to disk if possible
    static void Flush();

    /// Log a message
    static void Message(LogLevel level, const char* file, int line, const char* fmt, ...);

    /// Add a listener for log messages
    static void AddListener(LogCallback callback);

    /// Clear all listeners
    static void ClearListeners();

    /// Get all logged messages (for Console Panel initialization)
    static const std::vector<LogMessage>& GetMessages();

    /// Clear message history
    static void ClearMessages();

    /// Get level name as string
    static const char* GetLevelName(LogLevel level);

    /// Get level color for ImGui (RGBA)
    static uint32 GetLevelColor(LogLevel level);

    /// Get the active log file path
    static const std::string& GetLogFilePath();

private:
    static std::vector<LogMessage> s_Messages;
    static std::vector<LogCallback> s_Listeners;
    static std::mutex s_Mutex;
    static std::string s_LogFilePath;
    static bool s_Initialized;
};

} // namespace Dot

// =============================================================================
// Logging Macros - use these for logging
// =============================================================================

#define DOT_LOG_TRACE(...) ::Dot::Log::Message(::Dot::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define DOT_LOG_INFO(...) ::Dot::Log::Message(::Dot::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define DOT_LOG_WARN(...) ::Dot::Log::Message(::Dot::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define DOT_LOG_ERROR(...) ::Dot::Log::Message(::Dot::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define DOT_LOG_FATAL(...) ::Dot::Log::Message(::Dot::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)
