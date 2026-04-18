// =============================================================================
// Dot Engine - DotMcpServer bootstrap wrapper
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

std::filesystem::path GetExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = 0;
    while (true)
    {
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            break;
        buffer.resize(buffer.size() * 2);
    }

    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::filesystem::path GetScriptPath()
{
    const std::filesystem::path executablePath = GetExecutablePath();
    if (executablePath.empty())
        return {};

    // build_fresh/bin/Debug/DotMcpServer.exe -> repo root
    const std::filesystem::path repoRoot = executablePath.parent_path().parent_path().parent_path().parent_path();
    return repoRoot / "Tools" / "DotMcpServer" / "server.mjs";
}

std::filesystem::path GetLogPath()
{
    return GetExecutablePath().parent_path() / "DotMcpServer.wrapper.log";
}

void AppendLog(const std::string& message)
{
    std::ofstream file(GetLogPath(), std::ios::app);
    if (!file.is_open())
        return;
    file << message << '\n';
}

std::wstring Quote(const std::filesystem::path& path)
{
    return L"\"" + path.wstring() + L"\"";
}

std::filesystem::path ResolveNodeExecutable()
{
    const std::filesystem::path explicitPath = L"C:\\Program Files\\nodejs\\node.exe";
    if (std::filesystem::exists(explicitPath))
        return explicitPath;

    wchar_t buffer[MAX_PATH] = {};
    const DWORD result = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, buffer, nullptr);
    if (result == 0 || result >= MAX_PATH)
        return {};

    return std::filesystem::path(buffer);
}

HANDLE DuplicateForChild(HANDLE handle)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return nullptr;

    HANDLE duplicated = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(),
                         handle,
                         GetCurrentProcess(),
                         &duplicated,
                         0,
                         TRUE,
                         DUPLICATE_SAME_ACCESS))
    {
        return nullptr;
    }

    return duplicated;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const std::filesystem::path nodePath = ResolveNodeExecutable();
    const std::filesystem::path scriptPath = GetScriptPath();
    AppendLog("wrapper startup");
    if (nodePath.empty() || !std::filesystem::exists(scriptPath))
    {
        AppendLog("Failed to resolve node.exe or server.mjs");
        return 1;
    }

    std::wstring commandLine = Quote(nodePath) + L" " + Quote(scriptPath);
    for (int index = 1; index < argc; ++index)
    {
        commandLine += L" ";
        commandLine += Quote(argv[index]);
    }
    AppendLog("wrapper launching node child");

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    HANDLE childStdIn = DuplicateForChild(GetStdHandle(STD_INPUT_HANDLE));
    HANDLE childStdOut = DuplicateForChild(GetStdHandle(STD_OUTPUT_HANDLE));
    HANDLE childStdErr = DuplicateForChild(GetStdHandle(STD_ERROR_HANDLE));
    startupInfo.hStdInput = childStdIn ? childStdIn : GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = childStdOut ? childStdOut : GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = childStdErr ? childStdErr : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION processInfo{};
    std::wstring mutableCommandLine = commandLine;
    const std::filesystem::path workingDirectory = scriptPath.parent_path();
    const BOOL created = CreateProcessW(nodePath.c_str(),
                                        mutableCommandLine.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        0,
                                        nullptr,
                                        workingDirectory.c_str(),
                                        &startupInfo,
                                        &processInfo);
    if (!created)
    {
        AppendLog("CreateProcessW(node server.mjs) failed");
        if (childStdIn)
            CloseHandle(childStdIn);
        if (childStdOut)
            CloseHandle(childStdOut);
        if (childStdErr)
            CloseHandle(childStdErr);
        return static_cast<int>(GetLastError());
    }

    if (childStdIn)
        CloseHandle(childStdIn);
    if (childStdOut)
        CloseHandle(childStdOut);
    if (childStdErr)
        CloseHandle(childStdErr);

    AppendLog("wrapper child created");
    CloseHandle(processInfo.hThread);
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    AppendLog("wrapper child exited");
    return static_cast<int>(exitCode);
}
