// =============================================================================
// Dot Engine - Shared Crash Handling
// =============================================================================

#include "Core/Crash/CrashHandling.h"

#include "Core/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <DbgHelp.h>

    #pragma comment(lib, "dbghelp.lib")
#endif

namespace Dot
{

namespace
{

std::mutex g_CrashMutex;
CrashContext g_CrashContext;
bool g_CrashInstalled = false;

#ifdef _WIN32

std::string EscapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value)
    {
        switch (ch)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string UnescapeJson(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\\' && i + 1 < value.size())
        {
            ++i;
            switch (value[i])
            {
                case '\\':
                    result += '\\';
                    break;
                case '"':
                    result += '"';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += value[i];
                    break;
            }
        }
        else
        {
            result += value[i];
        }
    }
    return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size);
    return result;
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

std::filesystem::path GetCurrentExecutablePath()
{
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer);
}

std::string GetBuildConfigString()
{
#if defined(DOT_DEBUG)
    return "Debug";
#elif defined(DOT_PROFILE)
    return "Profile";
#elif defined(DOT_RELEASE)
    return "Release";
#else
    return "Unknown";
#endif
}

std::string GetCurrentTimestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime{};
    gmtime_s(&utcTime, &nowTime);

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H-%M-%SZ");
    return stream.str();
}

std::string GetExceptionCodeName(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:
            return "BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            return "FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:
            return "FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:
            return "FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:
            return "FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:
            return "FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:
            return "INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:
            return "INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return "NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:
            return "SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:
            return "STACK_OVERFLOW";
        default:
            return "UNKNOWN_EXCEPTION";
    }
}

std::string ToHexString(uint64_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

void EnsureSymbolsInitialized()
{
    static bool s_Attempted = false;
    static bool s_Initialized = false;
    if (s_Attempted)
        return;

    s_Attempted = true;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    s_Initialized = SymInitialize(process, nullptr, TRUE) == TRUE;
    (void)s_Initialized;
}

std::string DescribeAddress(void* address)
{
    EnsureSymbolsInitialized();

    HANDLE process = GetCurrentProcess();
    DWORD64 displacement = 0;
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    const DWORD64 rawAddress = reinterpret_cast<DWORD64>(address);
    std::ostringstream stream;
    if (SymFromAddr(process, rawAddress, &displacement, symbol) == TRUE)
    {
        stream << symbol->Name << " + " << ToHexString(static_cast<uint64_t>(displacement));

        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof(lineInfo);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, rawAddress, &lineDisplacement, &lineInfo) == TRUE)
            stream << " (" << lineInfo.FileName << ":" << lineInfo.LineNumber << ")";
    }
    else
    {
        stream << ToHexString(rawAddress);
    }
    return stream.str();
}

std::string CaptureStackSummary(CONTEXT* context)
{
    if (!context)
        return {};

    EnsureSymbolsInitialized();

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    STACKFRAME64 stackFrame = {};
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    std::ostringstream stream;
    for (int frameIndex = 0; frameIndex < 24; ++frameIndex)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
        {
            break;
        }

        if (stackFrame.AddrPC.Offset == 0)
            break;

        DWORD64 displacement = 0;
        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        stream << "#" << std::setw(2) << std::setfill('0') << frameIndex << " ";
        if (SymFromAddr(process, stackFrame.AddrPC.Offset, &displacement, symbol) == TRUE)
        {
            stream << symbol->Name << " + " << ToHexString(static_cast<uint64_t>(displacement));

            IMAGEHLP_LINE64 lineInfo = {};
            lineInfo.SizeOfStruct = sizeof(lineInfo);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, stackFrame.AddrPC.Offset, &lineDisplacement, &lineInfo) == TRUE)
                stream << " (" << lineInfo.FileName << ":" << lineInfo.LineNumber << ")";
        }
        else
        {
            stream << ToHexString(static_cast<uint64_t>(stackFrame.AddrPC.Offset));
        }
        stream << '\n';
    }

    return stream.str();
}

std::filesystem::path ResolveCrashRoot(const CrashContext& context)
{
    if (!context.crashRootOverride.empty())
        return context.crashRootOverride;
    return GetCurrentExecutablePath().parent_path() / "Saved" / "Crashes";
}

