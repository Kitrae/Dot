// =============================================================================
// Dot Engine - Prefab Implementation
// =============================================================================

#include "Core/Scene/Prefab.h"

#include "Core/Log.h"

#include <fstream>
#include <sstream>

namespace Dot
{

// =============================================================================
// Simple JSON helpers (no external dependency)
// =============================================================================

namespace
{
// Escape string for JSON
std::string EscapeJson(const std::string& s)
{
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

// Unescape JSON string
std::string UnescapeJson(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    bool escape = false;
    for (char c : s)
    {
        if (escape)
        {
            switch (c)
            {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
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
                    result += c;
            }
            escape = false;
        }
        else if (c == '\\')
        {
            escape = true;
        }
        else
        {
            result += c;
        }
    }
    return result;
}

// Skip whitespace
void SkipWhitespace(const std::string& json, size_t& pos)
{
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t'))
    {
        pos++;
    }
}

// Parse JSON string value (expects pos at opening ")
std::string ParseJsonString(const std::string& json, size_t& pos)
{
    if (pos >= json.size() || json[pos] != '"')
        return "";

    pos++; // skip opening "
    std::string result;
    bool escape = false;

    while (pos < json.size())
    {
        char c = json[pos++];
        if (escape)
        {
            switch (c)
            {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
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
                    result += c;
            }
            escape = false;
        }
        else if (c == '\\')
        {
            escape = true;
        }
        else if (c == '"')
        {
            break;
        }
        else
        {
            result += c;
        }
    }
    return result;
}

// Parse JSON integer
int ParseJsonInt(const std::string& json, size_t& pos)
{
    SkipWhitespace(json, pos);
    std::string num;
    if (pos < json.size() && json[pos] == '-')
    {
        num += '-';
        pos++;
    }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
    {
        num += json[pos++];
    }
    return num.empty() ? 0 : std::stoi(num);
}

// Skip to next key or end of object
void SkipJsonValue(const std::string& json, size_t& pos)
{
    SkipWhitespace(json, pos);
    if (pos >= json.size())
        return;

    char c = json[pos];
    if (c == '"')
    {
        ParseJsonString(json, pos);
    }
    else if (c == '{')
    {
        int depth = 1;
        pos++;
        while (pos < json.size() && depth > 0)
        {
            if (json[pos] == '{')
                depth++;
            else if (json[pos] == '}')
                depth--;
            else if (json[pos] == '"')
            {
                ParseJsonString(json, pos);
                continue; // ParseJsonString already advances pos
            }
            pos++; // Always advance pos
        }
    }
    else if (c == '[')
    {
        int depth = 1;
        pos++;
        while (pos < json.size() && depth > 0)
        {
            if (json[pos] == '[')
                depth++;
            else if (json[pos] == ']')
                depth--;
            else if (json[pos] == '"')
            {
                ParseJsonString(json, pos);
                continue; // ParseJsonString already advances pos
            }
            pos++; // Always advance pos
        }
    }
    else
    {
        // Number, bool, null - skip until , or } or ]
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
        {
            pos++;
        }
    }
}
} // namespace

// =============================================================================
// Prefab Serialization
// =============================================================================

bool Prefab::SaveToFile(const std::string& path) const
{
    std::ofstream file(path);
    if (!file)
    {
        DOT_LOG_ERROR("Failed to open prefab file for writing: %s", path.c_str());
        return false;
    }

    std::string jsonStr = ToJson();

    // Debug: Log the JSON to console
    DOT_LOG_INFO("Prefab JSON:\n%s", jsonStr.c_str());

    file << jsonStr;
    file.flush();
    file.close();

    DOT_LOG_INFO("Saved prefab '%s' to %s (%zu bytes)", m_Name.c_str(), path.c_str(), jsonStr.size());
    return true;
}

bool Prefab::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        DOT_LOG_ERROR("Failed to open prefab file: %s", path.c_str());
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    if (!FromJson(buffer.str()))
    {
        DOT_LOG_ERROR("Failed to parse prefab file: %s", path.c_str());
        return false;
    }

    m_SourcePath = path;
    DOT_LOG_INFO("Loaded prefab '%s' from %s", m_Name.c_str(), path.c_str());
    return true;
}

std::string Prefab::ToJson() const
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"name\": \"" << EscapeJson(m_Name) << "\",\n";
    json << "  \"version\": 1,\n";
    json << "  \"entities\": [\n";

