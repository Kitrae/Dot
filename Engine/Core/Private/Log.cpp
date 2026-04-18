// =============================================================================
// Dot Engine - Logging System Implementation
// =============================================================================

#include "Core/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace Dot
{

// Static member initialization
std::vector<LogMessage> Log::s_Messages;
std::vector<Log::LogCallback> Log::s_Listeners;
std::mutex Log::s_Mutex;
std::string Log::s_LogFilePath;
bool Log::s_Initialized = false;

static std::string GetCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

static std::string ExtractFilename(const char* path)
{
    std::string fullPath(path);
    size_t pos = fullPath.find_last_of("\\/");
    return (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
}

void Log::Initialize(const std::string& logFilePath)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    s_LogFilePath = logFilePath;
    s_Initialized = true;
    s_Messages.clear();
    s_Messages.reserve(1000); // Pre-allocate for performance

    // Create/clear log file
    std::ofstream file(s_LogFilePath, std::ios::trunc);
    if (file.is_open())
    {
        file << "=== Dot Engine Log Started ===" << std::endl;
        file.close();
    }
}

void Log::Shutdown()
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    // Write final message to log file
    std::ofstream file(s_LogFilePath, std::ios::app);
    if (file.is_open())
    {
        file << "=== Dot Engine Log Ended ===" << std::endl;
        file.close();
    }

    s_Messages.clear();
    s_Listeners.clear();
    s_Initialized = false;
}

void Log::Flush()
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    if (s_LogFilePath.empty())
        return;

    std::ofstream file(s_LogFilePath, std::ios::app);
    if (file.is_open())
        file.flush();
}

void Log::Message(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    // Format message
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    LogMessage msg;
    msg.level = level;
    msg.message = buffer;
    msg.timestamp = GetCurrentTimestamp();
    msg.file = ExtractFilename(file);
    msg.line = line;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        // Store message
        s_Messages.push_back(msg);

        // Notify all listeners
        for (auto& callback : s_Listeners)
        {
            callback(msg);
        }

        // Write to file
        if (!s_LogFilePath.empty())
        {
            std::ofstream logFile(s_LogFilePath, std::ios::app);
            if (logFile.is_open())
            {
                logFile << "[" << msg.timestamp << "] "
                        << "[" << GetLevelName(level) << "] " << msg.message;

                if (level >= LogLevel::Warning)
                {
                    logFile << " (" << msg.file << ":" << msg.line << ")";
                }
                logFile << std::endl;

                // Flush on errors for crash safety
                if (level >= LogLevel::Error)
                {
                    logFile.flush();
                }
            }
        }

        // Console output with colors
#ifdef _WIN32
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

        switch (level)
        {
            case LogLevel::Trace:
                color = FOREGROUND_INTENSITY;
                break;
            case LogLevel::Info:
                color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                break;
            case LogLevel::Warning:
                color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                break;
            case LogLevel::Error:
                color = FOREGROUND_RED | FOREGROUND_INTENSITY;
                break;
            case LogLevel::Fatal:
                color = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                break;
        }

        SetConsoleTextAttribute(console, color);
        std::printf("[%s] [%s] %s\n", msg.timestamp.c_str(), GetLevelName(level), buffer);
        SetConsoleTextAttribute(console, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
        std::printf("[%s] [%s] %s\n", msg.timestamp.c_str(), GetLevelName(level), buffer);
#endif
    }

    // Fatal errors - show message box and abort
    if (level == LogLevel::Fatal)
    {
#ifdef _WIN32
        char errorMsg[5120];
        snprintf(errorMsg, sizeof(errorMsg), "Fatal Error:\n\n%s\n\nFile: %s\nLine: %d", buffer, file, line);
        MessageBoxA(NULL, errorMsg, "Dot Engine - Fatal Error", MB_OK | MB_ICONERROR);
#endif
        std::abort();
    }
}

void Log::AddListener(LogCallback callback)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Listeners.push_back(callback);
}

void Log::ClearListeners()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Listeners.clear();
}

const std::vector<LogMessage>& Log::GetMessages()
{
    return s_Messages;
}

void Log::ClearMessages()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Messages.clear();
}

const char* Log::GetLevelName(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
    }
    return "UNKNOWN";
}

uint32 Log::GetLevelColor(LogLevel level)
{
    // RGBA format for ImGui
    switch (level)
    {
        case LogLevel::Trace:
            return 0xFF888888; // Gray
        case LogLevel::Info:
            return 0xFFFFFFFF; // White
        case LogLevel::Warning:
            return 0xFF00FFFF; // Yellow (ABGR)
        case LogLevel::Error:
            return 0xFF0000FF; // Red (ABGR)
        case LogLevel::Fatal:
            return 0xFFFF00FF; // Magenta (ABGR)
    }
    return 0xFFFFFFFF;
}

const std::string& Log::GetLogFilePath()
{
    return s_LogFilePath;
}

} // namespace Dot