std::filesystem::path ResolveReporterPath(const CrashContext& context)
{
    if (!context.reporterExecutablePath.empty())
        return context.reporterExecutablePath;
    return GetCurrentExecutablePath().parent_path() / "DotCrashReporter.exe";
}

std::string QuoteIfNeeded(const std::string& value)
{
    if (value.empty())
        return {};
    if (value.find(' ') == std::string::npos && value.find('\t') == std::string::npos)
        return value;
    return "\"" + value + "\"";
}

bool CopyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::error_code ec;
    if (source.empty() || !std::filesystem::exists(source, ec) || ec)
        return false;

    std::filesystem::create_directories(destination.parent_path(), ec);
    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return false;

    file << contents;
    return file.good();
}

bool WriteMinidumpFile(EXCEPTION_POINTERS* exceptionInfo, const std::filesystem::path& dumpPath)
{
    HANDLE file = CreateFileW(dumpPath.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION exceptionData = {};
    exceptionData.ThreadId = GetCurrentThreadId();
    exceptionData.ExceptionPointers = exceptionInfo;
    exceptionData.ClientPointers = FALSE;

    const BOOL success = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal,
                                           exceptionInfo ? &exceptionData : nullptr, nullptr, nullptr);
    CloseHandle(file);
    return success == TRUE;
}