    for (size_t i = 0; i < m_Entities.size(); i++)
    {
        const auto& entity = m_Entities[i];
        json << "    {\n";
        json << "      \"name\": \"" << EscapeJson(entity.name) << "\",\n";
        json << "      \"parent\": " << entity.parentIndex << ",\n";
        json << "      \"components\": {\n";

        size_t compIdx = 0;
        for (const auto& [typeName, compJson] : entity.components)
        {
            json << "        \"" << EscapeJson(typeName) << "\": " << compJson;
            if (++compIdx < entity.components.size())
                json << ",";
            json << "\n";
        }

        json << "      }\n";
        json << "    }";
        if (i < m_Entities.size() - 1)
            json << ",";
        json << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    return json.str();
}

bool Prefab::FromJson(const std::string& json)
{
    m_Entities.clear();
    m_Name.clear();

    size_t pos = 0;
    SkipWhitespace(json, pos);

    if (pos >= json.size() || json[pos] != '{')
        return false;
    pos++;

    while (pos < json.size())
    {
        SkipWhitespace(json, pos);
        if (pos >= json.size())
            break;

        if (json[pos] == '}')
        {
            pos++;
            break;
        }

        if (json[pos] == ',')
        {
            pos++;
            continue;
        }

        // Parse key
        std::string key = ParseJsonString(json, pos);

        SkipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != ':')
            return false;
        pos++;
        SkipWhitespace(json, pos);

        if (key == "name")
        {
            m_Name = ParseJsonString(json, pos);
        }
        else if (key == "version")
        {
            ParseJsonInt(json, pos); // Currently just skip
        }
        else if (key == "entities")
        {
            // Parse entities array
            if (pos >= json.size() || json[pos] != '[')
                return false;
            pos++;

            while (pos < json.size())
            {
                SkipWhitespace(json, pos);
                if (pos >= json.size())
                    break;

                if (json[pos] == ']')
                {
                    pos++;
                    break;
                }

                if (json[pos] == ',')
                {
                    pos++;
                    continue;
                }

                // Parse entity object
                if (json[pos] != '{')
                    return false;
                pos++;

                PrefabEntity entity;

                while (pos < json.size())
                {
                    SkipWhitespace(json, pos);
                    if (pos >= json.size())
                        break;

                    if (json[pos] == '}')
                    {
                        pos++;
                        break;
                    }

                    if (json[pos] == ',')
                    {
                        pos++;
                        continue;
                    }

                    std::string entKey = ParseJsonString(json, pos);

                    SkipWhitespace(json, pos);
                    if (pos >= json.size() || json[pos] != ':')
                        return false;
                    pos++;
                    SkipWhitespace(json, pos);

                    if (entKey == "name")
                    {
                        entity.name = ParseJsonString(json, pos);
                    }
                    else if (entKey == "parent")
                    {
                        entity.parentIndex = ParseJsonInt(json, pos);
                    }
                    else if (entKey == "components")
                    {
                        // Parse components object
                        if (pos >= json.size() || json[pos] != '{')
                            return false;
                        pos++;

                        while (pos < json.size())
                        {
                            SkipWhitespace(json, pos);
                            if (pos >= json.size())
                                break;

                            if (json[pos] == '}')
                            {
                                pos++;
                                break;
                            }

                            if (json[pos] == ',')
                            {
                                pos++;
                                continue;
                            }

                            std::string compType = ParseJsonString(json, pos);

                            SkipWhitespace(json, pos);
                            if (pos >= json.size() || json[pos] != ':')
                                return false;
                            pos++;
                            SkipWhitespace(json, pos);

                            // Capture component JSON (including nested objects)
                            size_t startPos = pos;
                            SkipJsonValue(json, pos);
                            std::string compJson = json.substr(startPos, pos - startPos);

                            DOT_LOG_INFO("Parsed component: %s = %s", compType.c_str(), compJson.c_str());
                            entity.components[compType] = compJson;
                        }
                    }
                    else
                    {
                        SkipJsonValue(json, pos);
                    }
                }

                m_Entities.push_back(entity);
            }
        }
        else
        {
            SkipJsonValue(json, pos);
        }
    }

    return !m_Entities.empty();
}

} // namespace Dot
