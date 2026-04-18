// =============================================================================
// Dot Engine - Crash Handling
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <filesystem>
#include <string>

namespace Dot
{

struct CrashContext
{
    std::string appName;
    std::filesystem::path reporterExecutablePath;
    std::filesystem::path relaunchExecutablePath;
    std::string relaunchArguments;
    std::string projectPath;
    std::string scenePath;
    std::filesystem::path crashRootOverride;
    std::string logFilePath;
};

struct CrashArtifacts
{
    std::filesystem::path crashDirectory;
    std::filesystem::path metadataPath;
    std::filesystem::path dumpPath;
    std::filesystem::path logCopyPath;
    std::filesystem::path summaryPath;
};

struct CrashMetadata
{
    std::string appName;
    std::string buildConfig;
    std::string timestampUtc;
    std::string exceptionName;
    std::string exceptionCode;
    std::string exceptionAddress;
    std::string accessType;
    std::string accessAddress;
    std::string executablePath;
    std::string workingDirectory;
    std::string commandLine;
    std::string projectPath;
    std::string scenePath;
    std::string relaunchExecutablePath;
    std::string relaunchArguments;
    std::string crashDirectory;
    std::string dumpPath;
    std::string logPath;
    std::string summaryPath;
    std::string stackSummary;
};

namespace CrashHandling
{

DOT_CORE_API bool Install(const CrashContext& context);
DOT_CORE_API void SetProjectPath(const std::string& path);
DOT_CORE_API void SetScenePath(const std::string& path);
DOT_CORE_API void SetRelaunchArguments(const std::string& arguments);
DOT_CORE_API void SetReporterExecutablePath(const std::filesystem::path& path);

DOT_CORE_API bool WriteCrashMetadata(const CrashMetadata& metadata, const std::filesystem::path& path);
DOT_CORE_API bool LoadCrashMetadata(const std::filesystem::path& path, CrashMetadata& outMetadata);
DOT_CORE_API bool ReportHandledFatalError(const CrashMetadata& metadata, bool launchReporter = true,
                                          CrashArtifacts* outArtifacts = nullptr);

} // namespace CrashHandling

} // namespace Dot