bool LaunchCrashReporter(const CrashContext& context, const std::filesystem::path& metadataPath)
{
    const std::filesystem::path reporterPath = ResolveReporterPath(context);
    if (!std::filesystem::exists(reporterPath))
    {
        std::string message =
            "Crash artifacts were written, but DotCrashReporter.exe was not found.\n\nMetadata: " + metadataPath.string();
        MessageBoxA(nullptr, message.c_str(), "Dot Engine Crash", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring reporterExe = reporterPath.wstring();
    std::wstring commandLine = L"\"" + reporterExe + L"\" \"" + metadataPath.wstring() + L"\"";

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const BOOL launched = CreateProcessW(reporterExe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                         reporterPath.parent_path().wstring().c_str(), &startupInfo, &processInfo);
    if (launched)
    {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return true;
    }
    return false;
}

std::string BuildCrashSummaryText(const CrashMetadata& metadata, const std::string& symbolDescription)
{
    std::ostringstream summary;
    summary << "App: " << metadata.appName << '\n';
    summary << "Time (UTC): " << metadata.timestampUtc << '\n';
    summary << "Build: " << metadata.buildConfig << '\n';
    summary << "Exception: " << metadata.exceptionName;
    if (!metadata.exceptionCode.empty())
        summary << " (" << metadata.exceptionCode << ")";
    summary << '\n';
    if (!metadata.exceptionAddress.empty())
        summary << "Address: " << metadata.exceptionAddress << '\n';
    if (!metadata.accessType.empty())
        summary << "Access: " << metadata.accessType << " at " << metadata.accessAddress << '\n';
    summary << "Executable: " << metadata.executablePath << '\n';
    if (!metadata.projectPath.empty())
        summary << "Project: " << metadata.projectPath << '\n';
    if (!metadata.scenePath.empty())
        summary << "Scene: " << metadata.scenePath << '\n';
    if (!symbolDescription.empty())
        summary << "Symbol: " << symbolDescription << '\n';
    if (!metadata.stackSummary.empty())
    {
        summary << "\nStack Trace\n-----------\n";
        summary << metadata.stackSummary;
    }

    return summary.str();
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    std::lock_guard<std::mutex> lock(g_CrashMutex);

    const CrashContext context = g_CrashContext;
    const std::filesystem::path crashRoot = ResolveCrashRoot(context);
    const std::string timestamp = GetCurrentTimestampUtc();
    const std::filesystem::path crashDirectory =
        crashRoot / (timestamp + "_" + std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())));
    std::error_code ec;
    std::filesystem::create_directories(crashDirectory, ec);

    CrashArtifacts artifacts;
    artifacts.crashDirectory = crashDirectory;
    artifacts.metadataPath = crashDirectory / "crash.json";
    artifacts.dumpPath = crashDirectory / "crash.dmp";
    artifacts.logCopyPath = crashDirectory / "crash.log";
    artifacts.summaryPath = crashDirectory / "summary.txt";

    DWORD exceptionCode = exceptionInfo && exceptionInfo->ExceptionRecord ? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
    void* exceptionAddress =
        exceptionInfo && exceptionInfo->ExceptionRecord ? exceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;

    CrashMetadata metadata;
    metadata.appName = context.appName;
    metadata.buildConfig = GetBuildConfigString();
    metadata.timestampUtc = timestamp;
    metadata.exceptionName = GetExceptionCodeName(exceptionCode);
    metadata.exceptionCode = ToHexString(exceptionCode);
    metadata.exceptionAddress = ToHexString(reinterpret_cast<uint64_t>(exceptionAddress));
    metadata.executablePath = GetCurrentExecutablePath().string();
    metadata.workingDirectory = std::filesystem::current_path().string();
    metadata.commandLine = WideToUtf8(GetCommandLineW());
    metadata.projectPath = context.projectPath;
    metadata.scenePath = context.scenePath;
    metadata.relaunchExecutablePath =
        (!context.relaunchExecutablePath.empty() ? context.relaunchExecutablePath : GetCurrentExecutablePath()).string();
    metadata.relaunchArguments = context.relaunchArguments;
    metadata.crashDirectory = artifacts.crashDirectory.string();
    metadata.dumpPath = artifacts.dumpPath.string();
    metadata.logPath = artifacts.logCopyPath.string();
    metadata.summaryPath = artifacts.summaryPath.string();

    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION && exceptionInfo && exceptionInfo->ExceptionRecord &&
        exceptionInfo->ExceptionRecord->NumberParameters >= 2)
    {
        const ULONG_PTR accessType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR accessAddress = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
        metadata.accessType = (accessType == 0) ? "READ" : (accessType == 1) ? "WRITE" : "DEP";
        metadata.accessAddress = ToHexString(accessAddress);
    }

    const std::string symbolDescription = DescribeAddress(exceptionAddress);
    metadata.stackSummary = CaptureStackSummary(exceptionInfo ? exceptionInfo->ContextRecord : nullptr);

    WriteMinidumpFile(exceptionInfo, artifacts.dumpPath);
    WriteTextFile(artifacts.summaryPath, BuildCrashSummaryText(metadata, symbolDescription));
    CrashHandling::WriteCrashMetadata(metadata, artifacts.metadataPath);

    Log::Flush();
    std::filesystem::path logPath = context.logFilePath.empty() ? Log::GetLogFilePath() : context.logFilePath;
    if (!logPath.empty())
        CopyFileIfExists(std::filesystem::absolute(logPath), artifacts.logCopyPath);

    LaunchCrashReporter(context, artifacts.metadataPath);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

std::string FindJsonStringValue(const std::string& json, const char* key)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t keyPos = json.find(token);
    if (keyPos == std::string::npos)
        return {};

    const size_t colonPos = json.find(':', keyPos + token.size());
    if (colonPos == std::string::npos)
        return {};

    size_t valueStart = json.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos || json[valueStart] != '"')
        return {};

    ++valueStart;
    std::string value;
    value.reserve(128);
    bool escaped = false;
    for (size_t i = valueStart; i < json.size(); ++i)
    {
        const char ch = json[i];
        if (escaped)
        {
            value += '\\';
            value += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\')
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
            return UnescapeJson(value);
        value += ch;
    }

    return {};
}

} // namespace

namespace CrashHandling
{

bool Install(const CrashContext& context)
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_CrashMutex);
    g_CrashContext = context;
    if (g_CrashContext.appName.empty())
        g_CrashContext.appName = "Dot Engine";
    if (g_CrashContext.relaunchExecutablePath.empty())
        g_CrashContext.relaunchExecutablePath = GetCurrentExecutablePath();
    if (g_CrashContext.logFilePath.empty())
        g_CrashContext.logFilePath = Log::GetLogFilePath();
    SetUnhandledExceptionFilter(CrashHandler);
    g_CrashInstalled = true;
    return true;
