#include "Core/Project/DotProjectAsset.h"

#include <fstream>
#include <sstream>

namespace Dot
{

namespace
{

std::string Trim(const std::string& str)
{
    const size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    const size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string FindJsonValue(const std::string& content, const char* key)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t keyPos = content.find(token);
    if (keyPos == std::string::npos)
        return {};

    const size_t colonPos = content.find(':', keyPos + token.size());
    if (colonPos == std::string::npos)
        return {};

    size_t valueStart = content.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos)
        return {};

    if (content[valueStart] == '"')
    {
        const size_t valueEnd = content.find('"', valueStart + 1);
        if (valueEnd == std::string::npos)
            return {};
        return content.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    const size_t valueEnd = content.find_first_of(",}\r\n", valueStart);
    return Trim(content.substr(valueStart, valueEnd - valueStart));
}

bool ParseBool(const std::string& value, bool defaultValue)
{
    if (value == "true")
        return true;
    if (value == "false")
        return false;
    return defaultValue;
}

uint32 ParseUInt(const std::string& value, uint32 defaultValue)
{
    if (value.empty())
        return defaultValue;
    try
    {
        return static_cast<uint32>(std::stoul(value));
    }
    catch (...)
    {
        return defaultValue;
    }
}

} // namespace

bool DotProjectSerializer::Save(const DotProjectAsset& asset, const std::filesystem::path& path)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Failed to open project file for writing: " + path.string();
        return false;
    }

    file << "{\n";
    file << "  \"project\": {\n";
    file << "    \"version\": " << asset.version << ",\n";
    file << "    \"gameName\": \"" << asset.gameName << "\",\n";
    file << "    \"startupScene\": \"" << asset.startupScene << "\",\n";
    file << "    \"windowWidth\": " << asset.windowWidth << ",\n";
    file << "    \"windowHeight\": " << asset.windowHeight << ",\n";
    file << "    \"startFullscreen\": " << (asset.startFullscreen ? "true" : "false") << ",\n";
    file << "    \"captureMouseOnStart\": " << (asset.captureMouseOnStart ? "true" : "false") << "\n";
    file << "  }\n";
    file << "}\n";
    m_LastError.clear();
    return true;
}

bool DotProjectSerializer::Load(DotProjectAsset& asset, const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Failed to open project file for reading: " + path.string();
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    asset.version = ParseUInt(FindJsonValue(content, "version"), asset.version);
    asset.gameName = FindJsonValue(content, "gameName");
    asset.startupScene = FindJsonValue(content, "startupScene");
    asset.windowWidth = ParseUInt(FindJsonValue(content, "windowWidth"), asset.windowWidth);
    asset.windowHeight = ParseUInt(FindJsonValue(content, "windowHeight"), asset.windowHeight);
    asset.startFullscreen = ParseBool(FindJsonValue(content, "startFullscreen"), asset.startFullscreen);
    asset.captureMouseOnStart = ParseBool(FindJsonValue(content, "captureMouseOnStart"), asset.captureMouseOnStart);
    m_LastError.clear();
    return true;
}

} // namespace Dot
