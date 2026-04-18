#include "RuntimeGameApp.h"

#include <Core/Crash/CrashHandling.h>

#include <shellapi.h>

namespace
{

std::filesystem::path GetExecutableDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path();
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    std::filesystem::path projectPathOverride;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        if (argc > 1)
            projectPathOverride = WideToUtf8(argv[1]);
        LocalFree(argv);
    }

    Dot::CrashContext crashContext;
    crashContext.appName = "DotGame";
    crashContext.reporterExecutablePath = GetExecutableDirectory() / "DotCrashReporter.exe";
    if (!projectPathOverride.empty())
        crashContext.relaunchArguments = "\"" + projectPathOverride.string() + "\"";
    Dot::CrashHandling::Install(crashContext);

    Dot::RuntimeGameApp app;
    if (!app.Initialize(hInstance, projectPathOverride))
        return 1;

    return app.Run();
}
