// =============================================================================
// Dot Engine - Editor Entry Point
// =============================================================================

#include "Application.h"

#include "Core/Crash/CrashHandling.h"
#include "Core/Log.h"

#ifdef DOT_PLATFORM_WINDOWS

namespace
{

std::filesystem::path GetExecutableDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path();
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    Dot::CrashContext crashContext;
    crashContext.appName = "DotEditor";
    crashContext.reporterExecutablePath = GetExecutableDirectory() / "DotCrashReporter.exe";
    Dot::CrashHandling::Install(crashContext);

    Dot::ApplicationConfig config;
    config.Title = L"Dot Engine Editor";
    config.Width = 1600;
    config.Height = 900;
    config.VSync = true;

    int exitCode = 1;
    {
        Dot::Application app(config);
        if (app.Initialize(hInstance))
            exitCode = app.Run();
    }

    Dot::Log::Shutdown();
    return exitCode;
}

#else

int main()
{
    return 1;
}

#endif
