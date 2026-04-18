// =============================================================================
// Dot Engine - Logger
// =============================================================================
// Flexible logging system with severity levels, multiple sinks, and
// compile-time stripping for shipping builds.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


namespace Dot
{

/// Log severity levels
enum class LogLevel : uint8
{
    Trace = 0, ///< Verbose debug info
    Debug,     ///< Debug information
    Info,      ///< General information
    Warn,      ///< Warnings
    Error,     ///< Errors
    Fatal,     ///< Fatal errors (may crash)
    Off        ///< Disable logging
};

/// Convert log level to string
inline const char* LogLevelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        default:
            return "???";
    }
}

/// Log sink interface - extend to create custom sinks
class DOT_CORE_API ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(LogLevel level, const char* category, const char* message) = 0;
    virtual void Flush() = 0;
};

/// Console sink - writes to stdout/stderr
class DOT_CORE_API ConsoleSink : public ILogSink
{
public:
    void Write(LogLevel level, const char* category, const char* message) override;
    void Flush() override;
};

/// File sink - writes to a log file
class DOT_CORE_API FileSink : public ILogSink
{
public:
    explicit FileSink(const char* filepath);
    ~FileSink() override;

    void Write(LogLevel level, const char* category, const char* message) override;
    void Flush() override;

private:
    std::FILE* m_File = nullptr;
};

/// Logger singleton
///
/// Thread-safe logging with support for multiple output sinks.
///
/// Usage:
/// @code
///     DOT_LOG_INFO("Engine", "Initializing...");
///     DOT_LOG_ERROR("Renderer", "Failed to create device: %s", errorMsg);
/// @endcode
class DOT_CORE_API Logger
{
public:
    static Logger& Get();

    /// Set minimum log level (messages below this are ignored)
    void SetLevel(LogLevel level) { m_MinLevel = level; }
    LogLevel GetLevel() const { return m_MinLevel; }

    /// Add a log sink
    void AddSink(std::shared_ptr<ILogSink> sink);

    /// Remove all sinks
    void ClearSinks();

    /// Log a message (prefer macros for source location)
    void Log(LogLevel level, const char* category, const char* file, int line, const char* fmt, ...);

    /// Flush all sinks
    void Flush();

    // Non-copyable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger() = default;

    std::mutex m_Mutex;
    std::vector<std::shared_ptr<ILogSink>> m_Sinks;
    LogLevel m_MinLevel = LogLevel::Trace;
};

// =============================================================================
// Logging Macros
// =============================================================================

#if DOT_DEBUG || DOT_PROFILE
    #define DOT_LOG(level, category, ...) ::Dot::Logger::Get().Log(level, category, __FILE__, __LINE__, __VA_ARGS__)

    #define DOT_LOG_TRACE(category, ...) DOT_LOG(::Dot::LogLevel::Trace, category, __VA_ARGS__)
    #define DOT_LOG_DEBUG(category, ...) DOT_LOG(::Dot::LogLevel::Debug, category, __VA_ARGS__)
    #define DOT_LOG_INFO(category, ...) DOT_LOG(::Dot::LogLevel::Info, category, __VA_ARGS__)
    #define DOT_LOG_WARN(category, ...) DOT_LOG(::Dot::LogLevel::Warn, category, __VA_ARGS__)
    #define DOT_LOG_ERROR(category, ...) DOT_LOG(::Dot::LogLevel::Error, category, __VA_ARGS__)
    #define DOT_LOG_FATAL(category, ...) DOT_LOG(::Dot::LogLevel::Fatal, category, __VA_ARGS__)
#else
    // Strip logs in Release builds (except errors/fatal)
    #define DOT_LOG(level, category, ...) ((void)0)
    #define DOT_LOG_TRACE(category, ...) ((void)0)
    #define DOT_LOG_DEBUG(category, ...) ((void)0)
    #define DOT_LOG_INFO(category, ...) ((void)0)
    #define DOT_LOG_WARN(category, ...) ((void)0)
    #define DOT_LOG_ERROR(category, ...)                                                                               \
        ::Dot::Logger::Get().Log(::Dot::LogLevel::Error, category, __FILE__, __LINE__, __VA_ARGS__)
    #define DOT_LOG_FATAL(category, ...)                                                                               \
        ::Dot::Logger::Get().Log(::Dot::LogLevel::Fatal, category, __FILE__, __LINE__, __VA_ARGS__)
#endif

} // namespace Dot