#else
    (void)context;
    return false;
#endif
}

void SetProjectPath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_CrashMutex);
    g_CrashContext.projectPath = path;
}

void SetScenePath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_CrashMutex);
    g_CrashContext.scenePath = path;
}

void SetRelaunchArguments(const std::string& arguments)
{
    std::lock_guard<std::mutex> lock(g_CrashMutex);
    g_CrashContext.relaunchArguments = arguments;
}

void SetReporterExecutablePath(const std::filesystem::path& path)
{
    std::lock_guard<std::mutex> lock(g_CrashMutex);
    g_CrashContext.reporterExecutablePath = path;
}

bool WriteCrashMetadata(const CrashMetadata& metadata, const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return false;

    file << "{\n";
    file << "  \"crash\": {\n";
    file << "    \"appName\": \"" << EscapeJson(metadata.appName) << "\",\n";
    file << "    \"buildConfig\": \"" << EscapeJson(metadata.buildConfig) << "\",\n";
    file << "    \"timestampUtc\": \"" << EscapeJson(metadata.timestampUtc) << "\",\n";
    file << "    \"exceptionName\": \"" << EscapeJson(metadata.exceptionName) << "\",\n";
    file << "    \"exceptionCode\": \"" << EscapeJson(metadata.exceptionCode) << "\",\n";
    file << "    \"exceptionAddress\": \"" << EscapeJson(metadata.exceptionAddress) << "\",\n";
    file << "    \"accessType\": \"" << EscapeJson(metadata.accessType) << "\",\n";
    file << "    \"accessAddress\": \"" << EscapeJson(metadata.accessAddress) << "\",\n";
    file << "    \"executablePath\": \"" << EscapeJson(metadata.executablePath) << "\",\n";
    file << "    \"workingDirectory\": \"" << EscapeJson(metadata.workingDirectory) << "\",\n";
    file << "    \"commandLine\": \"" << EscapeJson(metadata.commandLine) << "\",\n";
    file << "    \"projectPath\": \"" << EscapeJson(metadata.projectPath) << "\",\n";
    file << "    \"scenePath\": \"" << EscapeJson(metadata.scenePath) << "\",\n";
    file << "    \"relaunchExecutablePath\": \"" << EscapeJson(metadata.relaunchExecutablePath) << "\",\n";
    file << "    \"relaunchArguments\": \"" << EscapeJson(metadata.relaunchArguments) << "\",\n";
    file << "    \"crashDirectory\": \"" << EscapeJson(metadata.crashDirectory) << "\",\n";
    file << "    \"dumpPath\": \"" << EscapeJson(metadata.dumpPath) << "\",\n";
    file << "    \"logPath\": \"" << EscapeJson(metadata.logPath) << "\",\n";
    file << "    \"summaryPath\": \"" << EscapeJson(metadata.summaryPath) << "\",\n";
    file << "    \"stackSummary\": \"" << EscapeJson(metadata.stackSummary) << "\"\n";
    file << "  }\n";
    file << "}\n";
    return file.good();
}

bool LoadCrashMetadata(const std::filesystem::path& path, CrashMetadata& outMetadata)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    outMetadata.appName = FindJsonStringValue(json, "appName");
    outMetadata.buildConfig = FindJsonStringValue(json, "buildConfig");
    outMetadata.timestampUtc = FindJsonStringValue(json, "timestampUtc");
    outMetadata.exceptionName = FindJsonStringValue(json, "exceptionName");
    outMetadata.exceptionCode = FindJsonStringValue(json, "exceptionCode");
    outMetadata.exceptionAddress = FindJsonStringValue(json, "exceptionAddress");
    outMetadata.accessType = FindJsonStringValue(json, "accessType");
    outMetadata.accessAddress = FindJsonStringValue(json, "accessAddress");
    outMetadata.executablePath = FindJsonStringValue(json, "executablePath");
    outMetadata.workingDirectory = FindJsonStringValue(json, "workingDirectory");
    outMetadata.commandLine = FindJsonStringValue(json, "commandLine");
    outMetadata.projectPath = FindJsonStringValue(json, "projectPath");
    outMetadata.scenePath = FindJsonStringValue(json, "scenePath");
    outMetadata.relaunchExecutablePath = FindJsonStringValue(json, "relaunchExecutablePath");
    outMetadata.relaunchArguments = FindJsonStringValue(json, "relaunchArguments");
    outMetadata.crashDirectory = FindJsonStringValue(json, "crashDirectory");
    outMetadata.dumpPath = FindJsonStringValue(json, "dumpPath");
    outMetadata.logPath = FindJsonStringValue(json, "logPath");
    outMetadata.summaryPath = FindJsonStringValue(json, "summaryPath");
    outMetadata.stackSummary = FindJsonStringValue(json, "stackSummary");

    return !outMetadata.appName.empty();
}

