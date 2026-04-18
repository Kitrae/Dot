#pragma once

#include <Core/Project/DotProjectAsset.h>

#include <filesystem>
#include <string>

namespace Dot
{

class AssetDependencyCooker;

struct GameExportOptions
{
    std::filesystem::path outputDirectory;
    std::filesystem::path gameExecutablePath;
    std::filesystem::path crashReporterExecutablePath;
    std::filesystem::path assetsRoot;
    std::filesystem::path shadersRoot;
    DotProjectAsset projectAsset;
    bool cookDependenciesOnly = true;
};

class GameExporter
{
public:
    bool Export(const GameExportOptions& options);

    const std::string& GetLastError() const { return m_LastError; }

private:
    bool CopyFileTo(const std::filesystem::path& source, const std::filesystem::path& destination);
    bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination);
    bool WriteCookManifest(const GameExportOptions& options, const AssetDependencyCooker* cooker);

    std::string m_LastError;
};

} // namespace Dot
