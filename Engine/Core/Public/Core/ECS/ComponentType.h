// =============================================================================
// Dot Engine - Component Type
// =============================================================================
// Runtime type information for ECS components.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <atomic>
#include <typeinfo>


namespace Dot
{

/// Unique component type ID
using ComponentTypeId = uint32;

/// Invalid component type
constexpr ComponentTypeId kInvalidComponentType = 0;

/// Component type info
struct ComponentTypeInfo
{
    ComponentTypeId id;
    usize size;
    usize alignment;
    const char* name;

    // Function pointers for type-erased operations
    void (*construct)(void* ptr);
    void (*destruct)(void* ptr);
    void (*move)(void* dst, void* src);
    void (*copy)(void* dst, const void* src);
};

/// Component type registry - generates unique IDs per type
class ComponentTypeRegistry
{
public:
    template <typename T> static ComponentTypeId GetId()
    {
        static ComponentTypeId id = NextId();
        return id;
    }

    template <typename T> static const ComponentTypeInfo& GetInfo()
    {
        static ComponentTypeInfo info = CreateInfo<T>();
        return info;
    }

private:
    static ComponentTypeId NextId()
    {
        static std::atomic<ComponentTypeId> s_NextId{1};
        return s_NextId.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename T> static ComponentTypeInfo CreateInfo()
    {
        ComponentTypeInfo info{};
        info.id = GetId<T>();
        info.size = sizeof(T);
        info.alignment = alignof(T);
        info.name = typeid(T).name();

        info.construct = [](void* ptr) { new (ptr) T(); };

        info.destruct = [](void* ptr) { static_cast<T*>(ptr)->~T(); };

        info.move = [](void* dst, void* src) { new (dst) T(std::move(*static_cast<T*>(src))); };

        info.copy = [](void* dst, const void* src) { new (dst) T(*static_cast<const T*>(src)); };

        return info;
    }
};

/// Helper to get component type ID
template <typename T> inline ComponentTypeId GetComponentTypeId()
{
    return ComponentTypeRegistry::GetId<T>();
}

} // namespace Dot
