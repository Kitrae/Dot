#include "GameExporter.h"

#include "AssetDependencyCooker.h"

#include <Core/Project/DotProjectAsset.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Dot
{

namespace
{

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

const char* ToString(CookedAssetKind kind)
{
    switch (kind)
    {
        case CookedAssetKind::Scene:
            return "Scene";
        case CookedAssetKind::Map:
            return "Map";
        case CookedAssetKind::Material:
            return "Material";
        case CookedAssetKind::Mesh:
            return "Mesh";
        case CookedAssetKind::Script:
            return "Script";
        case CookedAssetKind::Cubemap:
            return "Cubemap";
        case CookedAssetKind::Texture:
            return "Texture";
        case CookedAssetKind::Prefab:
            return "Prefab";
        case CookedAssetKind::Generic:
        default:
            return "Generic";
    }
}

std::vector<CookedAssetRecord> EnumerateStagedAssets(const std::filesystem::path& assetsRoot)
{
    std::vector<CookedAssetRecord> records;
    std::error_code ec;
    if (!std::filesystem::exists(assetsRoot))
        return records;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;

        const std::filesystem::path relative = std::filesystem::relative(entry.path(), assetsRoot, ec);
        if (ec)
            continue;

        CookedAssetRecord record;
        record.path = relative.generic_string();
        record.kind = CookedAssetKind::Generic;
        record.sourceBytes = static_cast<uint64>(entry.file_size(ec));
        if (ec)
            record.sourceBytes = 0;
        records.push_back(std::move(record));
    }

    std::sort(records.begin(), records.end(),
              [](const CookedAssetRecord& a, const CookedAssetRecord& b) { return a.path < b.path; });
    return records;
}

} // namespace

