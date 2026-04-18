// =============================================================================
// Dot Engine - Serialization Helpers
// =============================================================================
// JSON serialization using reflection system.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/Math/Vec3.h"
#include "Core/Reflect/Registry.h"

#include <sstream>
#include <string>

namespace Dot
{

/// Serialize a property value to JSON format
inline void SerializePropertyToJson(std::ostream& out, const Property& prop, const void* obj)
{
    void* valuePtr = prop.getter(const_cast<void*>(obj));

    switch (prop.type)
    {
        case PropertyType::Bool:
            out << (*static_cast<bool*>(valuePtr) ? "true" : "false");
            break;
        case PropertyType::Int32:
            if (prop.size == sizeof(int8))
                out << static_cast<int32>(*static_cast<int8*>(valuePtr));
            else if (prop.size == sizeof(int16))
                out << static_cast<int32>(*static_cast<int16*>(valuePtr));
            else
                out << *static_cast<int32*>(valuePtr);
            break;
        case PropertyType::UInt32:
            if (prop.size == sizeof(uint8))
                out << static_cast<uint32>(*static_cast<uint8*>(valuePtr));
            else if (prop.size == sizeof(uint16))
                out << static_cast<uint32>(*static_cast<uint16*>(valuePtr));
            else
                out << *static_cast<uint32*>(valuePtr);
            break;
        case PropertyType::Float:
            out << std::fixed << *static_cast<float*>(valuePtr);
            break;
        case PropertyType::Double:
            out << std::fixed << *static_cast<double*>(valuePtr);
            break;
        case PropertyType::String:
            out << "\"" << *static_cast<std::string*>(valuePtr) << "\"";
            break;
        case PropertyType::Vec3:
        {
            Vec3* v = static_cast<Vec3*>(valuePtr);
            out << "[" << v->x << ", " << v->y << ", " << v->z << "]";
            break;
        }
        case PropertyType::Entity:
        {
            Entity* e = static_cast<Entity*>(valuePtr);
            // Serialize as index (or -1 if null)
            out << (e->IsValid() ? static_cast<int32>(e->GetIndex()) : -1);
            break;
        }
        default:
            out << "null";
            break;
    }
}

/// Serialize a reflected type to JSON
inline std::string SerializeToJson(const TypeInfo* type, const void* obj, int indent = 0)
{
    if (!type || !obj)
        return "{}";

    std::ostringstream out;
    std::string pad(indent, ' ');

    out << "{\n";

    auto props = type->GetAllProperties();
    for (size_t i = 0; i < props.size(); ++i)
    {
        const Property* prop = props[i];

        // Skip transient properties
        if (HasFlag(prop->flags, PropertyFlags::Transient))
            continue;

        out << pad << "  \"" << prop->name << "\": ";
        SerializePropertyToJson(out, *prop, obj);

        if (i < props.size() - 1)
            out << ",";
        out << "\n";
    }

    out << pad << "}";
    return out.str();
}

/// Parse a Vec3 from "[x, y, z]" format
inline bool ParseVec3FromJson(const std::string& str, Vec3& out)
{
    size_t start = str.find('[');
    size_t end = str.find(']');
    if (start == std::string::npos || end == std::string::npos)
        return false;

    std::string content = str.substr(start + 1, end - start - 1);
    float values[3] = {0, 0, 0};
    int idx = 0;

    std::stringstream ss(content);
    std::string token;
    while (std::getline(ss, token, ',') && idx < 3)
    {
        // Trim whitespace
        size_t first = token.find_first_not_of(" \t");
        if (first != std::string::npos)
            token = token.substr(first);
        values[idx++] = std::stof(token);
    }

    out.x = values[0];
    out.y = values[1];
    out.z = values[2];
    return true;
}

/// Deserialize a property value from JSON string
inline bool DeserializePropertyFromJson(const Property& prop, void* obj, const std::string& value)
{
    void* valuePtr = prop.getter(obj);

    switch (prop.type)
    {
        case PropertyType::Bool:
            *static_cast<bool*>(valuePtr) = (value == "true");
            return true;
        case PropertyType::Int32:
            if (prop.size == sizeof(int8))
                *static_cast<int8*>(valuePtr) = static_cast<int8>(std::stoi(value));
            else if (prop.size == sizeof(int16))
                *static_cast<int16*>(valuePtr) = static_cast<int16>(std::stoi(value));
            else
                *static_cast<int32*>(valuePtr) = std::stoi(value);
            return true;
        case PropertyType::UInt32:
            if (prop.size == sizeof(uint8))
                *static_cast<uint8*>(valuePtr) = static_cast<uint8>(std::stoul(value));
            else if (prop.size == sizeof(uint16))
                *static_cast<uint16*>(valuePtr) = static_cast<uint16>(std::stoul(value));
            else
                *static_cast<uint32*>(valuePtr) = static_cast<uint32>(std::stoul(value));
            return true;
        case PropertyType::Float:
            *static_cast<float*>(valuePtr) = std::stof(value);
            return true;
        case PropertyType::Double:
            *static_cast<double*>(valuePtr) = std::stod(value);
            return true;
        case PropertyType::String:
        {
            // Remove quotes
            std::string s = value;
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                s = s.substr(1, s.size() - 2);
            *static_cast<std::string*>(valuePtr) = s;
            return true;
        }
        case PropertyType::Vec3:
            return ParseVec3FromJson(value, *static_cast<Vec3*>(valuePtr));
        case PropertyType::Entity:
        {
            int32 idx = std::stoi(value);
            if (idx >= 0)
            {
                // Note: Entity needs generation, caller must fix up later
                *static_cast<Entity*>(valuePtr) = Entity(static_cast<uint32>(idx), 0);
            }
            else
            {
                *static_cast<Entity*>(valuePtr) = kNullEntity;
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace Dot
