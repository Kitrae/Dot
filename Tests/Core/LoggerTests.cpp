// =============================================================================
// Dot Engine - Logger Unit Tests
// =============================================================================

#include "Core/Log/Logger.h"

#include <gtest/gtest.h>
#include <sstream>

using namespace Dot;

// =============================================================================
// Log Level Tests
// =============================================================================

TEST(LogLevelTests, LogLevelToString)
{
    EXPECT_STREQ(LogLevelToString(LogLevel::Trace), "TRACE");
    EXPECT_STREQ(LogLevelToString(LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(LogLevelToString(LogLevel::Info), "INFO");
    EXPECT_STREQ(LogLevelToString(LogLevel::Warn), "WARN");
    EXPECT_STREQ(LogLevelToString(LogLevel::Error), "ERROR");
    EXPECT_STREQ(LogLevelToString(LogLevel::Fatal), "FATAL");
}

TEST(LogLevelTests, LogLevelOrdering)
{
    // Verify log levels are ordered correctly
    EXPECT_LT(static_cast<int>(LogLevel::Trace), static_cast<int>(LogLevel::Debug));
    EXPECT_LT(static_cast<int>(LogLevel::Debug), static_cast<int>(LogLevel::Info));
    EXPECT_LT(static_cast<int>(LogLevel::Info), static_cast<int>(LogLevel::Warn));
    EXPECT_LT(static_cast<int>(LogLevel::Warn), static_cast<int>(LogLevel::Error));
    EXPECT_LT(static_cast<int>(LogLevel::Error), static_cast<int>(LogLevel::Fatal));
}

// =============================================================================
// Capture Sink (for testing)
// =============================================================================

class CaptureSink : public ILogSink
{
public:
    void Write(LogLevel level, const char* category, const char* message) override
    {
        m_LastLevel = level;
        m_LastCategory = category;
        m_LastMessage = message;
        m_WriteCount++;
    }

    void Flush() override {}

    LogLevel m_LastLevel = LogLevel::Off;
    std::string m_LastCategory;
    std::string m_LastMessage;
    int m_WriteCount = 0;
};

// =============================================================================
// Logger Tests
// =============================================================================

class LoggerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Clear default sinks and add capture sink
        Logger::Get().ClearSinks();
        m_CaptureSink = std::make_shared<CaptureSink>();
        Logger::Get().AddSink(m_CaptureSink);
        Logger::Get().SetLevel(LogLevel::Trace);
    }

    void TearDown() override
    {
        // Restore default console sink
        Logger::Get().ClearSinks();
        Logger::Get().AddSink(std::make_shared<ConsoleSink>());
    }

    std::shared_ptr<CaptureSink> m_CaptureSink;
};

TEST_F(LoggerTests, BasicLogging)
{
    DOT_LOG_INFO("Test", "Hello World");

    EXPECT_EQ(m_CaptureSink->m_LastLevel, LogLevel::Info);
    EXPECT_EQ(m_CaptureSink->m_LastCategory, "Test");
    EXPECT_EQ(m_CaptureSink->m_LastMessage, "Hello World");
}

TEST_F(LoggerTests, FormattedLogging)
{
    DOT_LOG_INFO("Test", "Value: %d, Name: %s", 42, "foo");

    EXPECT_EQ(m_CaptureSink->m_LastMessage, "Value: 42, Name: foo");
}

TEST_F(LoggerTests, LogLevelFiltering)
{
    Logger::Get().SetLevel(LogLevel::Warn);

    DOT_LOG_INFO("Test", "Should be filtered");
    EXPECT_EQ(m_CaptureSink->m_WriteCount, 0);

    DOT_LOG_WARN("Test", "Should pass");
    EXPECT_EQ(m_CaptureSink->m_WriteCount, 1);

    DOT_LOG_ERROR("Test", "Should also pass");
    EXPECT_EQ(m_CaptureSink->m_WriteCount, 2);
}

TEST_F(LoggerTests, AllLogLevels)
{
    DOT_LOG_TRACE("Test", "Trace message");
    EXPECT_EQ(m_CaptureSink->m_LastLevel, LogLevel::Trace);

    DOT_LOG_DEBUG("Test", "Debug message");
    EXPECT_EQ(m_CaptureSink->m_LastLevel, LogLevel::Debug);

    DOT_LOG_INFO("Test", "Info message");
    EXPECT_EQ(m_CaptureSink->m_LastLevel, LogLevel::Info);

    DOT_LOG_WARN("Test", "Warn message");
    EXPECT_EQ(m_CaptureSink->m_LastLevel, LogLevel::Warn);

    DOT_LOG_ERROR("Test", "Error message");
    EXPECT_EQ(m_CaptureSink->m_LastLevel, LogLevel::Error);
}

TEST_F(LoggerTests, MultipleSinks)
{
    auto sink2 = std::make_shared<CaptureSink>();
    Logger::Get().AddSink(sink2);

    DOT_LOG_INFO("Test", "Broadcast");

    EXPECT_EQ(m_CaptureSink->m_WriteCount, 1);
    EXPECT_EQ(sink2->m_WriteCount, 1);
}

TEST_F(LoggerTests, DifferentCategories)
{
    DOT_LOG_INFO("Renderer", "Drawing...");
    EXPECT_EQ(m_CaptureSink->m_LastCategory, "Renderer");

    DOT_LOG_INFO("Physics", "Simulating...");
    EXPECT_EQ(m_CaptureSink->m_LastCategory, "Physics");

    DOT_LOG_INFO("Audio", "Playing...");
    EXPECT_EQ(m_CaptureSink->m_LastCategory, "Audio");
}
