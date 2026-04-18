// =============================================================================
// Dot Engine - Property
// =============================================================================
// Property descriptor for reflection.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <functional>
#include <string>


namespace Dot
{

/// Property type enumeration
enum class PropertyType : uint8
{
    Unknown = 0,
    Bool,
    Int32,
    Int64,
    UInt32,
    UInt64,
    Float,
    Double,
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Mat4,
    Entity,
    Object, // Pointer to reflected type
    Array,
    Custom,
};

/// Property flags
enum class PropertyFlags : uint32
{
    None = 0,
    ReadOnly = 1 << 0,
    Hidden = 1 << 1,
    Transient = 1 << 2, // Don't serialize
    Editor = 1 << 3,    // Editor-only
};

inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b)
{
    return static_cast<PropertyFlags>(static_cast<uint32>(a) | static_cast<uint32>(b));
}

inline bool HasFlag(PropertyFlags flags, PropertyFlags flag)
{
    return (static_cast<uint32>(flags) & static_cast<uint32>(flag)) != 0;
}

/// Property descriptor
struct Property
{
    std::string name;
    PropertyType type = PropertyType::Unknown;
    PropertyFlags flags = PropertyFlags::None;
    usize offset = 0;
    usize size = 0;

    // For arrays/containers
    PropertyType elementType = PropertyType::Unknown;

    // Accessors (type-erased)
    std::function<void*(void*)> getter;             // obj -> ptr to value
    std::function<void(void*, const void*)> setter; // obj, value -> void

    // For Object type
    const char* typeName = nullptr; // Referenced type name

    // Helpers
    template <typename T, typename Class>
    static Property Create(const char* name, T Class::* member, PropertyType type,
                           PropertyFlags flags = PropertyFlags::None)
    {
        Property prop;
        prop.name = name;
        prop.type = type;
        prop.flags = flags;
        prop.offset = reinterpret_cast<usize>(&(static_cast<Class*>(nullptr)->*member));
        prop.size = sizeof(T);

        prop.getter = [member](void* obj) -> void* { return &(static_cast<Class*>(obj)->*member); };

        prop.setter = [member](void* obj, const void* value)
        { static_cast<Class*>(obj)->*member = *static_cast<const T*>(value); };

        return prop;
    }
};

} // namespace Dot
