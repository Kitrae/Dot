// =============================================================================
// Dot Engine - TypeInfo
// =============================================================================
// Runtime type information for reflection.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Reflect/Property.h"

#include <functional>
#include <string>
#include <vector>

namespace Dot
{

/// Type information
struct TypeInfo
{
    std::string name;
    usize size = 0;
    usize alignment = 0;

    // Parent type (for inheritance)
    const TypeInfo* parent = nullptr;

    // Properties
    std::vector<Property> properties;

    // Factory function
    std::function<void*()> create;
    std::function<void(void*)> destroy;

    // Helpers
    const Property* GetProperty(const std::string& propName) const
    {
        for (const auto& prop : properties)
        {
            if (prop.name == propName)
                return &prop;
        }
        // Check parent
        if (parent)
            return parent->GetProperty(propName);
        return nullptr;
    }

    bool HasProperty(const std::string& propName) const { return GetProperty(propName) != nullptr; }

    // Collect all properties including inherited
    std::vector<const Property*> GetAllProperties() const
    {
        std::vector<const Property*> result;
        if (parent)
        {
            auto parentProps = parent->GetAllProperties();
            result.insert(result.end(), parentProps.begin(), parentProps.end());
        }
        for (const auto& prop : properties)
        {
            result.push_back(&prop);
        }
        return result;
    }
};

/// TypeInfo builder for fluent registration
template <typename T> class TypeInfoBuilder
{
public:
    TypeInfoBuilder(const char* name, TypeInfo& info) : m_Info(info)
    {
        m_Info.name = name;
        m_Info.size = sizeof(T);
        m_Info.alignment = alignof(T);
        m_Info.parent = nullptr;
        m_Info.properties.clear();

        m_Info.create = []() -> void* { return new T(); };
        m_Info.destroy = [](void* ptr) { delete static_cast<T*>(ptr); };
    }

    TypeInfoBuilder& Parent(const TypeInfo* parent)
    {
        m_Info.parent = parent;
        return *this;
    }

    template <typename MemberT>
    TypeInfoBuilder& Property(const char* propName, MemberT T::* member, PropertyType type,
                              PropertyFlags flags = PropertyFlags::None)
    {
        m_Info.properties.push_back(Dot::Property::Create(propName, member, type, flags));
        return *this;
    }

private:
    TypeInfo& m_Info;
};

} // namespace Dot
