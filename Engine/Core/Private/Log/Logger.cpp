// =============================================================================
// Dot Engine - Logger Implementation
// =============================================================================

#include "Core/Log/Logger.h"

#include <chrono>
#include <ctime>


namespace Dot
{

// =============================================================================
// Console Sink
// =============================================================================

void ConsoleSink::Write(LogLevel level, const char* category, const char* message)
{
    // Use stderr for warnings and above
    std::FILE* stream = (level >= LogLevel::Warn) ? stderr : stdout;

    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#if DOT_PLATFORM_WINDOWS
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    // Format: [HH:MM:SS.mmm] [LEVEL] [Category] Message
    std::fprintf(stream, "[%02d:%02d:%02d.%03d] [%-5s] [%s] %s\n", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                 static_cast<int>(ms.count()), LogLevelToString(level), category, message);
}

void ConsoleSink::Flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}

// =============================================================================
// File Sink
// =============================================================================

FileSink::FileSink(const char* filepath)
{
#if DOT_PLATFORM_WINDOWS
    fopen_s(&m_File, filepath, "w");
#else
    m_File = std::fopen(filepath, "w");
#endif

    if (m_File)
    {
        // Write header
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::fprintf(m_File, "=== Dot Engine Log ===\n");
        std::fprintf(m_File, "Started: %s\n", std::ctime(&time));
    }
}

FileSink::~FileSink()
{
    if (m_File)
    {
        std::fclose(m_File);
        m_File = nullptr;
    }
}

void FileSink::Write(LogLevel level, const char* category, const char* message)
{
    if (!m_File)
    {
        return;
    }

    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#if DOT_PLATFORM_WINDOWS
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::fprintf(m_File, "[%02d:%02d:%02d.%03d] [%-5s] [%s] %s\n", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                 static_cast<int>(ms.count()), LogLevelToString(level), category, message);
}

void FileSink::Flush()
{
    if (m_File)
    {
        std::fflush(m_File);
    }
}

// =============================================================================
// Logger
// =============================================================================

Logger& Logger::Get()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
{
    // Add default console sink
    AddSink(std::make_shared<ConsoleSink>());
}

void Logger::AddSink(std::shared_ptr<ILogSink> sink)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Sinks.push_back(std::move(sink));
}

void Logger::ClearSinks()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Sinks.clear();
}

void Logger::Log(LogLevel level, const char* category, [[maybe_unused]] const char* file, [[maybe_unused]] int line,
                 const char* fmt, ...)
{
    if (level < m_MinLevel)
    {
        return;
    }

    // Format message
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Write to all sinks
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (auto& sink : m_Sinks)
    {
        sink->Write(level, category, buffer);
    }

    // Fatal logs trigger a debug break
    if (level == LogLevel::Fatal)
    {
        DOT_DEBUGBREAK();
    }
}

void Logger::Flush()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (auto& sink : m_Sinks)
    {
        sink->Flush();
    }
}

} // namespace Dot