bool ReportHandledFatalError(const CrashMetadata& metadata, bool launchReporter, CrashArtifacts* outArtifacts)
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_CrashMutex);

    CrashMetadata resolved = metadata;
    const CrashContext context = g_CrashContext;

    if (resolved.appName.empty())
        resolved.appName = context.appName.empty() ? "Dot Engine" : context.appName;
    if (resolved.buildConfig.empty())
        resolved.buildConfig = GetBuildConfigString();
    if (resolved.timestampUtc.empty())
        resolved.timestampUtc = GetCurrentTimestampUtc();
    if (resolved.exceptionName.empty())
        resolved.exceptionName = "FATAL_ERROR";
    if (resolved.exceptionCode.empty())
        resolved.exceptionCode = resolved.exceptionName;
    if (resolved.exceptionAddress.empty())
        resolved.exceptionAddress = "0x0";
    if (resolved.executablePath.empty())
        resolved.executablePath = GetCurrentExecutablePath().string();
    if (resolved.workingDirectory.empty())
        resolved.workingDirectory = std::filesystem::current_path().string();
    if (resolved.commandLine.empty())
        resolved.commandLine = WideToUtf8(GetCommandLineW());
    if (resolved.projectPath.empty())
        resolved.projectPath = context.projectPath;
    if (resolved.scenePath.empty())
        resolved.scenePath = context.scenePath;
    if (resolved.relaunchExecutablePath.empty())
        resolved.relaunchExecutablePath =
            (!context.relaunchExecutablePath.empty() ? context.relaunchExecutablePath : GetCurrentExecutablePath()).string();
    if (resolved.relaunchArguments.empty())
        resolved.relaunchArguments = context.relaunchArguments;

    const std::filesystem::path crashRoot = ResolveCrashRoot(context);
    const std::filesystem::path crashDirectory =
        crashRoot / (resolved.timestampUtc + "_" + std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())));
    std::error_code ec;
    std::filesystem::create_directories(crashDirectory, ec);
    if (ec)
        return false;

    CrashArtifacts artifacts;
    artifacts.crashDirectory = crashDirectory;
    artifacts.metadataPath = crashDirectory / "crash.json";
    artifacts.logCopyPath = crashDirectory / "crash.log";
    artifacts.summaryPath = crashDirectory / "summary.txt";

    resolved.crashDirectory = artifacts.crashDirectory.string();
    resolved.dumpPath.clear();
    resolved.logPath = artifacts.logCopyPath.string();
    resolved.summaryPath = artifacts.summaryPath.string();

    const bool summaryWritten = WriteTextFile(artifacts.summaryPath, BuildCrashSummaryText(resolved, {}));
    const bool metadataWritten = WriteCrashMetadata(resolved, artifacts.metadataPath);

    Log::Flush();
    std::filesystem::path logPath = context.logFilePath.empty() ? Log::GetLogFilePath() : context.logFilePath;
    if (!logPath.empty())
        CopyFileIfExists(std::filesystem::absolute(logPath), artifacts.logCopyPath);

    if (outArtifacts)
        *outArtifacts = artifacts;

    bool reporterLaunched = true;
    if (launchReporter)
        reporterLaunched = LaunchCrashReporter(context, artifacts.metadataPath);

    return summaryWritten && metadataWritten && reporterLaunched;
#else
    (void)metadata;
    (void)launchReporter;
    (void)outArtifacts;
    return false;
#endif
}

} // namespace CrashHandling

} // namespace Dot
