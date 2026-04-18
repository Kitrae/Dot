// =============================================================================
// Dot Engine - Type Registry
// =============================================================================
// Global registry for reflected types.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Reflect/TypeInfo.h"

#include <string>
#include <unordered_map>


namespace Dot
{

/// Type registry singleton
class DOT_CORE_API TypeRegistry
{
public:
    static TypeRegistry& Get()
    {
        static TypeRegistry instance;
        return instance;
    }

    // Register a type
    template <typename T> TypeInfoBuilder<T> Register(const char* name)
    {
        auto& info = m_Types[name];
        return TypeInfoBuilder<T>(name, info);
    }

    // Get type info by name
    const TypeInfo* GetType(const std::string& name) const
    {
        auto it = m_Types.find(name);
        return it != m_Types.end() ? &it->second : nullptr;
    }

    // Check if type exists
    bool HasType(const std::string& name) const { return m_Types.find(name) != m_Types.end(); }

    // Get all registered types
    const std::unordered_map<std::string, TypeInfo>& GetAllTypes() const { return m_Types; }

    // Create instance by type name
    void* CreateInstance(const std::string& typeName) const
    {
        const TypeInfo* info = GetType(typeName);
        if (info && info->create)
        {
            return info->create();
        }
        return nullptr;
    }

    // Destroy instance
    void DestroyInstance(const std::string& typeName, void* instance) const
    {
        const TypeInfo* info = GetType(typeName);
        if (info && info->destroy && instance)
        {
            info->destroy(instance);
        }
    }

private:
    TypeRegistry() = default;
    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

    std::unordered_map<std::string, TypeInfo> m_Types;
};

/// Macro for easy type registration
#define DOT_REFLECT_TYPE(Type) Dot::TypeRegistry::Get().Register<Type>(#Type)

} // namespace Dot
