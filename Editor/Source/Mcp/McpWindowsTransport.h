#pragma once

#include "McpBridgeProtocol.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace Dot
{

struct McpInstanceManifest
{
    uint32_t pid = 0;
    std::string pipeName;
    std::string projectPath;
    uint64_t processStartTime = 0;
    bool bridgeEnabled = false;
    std::string editorVersion;
};

inline std::string WindowsErrorMessage(DWORD errorCode)
{
    char* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(flags, nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string result;
    if (length > 0 && buffer != nullptr)
        result.assign(buffer, length);
    if (buffer != nullptr)
        LocalFree(buffer);

    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}

inline std::optional<std::filesystem::path> GetLocalAppDataPath()
{
    char* envValue = nullptr;
    size_t length = 0;
    if (_dupenv_s(&envValue, &length, "LOCALAPPDATA") != 0 || envValue == nullptr || length == 0)
        return std::nullopt;

    std::filesystem::path path(envValue);
    std::free(envValue);
    return path;
}

inline std::filesystem::path GetMcpInstancesDirectory()
{
    const std::optional<std::filesystem::path> localAppData = GetLocalAppDataPath();
    if (localAppData.has_value())
        return *localAppData / "DotEngine" / "mcp" / "instances";
    return std::filesystem::temp_directory_path() / "DotEngine" / "mcp" / "instances";
}

inline std::filesystem::path GetMcpManifestPath(uint32_t pid)
{
    return GetMcpInstancesDirectory() / (std::to_string(pid) + ".json");
}

inline std::string MakeMcpPipeName(uint32_t pid)
{
    return "\\\\.\\pipe\\DotEngine.Mcp." + std::to_string(pid);
}

inline uint64_t FileTimeToUnixSeconds(const FILETIME& fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    constexpr uint64_t kWindowsEpochDifference = 11644473600ULL;
    return value.QuadPart / 10000000ULL - kWindowsEpochDifference;
}

inline uint64_t GetCurrentProcessStartTimeUnixSeconds()
{
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
        return 0;
    return FileTimeToUnixSeconds(creationTime);
}

inline McpJson::Value ToJson(const McpInstanceManifest& manifest)
{
    McpJson::Value value = McpJson::Value::MakeObject();
    auto& object = value.objectValue;
    object["pid"] = manifest.pid;
    object["pipeName"] = manifest.pipeName;
    object["projectPath"] = manifest.projectPath;
    object["processStartTime"] = manifest.processStartTime;
    object["bridgeEnabled"] = manifest.bridgeEnabled;
    object["editorVersion"] = manifest.editorVersion;
    return value;
}

inline std::optional<McpInstanceManifest> ParseManifest(const std::string& payload)
{
    const std::optional<McpJson::Value> value = McpJson::Parse(payload);
    if (!value.has_value() || !value->IsObject())
        return std::nullopt;

    const std::optional<double> pid = McpJson::GetNumber(*value, "pid");
    const std::optional<std::string> pipeName = McpJson::GetString(*value, "pipeName");
    const std::optional<std::string> projectPath = McpJson::GetString(*value, "projectPath");
    const std::optional<double> processStartTime = McpJson::GetNumber(*value, "processStartTime");
    const std::optional<bool> bridgeEnabled = McpJson::GetBool(*value, "bridgeEnabled");
    const std::optional<std::string> editorVersion = McpJson::GetString(*value, "editorVersion");
    if (!pid.has_value() || !pipeName.has_value() || !projectPath.has_value() || !processStartTime.has_value() ||
        !bridgeEnabled.has_value() || !editorVersion.has_value())
    {
        return std::nullopt;
    }

    McpInstanceManifest manifest;
    manifest.pid = static_cast<uint32_t>(*pid);
    manifest.pipeName = *pipeName;
    manifest.projectPath = *projectPath;
    manifest.processStartTime = static_cast<uint64_t>(*processStartTime);
    manifest.bridgeEnabled = *bridgeEnabled;
    manifest.editorVersion = *editorVersion;
    return manifest;
}

inline bool WriteManifestFile(const McpInstanceManifest& manifest, std::string* errorMessage = nullptr)
{
    const std::filesystem::path manifestPath = GetMcpManifestPath(manifest.pid);
    std::error_code errorCode;
    std::filesystem::create_directories(manifestPath.parent_path(), errorCode);
    if (errorCode)
    {
        if (errorMessage != nullptr)
            *errorMessage = errorCode.message();
        return false;
    }

    std::ofstream file(manifestPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to open manifest for writing";
        return false;
    }

    file << McpJson::Serialize(ToJson(manifest));
    return true;
}

inline bool ReadManifestFile(const std::filesystem::path& path, McpInstanceManifest& manifest, std::string* errorMessage = nullptr)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to open manifest";
        return false;
    }

    std::string payload((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::optional<McpInstanceManifest> parsed = ParseManifest(payload);
    if (!parsed.has_value())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Invalid manifest JSON";
        return false;
    }

    manifest = *parsed;
    return true;
}

inline bool WriteFramedMessage(HANDLE handle, const std::string& payload, std::string* errorMessage = nullptr)
{
    const uint32_t length = static_cast<uint32_t>(payload.size());
    auto writeExact = [handle, errorMessage](const void* data, size_t size) -> bool
    {
        const char* bytes = static_cast<const char*>(data);
        size_t totalWritten = 0;
        while (totalWritten < size)
        {
            DWORD chunkWritten = 0;
            const DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(size - totalWritten, 64 * 1024));
            if (!WriteFile(handle, bytes + totalWritten, chunkSize, &chunkWritten, nullptr))
            {
                if (errorMessage != nullptr)
                    *errorMessage = WindowsErrorMessage(GetLastError());
                return false;
            }

            if (chunkWritten == 0)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "Pipe write completed with zero bytes.";
                return false;
            }

            totalWritten += static_cast<size_t>(chunkWritten);
        }

        return true;
    };

    if (!writeExact(&length, sizeof(length)))
    {
        return false;
    }

    if (length == 0)
        return true;

    return writeExact(payload.data(), length);
}

inline bool ReadFramedMessage(HANDLE handle, std::string& payload, std::string* errorMessage = nullptr)
{
    uint32_t length = 0;
    auto readExact = [handle, errorMessage](void* data, size_t size) -> bool
    {
        char* bytes = static_cast<char*>(data);
        size_t totalRead = 0;
        while (totalRead < size)
        {
            DWORD chunkRead = 0;
            const DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(size - totalRead, 64 * 1024));
            if (!ReadFile(handle, bytes + totalRead, chunkSize, &chunkRead, nullptr))
            {
                if (errorMessage != nullptr)
                    *errorMessage = WindowsErrorMessage(GetLastError());
                return false;
            }

            if (chunkRead == 0)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "Pipe read completed with zero bytes.";
                return false;
            }

            totalRead += static_cast<size_t>(chunkRead);
        }

        return true;
    };

    if (!readExact(&length, sizeof(length)))
    {
        return false;
    }

    if (length > 8 * 1024 * 1024)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Bridge message exceeds 8MB safety limit";
        return false;
    }

    payload.assign(length, '\0');
    if (length == 0)
        return true;

    return readExact(payload.data(), length);
}

inline void CloseHandleIfValid(HANDLE& handle)
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

} // namespace Dot
