#include <Core/Crash/CrashHandling.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace Dot
{

TEST(CrashHandlingTests, CrashMetadataRoundTripsThroughJsonFile)
{
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "dot_crash_metadata_test";
    std::filesystem::create_directories(tempDir);
    const std::filesystem::path metadataPath = tempDir / "crash.json";

    CrashMetadata written;
    written.appName = "DotGame";
    written.buildConfig = "Debug";
    written.timestampUtc = "2026-03-18T22-00-00Z";
    written.exceptionName = "ACCESS_VIOLATION";
    written.exceptionCode = "0xC0000005";
    written.exceptionAddress = "0x1234";
    written.accessType = "READ";
    written.accessAddress = "0x5678";
    written.executablePath = "C:/Game/DotGame.exe";
    written.workingDirectory = "C:/Game";
    written.commandLine = "\"C:/Game/DotGame.exe\" \"C:/Game/Game.dotproject\"";
    written.projectPath = "C:/Game/Game.dotproject";
    written.scenePath = "C:/Game/Assets/Scenes/Test.dotscene";
    written.relaunchExecutablePath = "C:/Game/DotGame.exe";
    written.relaunchArguments = "\"C:/Game/Game.dotproject\"";
    written.crashDirectory = "C:/Game/Saved/Crashes/2026-03-18T22-00-00Z_100";
    written.dumpPath = written.crashDirectory + "/crash.dmp";
    written.logPath = written.crashDirectory + "/crash.log";
    written.summaryPath = written.crashDirectory + "/summary.txt";
    written.stackSummary = "#00 TestFunction (Test.cpp:42)\n";

    ASSERT_TRUE(CrashHandling::WriteCrashMetadata(written, metadataPath));

    CrashMetadata loaded;
    ASSERT_TRUE(CrashHandling::LoadCrashMetadata(metadataPath, loaded));
    EXPECT_EQ(loaded.appName, written.appName);
    EXPECT_EQ(loaded.projectPath, written.projectPath);
    EXPECT_EQ(loaded.scenePath, written.scenePath);
    EXPECT_EQ(loaded.relaunchArguments, written.relaunchArguments);
    EXPECT_EQ(loaded.stackSummary, written.stackSummary);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST(CrashHandlingTests, HandledFatalErrorReportWritesCrashArtifacts)
{
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "dot_handled_crash_report_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir);

    CrashContext context;
    context.appName = "DotEditor";
    context.crashRootOverride = tempDir / "Saved" / "Crashes";
    context.projectPath = "C:/Projects/Test.dotproj";
    context.scenePath = "C:/Projects/Assets/Scenes/Test.dotscene";

    ASSERT_TRUE(CrashHandling::Install(context));

    CrashMetadata failure;
    failure.appName = "DotEditor";
    failure.buildConfig = "Debug";
    failure.timestampUtc = "2026-04-18T12-00-00Z";
    failure.exceptionName = "RENDERER_INITIALIZATION_FAILED";
    failure.exceptionCode = "RENDERER_INIT_FAILED";
    failure.exceptionAddress = "0x0";
    failure.stackSummary = "ViewportPanel::Initialize returned false.\n";

    CrashArtifacts artifacts;
    ASSERT_TRUE(CrashHandling::ReportHandledFatalError(failure, false, &artifacts));
    EXPECT_TRUE(std::filesystem::exists(artifacts.crashDirectory));
    EXPECT_TRUE(std::filesystem::exists(artifacts.metadataPath));
    EXPECT_TRUE(std::filesystem::exists(artifacts.summaryPath));
    EXPECT_TRUE(artifacts.dumpPath.empty());

    CrashMetadata loaded;
    ASSERT_TRUE(CrashHandling::LoadCrashMetadata(artifacts.metadataPath, loaded));
    EXPECT_EQ(loaded.exceptionName, "RENDERER_INITIALIZATION_FAILED");
    EXPECT_EQ(loaded.exceptionCode, "RENDERER_INIT_FAILED");
    EXPECT_EQ(loaded.projectPath, context.projectPath);
    EXPECT_EQ(loaded.scenePath, context.scenePath);
    EXPECT_EQ(loaded.stackSummary, failure.stackSummary);

    std::filesystem::remove_all(tempDir, ec);
}

} // namespace Dot