bool GameExporter::Export(const GameExportOptions& options)
{
    m_LastError.clear();

    if (options.outputDirectory.empty())
    {
        m_LastError = "Export output directory is empty.";
        return false;
    }
    if (options.gameExecutablePath.empty() || !std::filesystem::exists(options.gameExecutablePath))
    {
        m_LastError = "DotGame.exe was not found. Build the game target before exporting.";
        return false;
    }
    if (options.crashReporterExecutablePath.empty() || !std::filesystem::exists(options.crashReporterExecutablePath))
    {
        m_LastError = "DotCrashReporter.exe was not found. Build the crash reporter target before exporting.";
        return false;
    }
    if (options.assetsRoot.empty() || !std::filesystem::exists(options.assetsRoot))
    {
        m_LastError = "Assets root does not exist.";
        return false;
    }
    if (options.projectAsset.startupScene.empty())
    {
        m_LastError = "Export project is missing a startup scene.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(options.outputDirectory, ec);
    if (ec)
    {
        m_LastError = "Failed to create export directory: " + options.outputDirectory.string();
        return false;
    }

    if (!CopyFileTo(options.gameExecutablePath, options.outputDirectory / "DotGame.exe"))
        return false;
    if (!CopyFileTo(options.crashReporterExecutablePath, options.outputDirectory / "DotCrashReporter.exe"))
        return false;

    const std::filesystem::path gamePdb = options.gameExecutablePath.parent_path() / "DotGame.pdb";
    if (std::filesystem::exists(gamePdb))
        CopyFileTo(gamePdb, options.outputDirectory / gamePdb.filename());
    const std::filesystem::path reporterPdb =
        options.crashReporterExecutablePath.parent_path() / "DotCrashReporter.pdb";
    if (std::filesystem::exists(reporterPdb))
        CopyFileTo(reporterPdb, options.outputDirectory / reporterPdb.filename());

    for (const auto& entry : std::filesystem::directory_iterator(options.gameExecutablePath.parent_path(), ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".dll")
            continue;
        if (!CopyFileTo(entry.path(), options.outputDirectory / entry.path().filename()))
            return false;
    }

    if (options.cookDependenciesOnly)
    {
        AssetDependencyCooker cooker;
        if (!cooker.Collect(options.assetsRoot, options.projectAsset.startupScene))
        {
            m_LastError = cooker.GetLastError();
            return false;
        }
        if (!cooker.StageTo(options.outputDirectory / "Assets"))
        {
            m_LastError = "Failed to stage cooked dependency assets.";
            return false;
        }
        if (!WriteCookManifest(options, &cooker))
            return false;
    }
    else
    {
        if (!CopyDirectoryRecursive(options.assetsRoot, options.outputDirectory / "Assets"))
            return false;
        if (!WriteCookManifest(options, nullptr))
            return false;
    }

    if (!options.shadersRoot.empty() && std::filesystem::exists(options.shadersRoot))
    {
        if (!CopyDirectoryRecursive(options.shadersRoot, options.outputDirectory / "Shaders"))
            return false;
    }

    DotProjectSerializer serializer;
    if (!serializer.Save(options.projectAsset, options.outputDirectory / "Game.dotproject"))
    {
        m_LastError = serializer.GetLastError();
        return false;
    }

    return true;
}

bool GameExporter::WriteCookManifest(const GameExportOptions& options, const AssetDependencyCooker* cooker)
{
    const std::filesystem::path manifestPath = options.outputDirectory / "CookManifest.json";
    std::ofstream file(manifestPath);
    if (!file.is_open())
    {
        m_LastError = "Failed to write cook manifest: " + manifestPath.string();
        return false;
    }

    const std::vector<CookedAssetRecord> records =
        cooker ? cooker->GetCookedAssetRecords() : EnumerateStagedAssets(options.outputDirectory / "Assets");

    uint64 totalSourceBytes = 0;
    for (const CookedAssetRecord& record : records)
        totalSourceBytes += record.sourceBytes;

    file << "{\n";
    file << "  \"export\": {\n";
    file << "    \"version\": 1,\n";
    file << "    \"gameName\": \"" << EscapeJson(options.projectAsset.gameName) << "\",\n";
    file << "    \"startupScene\": \"" << EscapeJson(options.projectAsset.startupScene) << "\",\n";
    file << "    \"cookDependenciesOnly\": " << (options.cookDependenciesOnly ? "true" : "false") << ",\n";
    file << "    \"assetCount\": " << records.size() << ",\n";
    file << "    \"totalSourceBytes\": " << totalSourceBytes << ",\n";
    file << "    \"assets\": [\n";
    for (size_t i = 0; i < records.size(); ++i)
    {
        const CookedAssetRecord& record = records[i];
        file << "      {\n";
        file << "        \"path\": \"" << EscapeJson(record.path) << "\",\n";
        file << "        \"kind\": \"" << ToString(record.kind) << "\",\n";
        file << "        \"sourceBytes\": " << record.sourceBytes << "\n";
        file << "      }";
        if (i + 1 < records.size())
            file << ",";
        file << "\n";
    }
    file << "    ]\n";
    file << "  }\n";
    file << "}\n";
    return true;
}

bool GameExporter::CopyFileTo(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        m_LastError = "Failed to copy file: " + source.string();
        return false;
    }
    return true;
}

bool GameExporter::CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    if (!std::filesystem::exists(source))
    {
        m_LastError = "Missing directory: " + source.string();
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec)
    {
        m_LastError = "Failed to create directory: " + destination.string();
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(source, ec))
    {
        if (ec)
        {
            m_LastError = "Failed to enumerate directory: " + source.string();
            return false;
        }

        const std::filesystem::path relative = std::filesystem::relative(entry.path(), source, ec);
        if (ec)
        {
            m_LastError = "Failed to compute relative export path for: " + entry.path().string();
            return false;
        }

        const std::filesystem::path target = destination / relative;
        if (entry.is_directory())
        {
            std::filesystem::create_directories(target, ec);
            if (ec)
            {
                m_LastError = "Failed to create directory: " + target.string();
                return false;
            }
            continue;
        }

        if (!entry.is_regular_file())
            continue;

        std::filesystem::create_directories(target.parent_path(), ec);
        ec.clear();
        std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            m_LastError = "Failed to copy file: " + entry.path().string();
            return false;
        }
    }

    return true;
}

} // namespace Dot
