#include "Core/Scene/SceneSettingsSerializer.h"

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

float ParseFloat(const std::string& value, float defaultValue)
{
    if (value.empty())
        return defaultValue;
    try
    {
        return std::stof(value);
    }
    catch (...)
    {
        return defaultValue;
    }
}

int ParseInt(const std::string& value, int defaultValue)
{
    if (value.empty())
        return defaultValue;
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return defaultValue;
    }
}

} // namespace

bool SceneSettingsSerializer::Save(const SceneSettingsAsset& asset, const std::filesystem::path& path)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Failed to open settings asset for writing: " + path.string();
        return false;
    }

    file << "{\n";
    file << "  \"sceneSettings\": {\n";
    file << "    \"version\": 1,\n";
    file << "    \"cubemapPath\": \"" << asset.cubemapPath << "\",\n";
    file << "    \"wrapMode\": " << asset.wrapMode << ",\n";
    file << "    \"tintR\": " << asset.tintR << ",\n";
    file << "    \"tintG\": " << asset.tintG << ",\n";
    file << "    \"tintB\": " << asset.tintB << ",\n";
    file << "    \"rotation\": " << asset.rotation << ",\n";
    file << "    \"showMarkers\": " << (asset.showMarkers ? "true" : "false") << ",\n";
    file << "    \"ambientEnabled\": " << (asset.ambientEnabled ? "true" : "false") << ",\n";
    file << "    \"ambientColorR\": " << asset.ambientColorR << ",\n";
    file << "    \"ambientColorG\": " << asset.ambientColorG << ",\n";
    file << "    \"ambientColorB\": " << asset.ambientColorB << ",\n";
    file << "    \"ambientIntensity\": " << asset.ambientIntensity << ",\n";
    file << "    \"sunEnabled\": " << (asset.sunEnabled ? "true" : "false") << ",\n";
    file << "    \"sunRotationX\": " << asset.sunRotationX << ",\n";
    file << "    \"sunRotationY\": " << asset.sunRotationY << ",\n";
    file << "    \"sunColorR\": " << asset.sunColorR << ",\n";
    file << "    \"sunColorG\": " << asset.sunColorG << ",\n";
    file << "    \"sunColorB\": " << asset.sunColorB << ",\n";
    file << "    \"sunIntensity\": " << asset.sunIntensity << ",\n";
    file << "    \"sunCastShadows\": " << (asset.sunCastShadows ? "true" : "false") << ",\n";
    file << "    \"sunShadowBias\": " << asset.sunShadowBias << ",\n";
    file << "    \"sunShadowDistance\": " << asset.sunShadowDistance << ",\n";
    file << "    \"mapPath\": \"" << asset.mapPath << "\",\n";
    file << "    \"mapVisible\": " << (asset.mapVisible ? "true" : "false") << ",\n";
    file << "    \"mapCollisionEnabled\": " << (asset.mapCollisionEnabled ? "true" : "false") << "\n";
    file << "  }\n";
    file << "}\n";
    return true;
}

bool SceneSettingsSerializer::Load(SceneSettingsAsset& asset, const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Failed to open settings asset for reading: " + path.string();
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    asset.cubemapPath = FindJsonValue(content, "cubemapPath");
    asset.wrapMode = ParseInt(FindJsonValue(content, "wrapMode"), asset.wrapMode);
    asset.tintR = ParseFloat(FindJsonValue(content, "tintR"), asset.tintR);
    asset.tintG = ParseFloat(FindJsonValue(content, "tintG"), asset.tintG);
    asset.tintB = ParseFloat(FindJsonValue(content, "tintB"), asset.tintB);
    asset.rotation = ParseFloat(FindJsonValue(content, "rotation"), asset.rotation);
    asset.showMarkers = ParseBool(FindJsonValue(content, "showMarkers"), asset.showMarkers);
    asset.ambientEnabled = ParseBool(FindJsonValue(content, "ambientEnabled"), asset.ambientEnabled);
    asset.ambientColorR = ParseFloat(FindJsonValue(content, "ambientColorR"), asset.ambientColorR);
    asset.ambientColorG = ParseFloat(FindJsonValue(content, "ambientColorG"), asset.ambientColorG);
    asset.ambientColorB = ParseFloat(FindJsonValue(content, "ambientColorB"), asset.ambientColorB);
    asset.ambientIntensity = ParseFloat(FindJsonValue(content, "ambientIntensity"), asset.ambientIntensity);
    asset.sunEnabled = ParseBool(FindJsonValue(content, "sunEnabled"), asset.sunEnabled);
    asset.sunRotationX = ParseFloat(FindJsonValue(content, "sunRotationX"), asset.sunRotationX);
    asset.sunRotationY = ParseFloat(FindJsonValue(content, "sunRotationY"), asset.sunRotationY);
    asset.sunColorR = ParseFloat(FindJsonValue(content, "sunColorR"), asset.sunColorR);
    asset.sunColorG = ParseFloat(FindJsonValue(content, "sunColorG"), asset.sunColorG);
    asset.sunColorB = ParseFloat(FindJsonValue(content, "sunColorB"), asset.sunColorB);
    asset.sunIntensity = ParseFloat(FindJsonValue(content, "sunIntensity"), asset.sunIntensity);
    asset.sunCastShadows = ParseBool(FindJsonValue(content, "sunCastShadows"), asset.sunCastShadows);
    asset.sunShadowBias = ParseFloat(FindJsonValue(content, "sunShadowBias"), asset.sunShadowBias);
    asset.sunShadowDistance = ParseFloat(FindJsonValue(content, "sunShadowDistance"), asset.sunShadowDistance);
    asset.mapPath = FindJsonValue(content, "mapPath");
    asset.mapVisible = ParseBool(FindJsonValue(content, "mapVisible"), asset.mapVisible);
    asset.mapCollisionEnabled = ParseBool(FindJsonValue(content, "mapCollisionEnabled"), asset.mapCollisionEnabled);
    return true;
}

} // namespace Dot
