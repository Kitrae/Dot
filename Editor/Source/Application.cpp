// =============================================================================
// Dot Engine - Editor Application Implementation
// =============================================================================

#include "Application.h"

#include "Core/Assets/AssetManager.h"
#include "Core/Crash/CrashHandling.h"
#include "Core/ECS/Entity.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Log.h"
#include "Core/Math/Vec2.h"
#include "Core/Reflect/Registry.h"
#include "Core/Scene/MapComponent.h"
#include "Core/Scene/ComponentReflection.h"
#include "Core/Scene/SceneRuntime.h"
#include "Core/Scene/ScriptComponent.h"

#include "Commands/CreateEntityCommands.h"
#include "Commands/CommandRegistry.h"
#include "Export/GameExporter.h"
#include "Map/MapDocument.h"
#include "Mcp/McpBridgeService.h"
#include "Panels/ConsolePanel.h"
#include "Panels/MapInspectorPanel.h"
#include "Panels/MapOutlinerPanel.h"
#include "Panels/SettingsPanel.h"
#include "Scene/EditorSceneContext.h"
#include "Scene/SceneSettingsSerializer.h"
#include "Scene/SceneSerializer.h"
#include "Settings/ProjectSettingsStorage.h"
#include "Settings/ViewSettings.h"
#include "Core/Toolbox/ModuleIds.h"
#include "Toolbox/ToolboxManager.h"
#include "Utils/FileDialogs.h"
#include "Workspaces/LayoutWorkspace.h"
#include "Workspaces/MapWorkspace.h"
#include "Workspaces/MaterialWorkspace.h"
#include "Workspaces/UIWorkspace.h"
#include "Workspaces/ScriptingWorkspace.h"

#include <cstdio>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

// Forward declare ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Dot
{

Application* Application::s_Instance = nullptr;

static constexpr const wchar_t* WINDOW_CLASS_NAME = L"DotEditorWindow";
static constexpr const char* MCP_EDITOR_VERSION = "0.1.0";

namespace
{

bool WorkspaceUsesMainViewport(WorkspaceType workspace)
{
    switch (workspace)
    {
        case WorkspaceType::Layout:
        case WorkspaceType::Map:
            return true;
        case WorkspaceType::UI:
        case WorkspaceType::Scripting:
        case WorkspaceType::Material:
            return false;
    }

    return false;
}

bool WorkspaceUsesSceneDirtyPolling(WorkspaceType workspace)
{
    switch (workspace)
    {
        case WorkspaceType::Layout:
        case WorkspaceType::Map:
            return true;
        case WorkspaceType::UI:
        case WorkspaceType::Scripting:
        case WorkspaceType::Material:
            return false;
    }

    return false;
}

bool IsLegacyOnlyEnvironmentEntity(World& world, Entity entity)
{
    const bool allowedName = world.HasComponent<NameComponent>(entity);
    const bool allowedTransform = world.HasComponent<TransformComponent>(entity);
    const bool allowedHierarchy = world.HasComponent<HierarchyComponent>(entity);
    const bool allowedActive = world.HasComponent<ActiveComponent>(entity);

    size_t componentCount = 0;
    componentCount += allowedName ? 1u : 0u;
    componentCount += allowedTransform ? 1u : 0u;
    componentCount += allowedHierarchy ? 1u : 0u;
    componentCount += allowedActive ? 1u : 0u;
    componentCount += world.HasComponent<SkyboxComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<AmbientLightComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<MapComponent>(entity) ? 1u : 0u;

    const bool hasOtherSceneComponents =
        world.HasComponent<PrimitiveComponent>(entity) || world.HasComponent<MeshComponent>(entity) ||
        world.HasComponent<CameraComponent>(entity) || world.HasComponent<DirectionalLightComponent>(entity) ||
        world.HasComponent<PointLightComponent>(entity) || world.HasComponent<SpotLightComponent>(entity) ||
        world.HasComponent<ReflectionProbeComponent>(entity) || world.HasComponent<ScriptComponent>(entity);

    return !hasOtherSceneComponents && componentCount > 0;
}

struct McpSceneComponentBinding
{
    const char* name = nullptr;
    const char* reflectedTypeName = nullptr;
    const void* (*getConst)(World&, Entity) = nullptr;
    void* (*getMutable)(World&, Entity) = nullptr;
};

template <typename T> const void* GetSceneComponentConst(World& world, Entity entity)
{
    return world.GetComponent<T>(entity);
}

template <typename T> void* GetSceneComponentMutable(World& world, Entity entity)
{
    return world.GetComponent<T>(entity);
}

const std::vector<McpSceneComponentBinding>& GetMcpSceneComponentBindings()
{
    static const std::vector<McpSceneComponentBinding> bindings = {
        {"NameComponent", "NameComponent", &GetSceneComponentConst<NameComponent>, &GetSceneComponentMutable<NameComponent>},
        {"TransformComponent", "TransformComponent", &GetSceneComponentConst<TransformComponent>,
         &GetSceneComponentMutable<TransformComponent>},
        {"HierarchyComponent", "HierarchyComponent", &GetSceneComponentConst<HierarchyComponent>,
         &GetSceneComponentMutable<HierarchyComponent>},
        {"ActiveComponent", "ActiveComponent", &GetSceneComponentConst<ActiveComponent>, &GetSceneComponentMutable<ActiveComponent>},
        {"PrimitiveComponent", "PrimitiveComponent", &GetSceneComponentConst<PrimitiveComponent>,
         &GetSceneComponentMutable<PrimitiveComponent>},
        {"DirectionalLightComponent", "DirectionalLightComponent", &GetSceneComponentConst<DirectionalLightComponent>,
         &GetSceneComponentMutable<DirectionalLightComponent>},
        {"PointLightComponent", "PointLightComponent", &GetSceneComponentConst<PointLightComponent>,
         &GetSceneComponentMutable<PointLightComponent>},
        {"SpotLightComponent", "SpotLightComponent", &GetSceneComponentConst<SpotLightComponent>,
         &GetSceneComponentMutable<SpotLightComponent>},
        {"ReflectionProbeComponent", "ReflectionProbeComponent", &GetSceneComponentConst<ReflectionProbeComponent>,
         &GetSceneComponentMutable<ReflectionProbeComponent>},
        {"AmbientLightComponent", "AmbientLightComponent", &GetSceneComponentConst<AmbientLightComponent>,
         &GetSceneComponentMutable<AmbientLightComponent>},
        {"MeshComponent", "MeshComponent", &GetSceneComponentConst<MeshComponent>, &GetSceneComponentMutable<MeshComponent>},
        {"MaterialComponent", "MaterialComponent", &GetSceneComponentConst<MaterialComponent>,
         &GetSceneComponentMutable<MaterialComponent>},
        {"MapComponent", "MapComponent", &GetSceneComponentConst<MapComponent>, &GetSceneComponentMutable<MapComponent>},
        {"NavAgentComponent", "NavAgentComponent", &GetSceneComponentConst<NavAgentComponent>,
         &GetSceneComponentMutable<NavAgentComponent>},
        {"SkyboxComponent", "SkyboxComponent", &GetSceneComponentConst<SkyboxComponent>, &GetSceneComponentMutable<SkyboxComponent>},
        {"CameraComponent", "CameraComponent", &GetSceneComponentConst<CameraComponent>, &GetSceneComponentMutable<CameraComponent>},
        {"RigidBodyComponent", "RigidBodyComponent", &GetSceneComponentConst<RigidBodyComponent>,
         &GetSceneComponentMutable<RigidBodyComponent>},
        {"BoxColliderComponent", "BoxColliderComponent", &GetSceneComponentConst<BoxColliderComponent>,
         &GetSceneComponentMutable<BoxColliderComponent>},
        {"SphereColliderComponent", "SphereColliderComponent", &GetSceneComponentConst<SphereColliderComponent>,
         &GetSceneComponentMutable<SphereColliderComponent>},
        {"CharacterControllerComponent", "CharacterControllerComponent", &GetSceneComponentConst<CharacterControllerComponent>,
         &GetSceneComponentMutable<CharacterControllerComponent>},
        {"PlayerInputComponent", "PlayerInputComponent", &GetSceneComponentConst<PlayerInputComponent>,
         &GetSceneComponentMutable<PlayerInputComponent>},
        {"HealthComponent", "HealthComponent", &GetSceneComponentConst<HealthComponent>, &GetSceneComponentMutable<HealthComponent>},
        {"ScriptComponent", "ScriptComponent", &GetSceneComponentConst<ScriptComponent>, &GetSceneComponentMutable<ScriptComponent>},
        {"PrefabComponent", "PrefabComponent", &GetSceneComponentConst<PrefabComponent>,
         &GetSceneComponentMutable<PrefabComponent>},
    };

    return bindings;
}

std::string ToLowerCopy(std::string value);

std::string NormalizeComponentName(const std::string& value)
{
    std::string lowered = ToLowerCopy(value);
    if (lowered.size() >= 9 && lowered.rfind("component") == lowered.size() - 9)
        lowered.erase(lowered.size() - 9);
    return lowered;
}

const McpSceneComponentBinding* FindMcpSceneComponentBinding(const std::string& componentName)
{
    const std::string normalized = NormalizeComponentName(componentName);
    for (const McpSceneComponentBinding& binding : GetMcpSceneComponentBindings())
    {
        if (NormalizeComponentName(binding.name) == normalized)
            return &binding;
    }
    return nullptr;
}

const TypeInfo* GetMcpSceneComponentTypeInfo(const McpSceneComponentBinding& binding)
{
    return TypeRegistry::Get().GetType(binding.reflectedTypeName);
}

const Property* FindSceneComponentProperty(const McpSceneComponentBinding& binding, const std::string& propertyName)
{
    const TypeInfo* typeInfo = GetMcpSceneComponentTypeInfo(binding);
    return typeInfo != nullptr ? typeInfo->GetProperty(propertyName) : nullptr;
}

McpJson::Value MakeVec2Value(const Vec2& value)
{
    McpJson::Value result = McpJson::Value::MakeObject();
    result.objectValue["x"] = value.x;
    result.objectValue["y"] = value.y;
    return result;
}

McpJson::Value MakeVec3Value(const Vec3& value)
{
    McpJson::Value result = McpJson::Value::MakeObject();
    result.objectValue["x"] = value.x;
    result.objectValue["y"] = value.y;
    result.objectValue["z"] = value.z;
    return result;
}

McpJson::Value MakeEntityReferenceValue(Entity entity)
{
    if (!entity.IsValid())
        return McpJson::Value();

    McpJson::Value result = McpJson::Value::MakeObject();
    result.objectValue["id"] = entity.id;
    result.objectValue["index"] = entity.GetIndex();
    result.objectValue["generation"] = static_cast<uint32_t>(entity.GetGeneration());
    return result;
}

McpJson::Value SerializeScenePropertyValue(const Property& property, const void* object)
{
    if (object == nullptr || !property.getter)
        return McpJson::Value();

    void* valuePtr = property.getter(const_cast<void*>(object));
    if (valuePtr == nullptr)
        return McpJson::Value();

    switch (property.type)
    {
        case PropertyType::Bool:
            return *static_cast<bool*>(valuePtr);
        case PropertyType::Int32:
            if (property.size == sizeof(int8))
                return static_cast<int>(*static_cast<int8*>(valuePtr));
            if (property.size == sizeof(int16))
                return static_cast<int>(*static_cast<int16*>(valuePtr));
            return *static_cast<int32*>(valuePtr);
        case PropertyType::UInt32:
            if (property.size == sizeof(uint8))
                return static_cast<uint32_t>(*static_cast<uint8*>(valuePtr));
            if (property.size == sizeof(uint16))
                return static_cast<uint32_t>(*static_cast<uint16*>(valuePtr));
            return *static_cast<uint32*>(valuePtr);
        case PropertyType::Float:
            return static_cast<double>(*static_cast<float*>(valuePtr));
        case PropertyType::Double:
            return *static_cast<double*>(valuePtr);
        case PropertyType::String:
            return *static_cast<std::string*>(valuePtr);
        case PropertyType::Vec2:
            return MakeVec2Value(*static_cast<Vec2*>(valuePtr));
        case PropertyType::Vec3:
            return MakeVec3Value(*static_cast<Vec3*>(valuePtr));
        case PropertyType::Entity:
            return MakeEntityReferenceValue(*static_cast<Entity*>(valuePtr));
        default:
            return McpJson::Value();
    }
}

bool ParseVec2Value(const McpJson::Value& value, Vec2& out)
{
    if (value.IsArray())
    {
        if (value.arrayValue.size() != 2 || !value.arrayValue[0].IsNumber() || !value.arrayValue[1].IsNumber())
            return false;
        out.x = static_cast<float>(value.arrayValue[0].numberValue);
        out.y = static_cast<float>(value.arrayValue[1].numberValue);
        return true;
    }

    if (!value.IsObject())
        return false;

    const std::optional<double> x = McpJson::GetNumber(value, "x");
    const std::optional<double> y = McpJson::GetNumber(value, "y");
    if (!x.has_value() || !y.has_value())
        return false;

    out.x = static_cast<float>(*x);
    out.y = static_cast<float>(*y);
    return true;
}

bool ParseVec3Value(const McpJson::Value& value, Vec3& out)
{
    if (value.IsArray())
    {
        if (value.arrayValue.size() != 3 || !value.arrayValue[0].IsNumber() || !value.arrayValue[1].IsNumber() ||
            !value.arrayValue[2].IsNumber())
        {
            return false;
        }

        out.x = static_cast<float>(value.arrayValue[0].numberValue);
        out.y = static_cast<float>(value.arrayValue[1].numberValue);
        out.z = static_cast<float>(value.arrayValue[2].numberValue);
        return true;
    }

    if (!value.IsObject())
        return false;

    const std::optional<double> x = McpJson::GetNumber(value, "x");
    const std::optional<double> y = McpJson::GetNumber(value, "y");
    const std::optional<double> z = McpJson::GetNumber(value, "z");
    if (!x.has_value() || !y.has_value() || !z.has_value())
        return false;

    out.x = static_cast<float>(*x);
    out.y = static_cast<float>(*y);
    out.z = static_cast<float>(*z);
    return true;
}

std::optional<Entity> ParseEntityReferenceValue(const McpJson::Value& value, World& world, bool allowNull, std::string* errorMessage)
{
    if (value.IsNull())
    {
        if (allowNull)
            return kNullEntity;
        if (errorMessage)
            *errorMessage = "A non-null entity reference is required.";
        return std::nullopt;
    }

    Entity entity = kNullEntity;
    if (value.IsNumber())
    {
        entity = Entity(static_cast<uint32_t>(value.numberValue));
    }
    else if (value.IsObject())
    {
        if (const std::optional<double> id = McpJson::GetNumber(value, "id"))
        {
            entity = Entity(static_cast<uint32_t>(*id));
        }
        else
        {
            const std::optional<double> index = McpJson::GetNumber(value, "index");
            const std::optional<double> generation = McpJson::GetNumber(value, "generation");
            if (!index.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Entity references must include either id or index.";
                return std::nullopt;
            }
            entity = Entity(static_cast<uint32_t>(*index), generation.has_value() ? static_cast<uint8_t>(*generation) : 0u);
        }
    }
    else
    {
        if (errorMessage)
            *errorMessage = "Entity references must be null, a numeric id, or an object with id/index.";
        return std::nullopt;
    }

    if (!entity.IsValid())
    {
        if (allowNull)
            return kNullEntity;
        if (errorMessage)
            *errorMessage = "A non-null entity reference is required.";
        return std::nullopt;
    }

    if (!world.IsAlive(entity))
    {
        if (errorMessage)
            *errorMessage = "The referenced entity does not exist.";
        return std::nullopt;
    }

    return entity;
}

bool ApplySceneComponentPropertyValue(World& world,
                                      Entity entity,
                                      const McpSceneComponentBinding& binding,
                                      const std::string& propertyName,
                                      const McpJson::Value& value,
                                      std::string* errorMessage)
{
    void* object = binding.getMutable != nullptr ? binding.getMutable(world, entity) : nullptr;
    if (object == nullptr)
    {
        if (errorMessage)
            *errorMessage = "That entity does not have the requested component.";
        return false;
    }

    const Property* property = FindSceneComponentProperty(binding, propertyName);
    if (property == nullptr)
    {
        if (errorMessage)
            *errorMessage = "Unknown component property.";
        return false;
    }

    if (HasFlag(property->flags, PropertyFlags::ReadOnly) || HasFlag(property->flags, PropertyFlags::Transient))
    {
        if (errorMessage)
            *errorMessage = "That property is not writable through MCP.";
        return false;
    }

    if (std::string_view(binding.reflectedTypeName) == "HierarchyComponent" && property->name == "parent")
    {
        if (errorMessage)
            *errorMessage = "Hierarchy parent changes are not exposed through MCP yet.";
        return false;
    }

    switch (property->type)
    {
        case PropertyType::Bool:
        {
            const std::optional<bool> parsed = McpJson::GetBool(value);
            if (!parsed.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Expected a boolean value.";
                return false;
            }
            property->setter(object, &(*parsed));
            break;
        }
        case PropertyType::Int32:
        {
            const std::optional<double> parsed = McpJson::GetNumber(value);
            if (!parsed.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Expected a numeric value.";
                return false;
            }
            if (property->size == sizeof(int8))
            {
                const int8 converted = static_cast<int8>(*parsed);
                property->setter(object, &converted);
            }
            else if (property->size == sizeof(int16))
            {
                const int16 converted = static_cast<int16>(*parsed);
                property->setter(object, &converted);
            }
            else
            {
                const int32 converted = static_cast<int32>(*parsed);
                property->setter(object, &converted);
            }
            break;
        }
        case PropertyType::UInt32:
        {
            const std::optional<double> parsed = McpJson::GetNumber(value);
            if (!parsed.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Expected a numeric value.";
                return false;
            }
            if (property->size == sizeof(uint8))
            {
                const uint8 converted = static_cast<uint8>(*parsed);
                property->setter(object, &converted);
            }
            else if (property->size == sizeof(uint16))
            {
                const uint16 converted = static_cast<uint16>(*parsed);
                property->setter(object, &converted);
            }
            else
            {
                const uint32 converted = static_cast<uint32>(*parsed);
                property->setter(object, &converted);
            }
            break;
        }
        case PropertyType::Float:
        {
            const std::optional<double> parsed = McpJson::GetNumber(value);
            if (!parsed.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Expected a numeric value.";
                return false;
            }
            const float converted = static_cast<float>(*parsed);
            property->setter(object, &converted);
            break;
        }
        case PropertyType::Double:
        {
            const std::optional<double> parsed = McpJson::GetNumber(value);
            if (!parsed.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Expected a numeric value.";
                return false;
            }
            property->setter(object, &(*parsed));
            break;
        }
        case PropertyType::String:
        {
            const std::optional<std::string> parsed = McpJson::GetString(value);
            if (!parsed.has_value())
            {
                if (errorMessage)
                    *errorMessage = "Expected a string value.";
                return false;
            }
            property->setter(object, &(*parsed));
            break;
        }
        case PropertyType::Vec2:
        {
            Vec2 converted;
            if (!ParseVec2Value(value, converted))
            {
                if (errorMessage)
                    *errorMessage = "Expected a Vec2 object {x, y} or an array [x, y].";
                return false;
            }
            property->setter(object, &converted);
            break;
        }
        case PropertyType::Vec3:
        {
            Vec3 converted;
            if (!ParseVec3Value(value, converted))
            {
                if (errorMessage)
                    *errorMessage = "Expected a Vec3 object {x, y, z} or an array [x, y, z].";
                return false;
            }
            property->setter(object, &converted);
            break;
        }
        case PropertyType::Entity:
        {
            const std::optional<Entity> parsed = ParseEntityReferenceValue(value, world, true, errorMessage);
            if (!parsed.has_value())
                return false;
            property->setter(object, &(*parsed));
            break;
        }
        default:
            if (errorMessage)
                *errorMessage = "That property type is not supported by MCP yet.";
            return false;
    }

    if (std::string_view(binding.reflectedTypeName) == "TransformComponent")
    {
        if (auto* transform = static_cast<TransformComponent*>(object))
            transform->dirty = true;
    }

    return true;
}

McpJson::Value BuildSceneComponentState(World& world, Entity entity, const McpSceneComponentBinding& binding)
{
    McpJson::Value component = McpJson::Value::MakeObject();
    component.objectValue["name"] = binding.name;
    component.objectValue["typeName"] = binding.reflectedTypeName;

    const TypeInfo* typeInfo = GetMcpSceneComponentTypeInfo(binding);
    const void* object = binding.getConst != nullptr ? binding.getConst(world, entity) : nullptr;
    McpJson::Value properties = McpJson::Value::MakeObject();
    if (typeInfo != nullptr && object != nullptr)
    {
        for (const Property* property : typeInfo->GetAllProperties())
            properties.objectValue[property->name] = SerializeScenePropertyValue(*property, object);
    }

    component.objectValue["properties"] = std::move(properties);
    return component;
}

std::string GetEntityDisplayName(World& world, Entity entity)
{
    if (const auto* name = world.GetComponent<NameComponent>(entity))
        return name->name;
    return "Entity";
}

McpJson::Value BuildEntitySummary(World& world, Entity entity, bool includeComponents)
{
    McpJson::Value result = McpJson::Value::MakeObject();
    result.objectValue["entityId"] = entity.id;
    result.objectValue["index"] = entity.GetIndex();
    result.objectValue["generation"] = static_cast<uint32_t>(entity.GetGeneration());
    result.objectValue["name"] = GetEntityDisplayName(world, entity);

    if (const auto* hierarchy = world.GetComponent<HierarchyComponent>(entity))
    {
        result.objectValue["parent"] = MakeEntityReferenceValue(hierarchy->parent);
        McpJson::Value children = McpJson::Value::MakeArray();
        for (Entity child : hierarchy->children)
            children.arrayValue.push_back(MakeEntityReferenceValue(child));
        result.objectValue["children"] = std::move(children);
    }
    else
    {
        result.objectValue["parent"] = McpJson::Value();
        result.objectValue["children"] = McpJson::Value::MakeArray();
    }

    McpJson::Value componentNames = McpJson::Value::MakeArray();
    McpJson::Value components = McpJson::Value::MakeObject();
    for (const McpSceneComponentBinding& binding : GetMcpSceneComponentBindings())
    {
        if (binding.getConst == nullptr || binding.getConst(world, entity) == nullptr)
            continue;

        componentNames.arrayValue.emplace_back(binding.name);
        if (includeComponents)
            components.objectValue[binding.name] = BuildSceneComponentState(world, entity, binding);
    }

    result.objectValue["components"] = std::move(componentNames);
    if (includeComponents)
        result.objectValue["componentStates"] = std::move(components);

    return result;
}

class ReflectedScenePropertyCommand : public Command
{
public:
    ReflectedScenePropertyCommand(World* world,
                                  Entity entity,
                                  std::string componentName,
                                  std::string propertyName,
                                  McpJson::Value beforeValue,
                                  McpJson::Value afterValue)
        : m_World(world), m_Entity(entity), m_ComponentName(std::move(componentName)), m_PropertyName(std::move(propertyName)),
          m_BeforeValue(std::move(beforeValue)), m_AfterValue(std::move(afterValue))
    {
    }

    void Execute() override { Apply(m_AfterValue); }

    void Undo() override { Apply(m_BeforeValue); }

    bool CanUndo() const override { return true; }

    const char* GetName() const override { return "Edit Entity Property"; }

private:
    void Apply(const McpJson::Value& value)
    {
        if (!m_World || !m_Entity.IsValid() || !m_World->IsAlive(m_Entity))
            return;

        const McpSceneComponentBinding* binding = FindMcpSceneComponentBinding(m_ComponentName);
        if (binding == nullptr)
            return;

        std::string errorMessage;
        ApplySceneComponentPropertyValue(*m_World, m_Entity, *binding, m_PropertyName, value, &errorMessage);
    }

    World* m_World = nullptr;
    Entity m_Entity = kNullEntity;
    std::string m_ComponentName;
    std::string m_PropertyName;
    McpJson::Value m_BeforeValue;
    McpJson::Value m_AfterValue;
};

std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

const char* WorkspaceTypeToMcpName(WorkspaceType workspace)
{
    switch (workspace)
    {
        case WorkspaceType::Layout:
            return "layout";
        case WorkspaceType::Map:
            return "map";
        case WorkspaceType::UI:
            return "ui";
        case WorkspaceType::Scripting:
            return "scripting";
        case WorkspaceType::Material:
            return "material";
    }

    return "layout";
}

std::optional<WorkspaceType> ParseWorkspaceTypeName(const std::string& value)
{
    const std::string lowered = ToLowerCopy(value);
    if (lowered == "layout")
        return WorkspaceType::Layout;
    if (lowered == "map")
        return WorkspaceType::Map;
    if (lowered == "ui")
        return WorkspaceType::UI;
    if (lowered == "scripting")
        return WorkspaceType::Scripting;
    if (lowered == "material")
        return WorkspaceType::Material;
    return std::nullopt;
}

bool IsTextAssetExtension(const std::string& extension)
{
    return extension == ".txt" || extension == ".md" || extension == ".json" || extension == ".ini" ||
           extension == ".lua" || extension == ".hlsl" || extension == ".glsl" || extension == ".yaml" ||
           extension == ".yml" || extension == ".csv";
}

bool IsTextureAssetExtension(const std::string& extension)
{
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga";
}

std::string FormatUtcTime(const std::filesystem::file_time_type& timestamp)
{
    using namespace std::chrono;

    const auto systemTime =
        time_point_cast<system_clock::duration>(timestamp - std::filesystem::file_time_type::clock::now() + system_clock::now());
    const std::time_t timeValue = system_clock::to_time_t(systemTime);

    std::tm utcTime{};
    gmtime_s(&utcTime, &timeValue);

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::optional<std::filesystem::path> ResolveAssetPathWithinRoot(const std::filesystem::path& root,
                                                                const std::string& relativePath)
{
    std::error_code errorCode;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, errorCode);
    if (errorCode)
        return std::nullopt;

    std::filesystem::path resolved = relativePath.empty() ? canonicalRoot : (canonicalRoot / relativePath).lexically_normal();
    const std::string rootString = canonicalRoot.generic_string();
    const std::string resolvedString = resolved.generic_string();
    if (resolvedString.size() < rootString.size() || resolvedString.compare(0, rootString.size(), rootString) != 0)
        return std::nullopt;
    if (resolvedString.size() > rootString.size() && resolvedString[rootString.size()] != '/')
        return std::nullopt;
    return resolved;
}

} // namespace

Application::Application(const ApplicationConfig& config) : m_Config(config)
{
    DOT_ASSERT(s_Instance == nullptr);
    s_Instance = this;
}

Application::~Application()
{
    DOT_LOG_INFO("Shutting down Dot Engine Editor...");
    Shutdown();
    s_Instance = nullptr;
}

bool Application::Initialize(HINSTANCE hInstance)
{
    // Initialize logging system first
    Log::Initialize("DotEngine.log");

    // Initialize core systems
    MemorySystem::Get().Initialize(); // Memory first - others may use it
    JobSystem::Get().Initialize();
    AssetManager::Get().Initialize();
    ProjectSettingsStorage::Load();
    ToolboxManager::Get().Initialize();

    // Register component reflection for serialization
    RegisterSceneComponents();
    DOT_LOG_INFO("=================================");
    DOT_LOG_INFO("  Dot Engine Editor Starting");
    DOT_LOG_INFO("  Version: 0.1.0");
    DOT_LOG_INFO("  Platform: %s", DOT_PLATFORM_NAME);
#if DOT_DEBUG
    DOT_LOG_INFO("  Build: Debug");
#elif DOT_RELEASE
    DOT_LOG_INFO("  Build: Release");
#elif DOT_PROFILE
    DOT_LOG_INFO("  Build: Profile");
#endif
    DOT_LOG_INFO("=================================");

    m_Instance = hInstance;

    // Create console for debug output
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    std::printf("=================================\n");
    std::printf("  Dot Engine Editor v0.1.0\n");
    std::printf("  Platform: %s\n", DOT_PLATFORM_NAME);
#if DOT_DEBUG
    std::printf("  Build: Debug\n");
#elif DOT_RELEASE
    std::printf("  Build: Release\n");
#elif DOT_PROFILE
    std::printf("  Build: Profile\n");
#endif
    std::printf("=================================\n\n");

    if (!CreateAppWindow(hInstance))
    {
        std::printf("ERROR: Failed to create window\n");
        return false;
    }

    if (!InitializeRHI())
    {
        std::printf("ERROR: Failed to initialize RHI\n");
        return false;
    }

    if (!InitializeGUI())
    {
        std::printf("ERROR: Failed to initialize GUI\n");
        return false;
    }

    // Register all commands
    RegisterCreateCommands();

    // Create editor panels
    m_HierarchyPanel = std::make_unique<HierarchyPanel>();
    m_InspectorPanel = std::make_unique<InspectorPanel>();
    m_ViewportPanel = std::make_unique<ViewportPanel>();
    m_ConsolePanel = std::make_unique<ConsolePanel>();
    m_DebugPanel = std::make_unique<DebugPanel>();
    m_MaterialGraphPanel = std::make_unique<MaterialGraphPanel>();
    m_MaterialGraphPanel->SetDevice(m_Device.get());
    m_MaterialGraphPanel->SetGUI(m_GUI.get());
    m_MaterialGraphPanel->InitializePreview();

    // Asset Manager - set to project Assets folder
    m_AssetManagerPanel = std::make_unique<AssetManagerPanel>();
    m_AssetManagerPanel->SetDevice(m_Device.get());

    // Robust Assets folder discovery
    std::filesystem::path rootPath = std::filesystem::current_path();
    std::filesystem::path assetsPath;

    // Climb up from current directory to find Assets (max 5 levels)
    std::filesystem::path searchPath = rootPath;
    bool found = false;
    for (int i = 0; i < 5; ++i)
    {
        if (std::filesystem::exists(searchPath / "Assets"))
        {
            assetsPath = searchPath / "Assets";
            found = true;
            break;
        }
        if (searchPath.has_parent_path())
            searchPath = searchPath.parent_path();
        else
            break;
    }

    if (!found)
    {
        // Default to current directory if not found anywhere else
        assetsPath = rootPath / "Assets";
        if (!std::filesystem::exists(assetsPath))
        {
            std::filesystem::create_directories(assetsPath);
        }
    }

    DOT_LOG_INFO("Assets root discovered: %s", assetsPath.string().c_str());

    m_AssetManagerPanel->SetRootPath(assetsPath);
    AssetManager::Get().SetRootPath(assetsPath.string());
    m_AssetManagerPanel->Refresh();

    // Text editor panel
    m_TextEditorPanel = std::make_unique<TextEditorPanel>();

    // Wire up Asset Manager to open text files in the text editor
    m_AssetManagerPanel->SetOnOpenTextFile(
        [this](const std::filesystem::path& path)
        {
            if (m_TextEditorPanel)
                m_TextEditorPanel->OpenFile(path);
        });

    m_AssetManagerPanel->SetOnOpenSceneFile(
        [this](const std::filesystem::path& path)
        {
            QueueSceneOpen(path);
        });

    m_AssetManagerPanel->SetOnOpenMapFile(
        [this](const std::filesystem::path& path)
        {
            QueueMapOpen(path);
        });

    m_AssetManagerPanel->SetOnOpenUiFile(
        [this](const std::filesystem::path& path)
        {
            QueueUiOpen(path);
        });

    // Prefab viewer panel
    m_PrefabViewerPanel = std::make_unique<PrefabViewerPanel>();

    // Wire up Asset Manager to open prefab files in the prefab viewer
    m_AssetManagerPanel->SetOnOpenPrefabFile(
        [this](const std::filesystem::path& path)
        {
            if (m_PrefabViewerPanel)
            {
                m_PrefabViewerPanel->OpenPrefab(path);
                m_PrefabViewerPanel->SetOpen(true);
            }
        });

    // Texture viewer panel
    m_TextureViewerPanel = std::make_unique<TextureViewerPanel>();
    m_TextureViewerPanel->SetDevice(m_Device.get());
    m_TextureViewerPanel->SetGUI(m_GUI.get());
    m_DebugPanel->SetTextureViewerPanel(m_TextureViewerPanel.get());
    m_SceneSettingsPanel = std::make_unique<SceneSettingsPanel>();

    // Wire up Asset Manager to open texture files in the texture viewer
    m_AssetManagerPanel->SetOnOpenTextureFile(
        [this](const std::filesystem::path& path)
        {
            if (m_TextureViewerPanel)
            {
                m_TextureViewerPanel->OpenTexture(path);
                m_TextureViewerPanel->SetOpen(true);
            }
        });

    m_MapDocument = std::make_unique<MapDocument>();
    m_SceneMapDocument = std::make_unique<MapDocument>();
    m_SceneContext = std::make_unique<EditorSceneContext>();
    m_MapOutlinerPanel = std::make_unique<MapOutlinerPanel>();
    m_MapInspectorPanel = std::make_unique<MapInspectorPanel>();
    m_MapOutlinerPanel->SetDocument(m_MapDocument.get());
    m_MapInspectorPanel->SetDocument(m_MapDocument.get());

    DOT_LOG_INFO("Editor panels created");

    // Initialize viewport 3D rendering
    if (!m_ViewportPanel->Initialize(m_Device.get()))
    {
        CrashMetadata failure;
        failure.appName = "DotEditor";
        failure.exceptionName = "RENDERER_INITIALIZATION_FAILED";
        failure.exceptionCode = "RENDERER_INIT_FAILED";
        failure.exceptionAddress = "0x0";
        failure.stackSummary = "ViewportPanel::Initialize returned false during editor startup.\n";

        CrashHandling::ReportHandledFatalError(failure);
        std::printf("ERROR: Failed to initialize 3D rendering\n");
        return false;
    }

    // Connect hierarchy panel to viewport's ECS world
    m_HierarchyPanel->SetWorld(&m_ViewportPanel->GetWorld());
    m_SceneContext->BindWorld(&m_ViewportPanel->GetWorld());
    m_SceneContext->BindMapDocuments(m_MapDocument.get(), m_SceneMapDocument.get());
    m_HierarchyPanel->SetSceneContext(m_SceneContext.get());
    m_ViewportPanel->SetSceneContext(m_SceneContext.get());
    m_SceneSettingsPanel->SetSceneContext(m_SceneContext.get());
    m_InspectorPanel->SetReflectionProbeBakeCallback(
        [this](Entity entity)
        {
            if (m_ViewportPanel)
                m_ViewportPanel->RequestReflectionProbeBake(entity);
        });

    // Connect debug panel to ECS world
    m_DebugPanel->SetWorld(&m_ViewportPanel->GetWorld());
    m_DebugPanel->SetFrameGraph(&m_ViewportPanel->GetFrameGraph());
    m_ViewportPanel->SetMapDocument(m_MapDocument.get());
    m_ViewportPanel->SetSceneMapDocument(m_SceneMapDocument.get());

    // Initialize runtime scene path (physics/input/scripting execution)
    m_SceneRuntime = std::make_unique<SceneRuntime>();
    m_SceneRuntime->SetScriptConsoleCallback([](const std::string& msg) { DOT_LOG_INFO("[Script] %s", msg.c_str()); });
    m_SceneRuntime->SetModuleConfig(ToolboxManager::Get().BuildSceneRuntimeConfig());
    if (!m_SceneRuntime->Initialize(&m_ViewportPanel->GetWorld()))
    {
        std::printf("ERROR: Failed to initialize runtime scene systems\n");
        return false;
    }
    m_SceneRuntime->SetStaticWorldGeometry(m_SceneMapDocument->GetStaticWorldGeometry());

    // Initialize Workspaces
    m_LayoutWorkspace = std::make_unique<LayoutWorkspace>();
    m_LayoutWorkspace->SetPanels(m_HierarchyPanel.get(), m_InspectorPanel.get(), m_ViewportPanel.get(),
                                 m_ConsolePanel.get(), m_DebugPanel.get(), m_AssetManagerPanel.get(),
                                 m_TextEditorPanel.get(), m_PrefabViewerPanel.get(), m_TextureViewerPanel.get(),
                                 m_SceneSettingsPanel.get());
    m_LayoutWorkspace->SetSceneContext(m_SceneContext.get());

    m_MapWorkspace = std::make_unique<MapWorkspace>();
    m_MapWorkspace->SetPanels(m_MapOutlinerPanel.get(), m_MapInspectorPanel.get(), m_ViewportPanel.get(),
                              m_AssetManagerPanel.get());
    m_MapWorkspace->SetDocument(m_MapDocument.get());
    m_MapWorkspace->SetSceneContext(m_SceneContext.get());
    m_MapWorkspace->SetSaveMapCallback([this]() { return SaveMapIfNeeded(); });

    m_UIWorkspace = std::make_unique<UIWorkspace>();

    m_MaterialWorkspace = std::make_unique<MaterialWorkspace>();
    m_MaterialWorkspace->SetPanels(m_MaterialGraphPanel.get(), m_AssetManagerPanel.get());

    m_ScriptingWorkspace = std::make_unique<ScriptingWorkspace>();
    m_ScriptingWorkspace->SetScriptsPath(assetsPath / "Scripts");
    m_ScriptingWorkspace->SetConsolePanel(m_ConsolePanel.get());
    SyncToolboxBindings();
    m_McpBridgeService = std::make_unique<McpBridgeService>();
    RefreshMcpBridgeState();

    m_CurrentWorkspace = WorkspaceType::Layout;
    EnsureWorkspaceSupported();
    CommitSceneSnapshotBaseline();

    m_Running = true;
    return true;
}

bool Application::CreateAppWindow(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc))
        return false;

    RECT rect = {0, 0, static_cast<LONG>(m_Config.Width), static_cast<LONG>(m_Config.Height)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_Window =
        CreateWindowExW(0, WINDOW_CLASS_NAME, m_Config.Title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr);

    if (!m_Window)
        return false;

    std::printf("Window created (%ux%u)\n", m_Config.Width, m_Config.Height);

    ShowWindow(m_Window, SW_SHOW);
    UpdateWindow(m_Window);

    return true;
}

bool Application::InitializeRHI()
{
    std::printf("Initializing RHI...\n");

    m_Device = CreateRHIDevice();
    if (!m_Device)
        return false;

    std::printf("  GPU: %s (%s)\n", m_Device->GetName(), m_Device->GetVendor());

    RHISwapChainDesc swapChainDesc;
    swapChainDesc.WindowHandle = m_Window;
    swapChainDesc.Width = m_Config.Width;
    swapChainDesc.Height = m_Config.Height;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.VSync = m_Config.VSync;

    m_SwapChain = CreateSwapChain(m_Device.get(), swapChainDesc);
    if (!m_SwapChain)
        return false;

    std::printf("  SwapChain: %ux%u\n", m_SwapChain->GetWidth(), m_SwapChain->GetHeight());

    // Dark blue-gray clear color
    m_SwapChain->SetClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    return true;
}

bool Application::InitializeGUI()
{
    std::printf("Initializing GUI...\n");

    RHIGUIDesc guiDesc;
    guiDesc.WindowHandle = m_Window;
    guiDesc.Width = m_Config.Width;
    guiDesc.Height = m_Config.Height;

    m_GUI = CreateGUI(m_Device.get(), m_SwapChain.get(), guiDesc);
    if (!m_GUI)
        return false;

    std::printf("GUI initialized!\n\n");
    return true;
}

void Application::Shutdown()
{
    std::printf("Shutting down core systems...\n");
    ProjectSettingsStorage::Save();

    // Flush GPU before releasing resources
    if (m_Device)
    {
        m_Device->Present(); // Blocks until GPU is idle in current single-allocator model
    }

    // Break non-owning workspace links before runtime teardown.
    if (m_ScriptingWorkspace)
    {
        m_ScriptingWorkspace->SetScriptSystem(nullptr);
    }

    if (m_SceneRuntime)
    {
        m_SceneRuntime->Shutdown();
        m_SceneRuntime.reset();
    }

    if (m_McpBridgeService)
    {
        m_McpBridgeService->Shutdown();
        m_McpBridgeService.reset();
    }

    // Shut down core systems after all runtime systems are down.
    AssetManager::Get().Shutdown();
    JobSystem::Get().Shutdown();
    MemorySystem::Get().Shutdown();

    // Console panel registers a global log callback; clear listeners before panel destruction.
    Log::ClearListeners();

    // Explicitly release editor UI objects before GUI/device teardown.
    m_ScriptingWorkspace.reset();
    m_MaterialWorkspace.reset();
    m_UIWorkspace.reset();
    m_MapWorkspace.reset();
    m_LayoutWorkspace.reset();

    m_MapDocument.reset();
    m_MapInspectorPanel.reset();
    m_MapOutlinerPanel.reset();
    m_TextureViewerPanel.reset();
    m_PrefabViewerPanel.reset();
    m_TextEditorPanel.reset();
    m_AssetManagerPanel.reset();
    m_MaterialGraphPanel.reset();
    m_DebugPanel.reset();
    m_InspectorPanel.reset();
    m_HierarchyPanel.reset();
    m_ViewportPanel.reset();
    m_ConsolePanel.reset();

    m_GUI.reset();
    m_SwapChain.reset();
    m_Device.reset();

    if (m_Window)
    {
        DestroyWindow(m_Window);
        UnregisterClassW(WINDOW_CLASS_NAME, m_Instance);
        m_Window = nullptr;
    }

    FreeConsole();
    ToolboxManager::Get().Shutdown();
}

int Application::Run()
{
    std::printf("Entering main loop...\n");

    while (m_Running)
    {
        // Process messages
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_Running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (m_McpBridgeService)
        {
            m_McpBridgeService->Pump(
                [this](const McpBridgeCommandRequest& request, McpBridgeService::Completion completion)
                {
                    HandleMcpBridgeRequest(request, std::move(completion));
                });
        }

        if (!m_Running || m_Minimized)
            continue;

        ProcessDeferredAssetActions();

        // Begin frame - memory system first
        MemorySystem::Get().BeginFrame();
        m_Device->BeginFrame();
        if (ViewSettings::Get().debugVisMode == DebugVisMode::Overdraw)
            m_SwapChain->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        else
            m_SwapChain->SetClearColor(0.2f, 0.1f, 0.3f, 1.0f); // Purple background
        m_SwapChain->BeginFrame();

        // ----- 3D Rendering -----
        const bool renderMainViewport = IsInPlayMode() || WorkspaceUsesMainViewport(m_CurrentWorkspace);

        if (m_ViewportPanel && renderMainViewport)
        {
            // Reset debug stats for this frame
            if (m_DebugPanel)
                m_DebugPanel->BeginFrame();

            m_ViewportPanel->RenderScene(m_SwapChain.get());

            // Finalize debug stats
            if (m_DebugPanel)
                m_DebugPanel->EndFrame();
        }
        // ------------------------

        m_GUI->BeginFrame();

        // Process viewport input AFTER ImGui BeginFrame (ImGui IO is now valid)
        // This updates transforms via gizmo before the next frame's RenderScene
        if (m_ViewportPanel && renderMainViewport)
            m_ViewportPanel->ProcessInput();


        // ----- Editor UI -----
        // Enable docking over main viewport with transparent background
        ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

        // Main menu bar (File, Edit, etc.)
        DrawMainMenuBar();

        // Debug visualization mode overlay (top-left of viewport background)
        if (renderMainViewport)
            DrawDebugVisOverlay();

        // Toolbar with play controls (only in Layout workspace)
        if (m_CurrentWorkspace == WorkspaceType::Layout)
            DrawToolbar();

        // Update play time
        if (m_PlayState == PlayState::Playing)
        {
            // Use ImGui's delta time which is calculated correctly between frames
            float dt = ImGui::GetIO().DeltaTime;
            m_PlayTime += dt;

            if (m_SceneRuntime)
            {
                // Update camera/viewport info for Lua bindings
                if (m_ViewportPanel)
                {
                    // Get camera info from viewport
                    const auto& cam = m_ViewportPanel->GetCamera();
                    ScriptSystem::CameraInfo camInfo;
                    camInfo.posX = cam.GetPosX();
                    camInfo.posY = cam.GetPosY();
                    camInfo.posZ = cam.GetPosZ();
                    cam.GetForward(camInfo.forwardX, camInfo.forwardY, camInfo.forwardZ);
                    cam.GetUp(camInfo.upX, camInfo.upY, camInfo.upZ);
                    cam.GetRight(camInfo.rightX, camInfo.rightY, camInfo.rightZ);
                    camInfo.fovDegrees = cam.GetFOVDegrees();
                    camInfo.nearZ = cam.GetNearZ();
                    camInfo.farZ = cam.GetFarZ();
                    m_SceneRuntime->SetCameraInfo(camInfo);

                    ScriptSystem::ViewportInfo viewportInfo;
                    viewportInfo.x = m_ViewportPanel->GetViewportPosX();
                    viewportInfo.y = m_ViewportPanel->GetViewportPosY();
                    viewportInfo.width = m_ViewportPanel->GetViewportWidth();
                    viewportInfo.height = m_ViewportPanel->GetViewportHeight();
                    m_SceneRuntime->SetViewportInfo(viewportInfo);
                }

                m_SceneRuntime->Tick(dt);
            }
        }

        // Only draw editor panels when NOT in play mode
        if (!IsInPlayMode())
        {
            // Draw active workspace
            switch (m_CurrentWorkspace)
            {
                case WorkspaceType::Layout:
                    if (m_LayoutWorkspace)
                        m_LayoutWorkspace->OnImGui();
                    break;
                case WorkspaceType::Map:
                    if (m_MapWorkspace)
                        m_MapWorkspace->OnImGui();
                    break;
                case WorkspaceType::UI:
                    if (m_UIWorkspace)
                        m_UIWorkspace->OnImGui();
                    break;
                case WorkspaceType::Scripting:
                    if (m_ScriptingWorkspace)
                        m_ScriptingWorkspace->OnImGui();
                    break;
                case WorkspaceType::Material:
                    if (m_MaterialWorkspace)
                        m_MaterialWorkspace->OnImGui();
                    break;
            }

            if (m_SceneSettingsPanel && m_CurrentWorkspace != WorkspaceType::Layout)
                m_SceneSettingsPanel->OnImGui();

            RefreshSceneDirtyState(ImGui::GetIO().DeltaTime);
        }
        else
        {
            // Play mode: just render viewport
            m_ViewportPanel->OnImGui();
        }

        DrawModalDialogs();
        // ---------------------

        // End frame
        m_GUI->EndFrame();
        m_SwapChain->EndFrame();

        m_Device->EndFrame();
        m_Device->Submit();
        m_SwapChain->Present();
        m_Device->Present();

        // End frame - memory system last (invalidates frame allocations)
        MemorySystem::Get().EndFrame();
    }

    return 0;
}

void Application::OnResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        m_Minimized = true;
        return;
    }
    m_Minimized = false;

    if (m_SwapChain)
        m_SwapChain->Resize(width, height);
    if (m_GUI)
        m_GUI->OnResize(width, height);
}

LRESULT CALLBACK Application::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Let ImGui handle input first
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    Application* app = Application::Get();

    switch (msg)
    {
        case WM_CLOSE:
            if (app)
                app->RequestExit();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && app && app->m_GUI && !app->m_GUI->WantCaptureKeyboard())
                app->RequestShutdown();
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && app)
                app->OnResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// =============================================================================
// Menu Bar and Scene Management
// =============================================================================

void Application::DrawMainMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            {
                NewScene();
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            {
                OpenScene();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save", "Ctrl+S"))
            {
                SaveScene();
            }

            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
            {
                SaveSceneAs();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Export Game..."))
            {
                OpenExportGameModal();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                RequestExit();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            auto& cmdRegistry = CommandRegistry::Get();

            // Undo with description
            std::string undoLabel = "Undo";
            if (auto* desc = cmdRegistry.GetUndoDescription())
                undoLabel = std::string("Undo ") + desc;
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, cmdRegistry.CanUndo()))
            {
                cmdRegistry.Undo();
                RefreshSceneDirtyState(0.0f, true);
            }

            // Redo with description
            std::string redoLabel = "Redo";
            if (auto* desc = cmdRegistry.GetRedoDescription())
                redoLabel = std::string("Redo ") + desc;
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, cmdRegistry.CanRedo()))
            {
                cmdRegistry.Redo();
                RefreshSceneDirtyState(0.0f, true);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, false))
            {
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, false))
            {
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, false))
            {
            }
            ImGui::EndMenu();
        }

        // Keyboard shortcuts for undo/redo (global, not just in menu)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            CommandRegistry::Get().Undo();
            RefreshSceneDirtyState(0.0f, true);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        {
            CommandRegistry::Get().Redo();
            RefreshSceneDirtyState(0.0f, true);
        }

        if (ImGui::BeginMenu("Toolbox"))
        {
            if (ImGui::MenuItem("Manage Modules..."))
                OpenToolboxModal();
            ImGui::Separator();
            if (ImGui::MenuItem("Auto Generate Reflection Probes"))
                RunAutoReflectionProbeUtility();
            if (ImGui::MenuItem("Clear Auto Reflection Probes"))
                ClearAutoReflectionProbeUtility();
            if (!m_ToolboxUtilityStatusMessage.empty())
            {
                ImGui::Separator();
                if (m_ToolboxUtilityStatusIsError)
                    ImGui::TextColored(ImVec4(0.92f, 0.44f, 0.44f, 1.0f), "%s", m_ToolboxUtilityStatusMessage.c_str());
                else
                    ImGui::TextDisabled("%s", m_ToolboxUtilityStatusMessage.c_str());
                ImGui::Separator();
            }
            ImGui::TextDisabled("Runtime");
            ImGui::Text("Physics: %s", ToolboxManager::Get().IsPhysicsEnabled() ? "On" : "Off");
            ImGui::Text("Navigation: %s", ToolboxManager::Get().IsNavigationEnabled() ? "On" : "Off");
            ImGui::Text("Scripting: %s", ToolboxManager::Get().IsScriptingEnabled() ? "On" : "Off");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Hierarchy Panel"))
            { /* toggle */
            }
            if (ImGui::MenuItem("Inspector Panel"))
            { /* toggle */
            }
            if (ImGui::MenuItem("Viewport Panel"))
            { /* toggle */
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Scene Settings", nullptr, m_SceneSettingsPanel && m_SceneSettingsPanel->IsOpen()))
            {
                if (m_SceneSettingsPanel)
                    m_SceneSettingsPanel->SetOpen(!m_SceneSettingsPanel->IsOpen());
            }
            if (ImGui::MenuItem("Settings..."))
            {
                SettingsPanel::Open();
            }
            if (ImGui::MenuItem("View Settings..."))
            {
                SettingsPanel::Open(SettingsPanel::Category::View);
            }
            if (ToolboxManager::Get().IsMaterialEditorEnabled())
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Material Graph"))
                {
                    // Material Graph panel is always visible as a dockable window
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug"))
        {
            if (ImGui::MenuItem("Debug Suite", nullptr, m_DebugPanel && m_DebugPanel->IsVisible()))
            {
                if (m_DebugPanel)
                    m_DebugPanel->Toggle();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About Dot Engine"))
            {
                // TODO: Show about dialog
            }
            ImGui::EndMenu();
        }

        DrawWorkspaceTabs();

        // Show scene info on the right side
        if (m_SceneContext && !m_SceneContext->GetScenePath().empty())
        {
            // Extract filename from path
            const std::string& scenePath = m_SceneContext->GetScenePath();
            size_t pos = scenePath.find_last_of("\\/");
            std::string filename = (pos != std::string::npos) ? scenePath.substr(pos + 1) : scenePath;
            if (m_SceneContext->IsSceneDirty())
                filename += " *";

            float textWidth = ImGui::CalcTextSize(filename.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", filename.c_str());
        }
        else
        {
            const char* unsaved = (m_SceneContext && m_SceneContext->IsSceneDirty()) ? "Unsaved Scene *" : "Untitled Scene";
            float textWidth = ImGui::CalcTextSize(unsaved).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", unsaved);
        }

        ImGui::EndMainMenuBar();
    }
}

void Application::NewScene()
{
    ExecuteSceneActionWithSavePrompt([this]() { CreateFreshScene(); }, "create a new scene");
}

void Application::OpenScene()
{
    FileFilter filter{"Dot Scene Files", "*.dotscene"};
    std::string filepath = FileDialogs::OpenFile(filter);

    if (!filepath.empty())
    {
        OpenScene(std::filesystem::path(filepath));
    }
}

void Application::OpenScene(const std::filesystem::path& path)
{
    ExecuteSceneActionWithSavePrompt([this, path]() { LoadSceneFromPath(path); }, "open another scene");
}

void Application::QueueSceneOpen(const std::filesystem::path& path)
{
    m_DeferredAssetActionType = DeferredAssetActionType::OpenScene;
    m_DeferredAssetActionPath = path;
}

void Application::OpenMap(const std::filesystem::path& path)
{
    if (!ToolboxManager::Get().IsMapEditorEnabled())
    {
        DOT_LOG_WARN("Map Editor is disabled in Toolbox. Re-enable it to edit map assets.");
        OpenToolboxModal();
        return;
    }

    if (!m_MapDocument)
        return;

    if (!m_MapDocument->Load(path))
    {
        DOT_LOG_ERROR("Failed to open map: %s", m_MapDocument->GetLastError().c_str());
        return;
    }

    EnsureSceneMapReference(path);
    SyncSceneMapDocumentFromScene();
    SwitchWorkspace(WorkspaceType::Map);
}

void Application::QueueMapOpen(const std::filesystem::path& path)
{
    m_DeferredAssetActionType = DeferredAssetActionType::OpenMap;
    m_DeferredAssetActionPath = path;
}

void Application::OpenUI(const std::filesystem::path& path)
{
    if (!ToolboxManager::Get().IsUIEditorEnabled())
    {
        DOT_LOG_WARN("UI Editor is disabled in Toolbox. Re-enable it to edit UI assets.");
        OpenToolboxModal();
        return;
    }

    if (!m_UIWorkspace)
        return;

    if (!m_UIWorkspace->OpenAsset(path))
    {
        DOT_LOG_ERROR("Failed to open UI asset: %s", path.string().c_str());
        return;
    }

    SwitchWorkspace(WorkspaceType::UI);
}

void Application::QueueUiOpen(const std::filesystem::path& path)
{
    m_DeferredAssetActionType = DeferredAssetActionType::OpenUI;
    m_DeferredAssetActionPath = path;
}

std::filesystem::path Application::ResolveDefaultMapPath() const
{
    const std::filesystem::path assetsRoot = AssetManager::Get().GetRootPath();
    const std::filesystem::path mapsRoot = assetsRoot / "Maps";
    std::filesystem::create_directories(mapsRoot);

    if (m_SceneContext && !m_SceneContext->GetScenePath().empty())
        return mapsRoot / (std::filesystem::path(m_SceneContext->GetScenePath()).stem().string() + ".dotmap");
    return mapsRoot / "Untitled.dotmap";
}

void Application::EnsureSceneMapReference(const std::filesystem::path& mapPath)
{
    if (!m_SceneContext)
        return;

    std::filesystem::path relativePath = mapPath;
    const std::filesystem::path assetsRoot = AssetManager::Get().GetRootPath();
    if (relativePath.is_absolute())
        relativePath = std::filesystem::relative(relativePath, assetsRoot);

    SceneSettingsAsset& settings = m_SceneContext->GetSceneSettings();
    settings.mapPath = relativePath.generic_string();
    settings.mapVisible = true;
    settings.mapCollisionEnabled = true;
    m_SceneContext->SetSceneDirty(true);
}

std::filesystem::path Application::ResolveSceneSettingsPath(const std::filesystem::path& scenePath) const
{
    return scenePath.string().empty() ? std::filesystem::path{} : std::filesystem::path(scenePath.string() + ".settings.json");
}

bool Application::SaveSceneSettings(const std::filesystem::path& scenePath)
{
    if (!m_SceneContext || scenePath.empty())
        return false;

    const std::filesystem::path settingsPath = ResolveSceneSettingsPath(scenePath);
    std::filesystem::create_directories(settingsPath.parent_path());

    SceneSettingsSerializer serializer;
    if (!serializer.Save(m_SceneContext->GetSceneSettings(), settingsPath))
    {
        DOT_LOG_ERROR("Failed to save scene settings: %s", serializer.GetLastError().c_str());
        return false;
    }
    return true;
}

bool Application::LoadSceneSettingsFromSerializer(const std::filesystem::path& scenePath, const SceneSerializer& serializer)
{
    if (!m_SceneContext)
        return false;

    m_SceneContext->ResetSceneSettings();
    const std::string& settingsReference = serializer.GetSceneSettingsReference();
    if (settingsReference.empty())
        return false;

    std::filesystem::path settingsPath = scenePath.parent_path() / settingsReference;
    if (!std::filesystem::exists(settingsPath))
        settingsPath = ResolveSceneSettingsPath(scenePath);
    if (!std::filesystem::exists(settingsPath))
        return false;

    SceneSettingsSerializer settingsSerializer;
    if (!settingsSerializer.Load(m_SceneContext->GetSceneSettings(), settingsPath))
    {
        DOT_LOG_WARN("Failed to load scene settings sidecar: %s", settingsSerializer.GetLastError().c_str());
        return false;
    }
    return true;
}

void Application::MigrateLegacySceneGlobalsToSceneSettings()
{
    if (!m_SceneContext || !m_ViewportPanel)
        return;

    World& world = m_ViewportPanel->GetWorld();
    SceneSettingsAsset& settings = m_SceneContext->GetSceneSettings();
    std::vector<Entity> entitiesToDestroy;

    world.EachEntity(
        [&](Entity entity)
        {
            bool removeEntity = false;

            if (AmbientLightComponent* ambient = world.GetComponent<AmbientLightComponent>(entity))
            {
                settings.ambientEnabled = true;
                settings.ambientColorR = ambient->color.x;
                settings.ambientColorG = ambient->color.y;
                settings.ambientColorB = ambient->color.z;
                settings.ambientIntensity = ambient->intensity;
                world.RemoveComponent<AmbientLightComponent>(entity);
                removeEntity = true;
            }

            if (MapComponent* map = world.GetComponent<MapComponent>(entity))
            {
                settings.mapPath = map->mapPath;
                settings.mapVisible = map->visible;
                settings.mapCollisionEnabled = map->collisionEnabled;
                world.RemoveComponent<MapComponent>(entity);
                removeEntity = true;
            }

            if (removeEntity && IsLegacyOnlyEnvironmentEntity(world, entity))
                entitiesToDestroy.push_back(entity);
        });

    for (Entity entity : entitiesToDestroy)
    {
        if (world.IsAlive(entity))
            world.DestroyEntity(entity);
    }
}

bool Application::SaveMapIfNeeded()
{
    if (!m_MapDocument)
        return true;

    if (m_MapDocument->GetAsset().brushes.empty() && !m_MapDocument->HasPath())
        return true;

    if (!m_MapDocument->HasPath())
        m_MapDocument->SetPath(ResolveDefaultMapPath());

    std::filesystem::create_directories(m_MapDocument->GetPath().parent_path());
    EnsureSceneMapReference(m_MapDocument->GetPath());
    if (!m_MapDocument->Save())
    {
        DOT_LOG_ERROR("Failed to save map: %s", m_MapDocument->GetLastError().c_str());
        return false;
    }

    SyncSceneMapDocumentFromEditingDocument();

    return true;
}

void Application::SyncMapDocumentFromScene()
{
    if (!m_MapDocument || !m_SceneContext)
        return;

    const std::string& mapPath = m_SceneContext->GetSceneSettings().mapPath;
    if (mapPath.empty())
    {
        m_MapDocument->New();
    }
    else
    {
        const std::filesystem::path fullPath = std::filesystem::path(AssetManager::Get().GetRootPath()) / mapPath;
        if (std::filesystem::exists(fullPath))
        {
            if (!m_MapDocument->Load(fullPath))
                DOT_LOG_ERROR("Failed to load scene map: %s", m_MapDocument->GetLastError().c_str());
        }
        else
        {
            m_MapDocument->New();
            m_MapDocument->SetPath(fullPath);
        }
    }

    SyncSceneMapDocumentFromScene();
}

void Application::SyncSceneMapDocumentFromScene()
{
    if (!m_SceneMapDocument || !m_SceneContext)
        return;

    const std::string& mapPath = m_SceneContext->GetSceneSettings().mapPath;
    if (mapPath.empty())
    {
        m_SceneMapDocument->New();
    }
    else
    {
        const std::filesystem::path fullPath = std::filesystem::path(AssetManager::Get().GetRootPath()) / mapPath;
        if (std::filesystem::exists(fullPath))
        {
            if (!m_SceneMapDocument->Load(fullPath))
                DOT_LOG_ERROR("Failed to load scene render map: %s", m_SceneMapDocument->GetLastError().c_str());
        }
        else
        {
            m_SceneMapDocument->New();
            m_SceneMapDocument->SetPath(fullPath);
        }
    }

    if (m_SceneRuntime)
        m_SceneRuntime->SetStaticWorldGeometry(m_SceneMapDocument->GetStaticWorldGeometry());
}

void Application::SyncSceneMapDocumentFromEditingDocument()
{
    if (!m_SceneMapDocument || !m_MapDocument)
        return;

    m_SceneMapDocument->SetPath(m_MapDocument->GetPath());
    m_SceneMapDocument->ApplySnapshot(m_MapDocument->GetAsset(), {}, MapSelection{}, {}, {}, false);

    if (m_SceneRuntime)
        m_SceneRuntime->SetStaticWorldGeometry(m_SceneMapDocument->GetStaticWorldGeometry());
}

bool Application::SaveScene()
{
    if (!m_SceneContext || m_SceneContext->GetScenePath().empty())
    {
        return SaveSceneAs();
    }

    if (m_ViewportPanel)
    {
        if (!SaveMapIfNeeded())
            return false;
        SceneSerializer serializer;
        if (!SaveSceneSettings(m_SceneContext->GetScenePath()))
            return false;
        serializer.SetSceneSettingsReference(ResolveSceneSettingsPath(m_SceneContext->GetScenePath()).filename().generic_string());
        if (serializer.Save(m_ViewportPanel->GetWorld(), m_SceneContext->GetScenePath()))
        {
            m_SceneContext->SetSceneDirty(false);
            CommitSceneSnapshotBaseline();
            UpdateWindowTitle();
            std::printf("Scene saved: %s\n", m_SceneContext->GetScenePath().c_str());
            return true;
        }
        else
        {
            std::printf("Failed to save scene: %s\n", serializer.GetLastError().c_str());
        }
    }

    return false;
}

bool Application::SaveSceneAs()
{
    FileFilter filter{"Dot Scene Files", "*.dotscene"};
    std::string filepath = FileDialogs::SaveFile(filter, "scene.dotscene");

    if (!filepath.empty() && m_ViewportPanel)
    {
        if (!m_MapDocument->HasPath() && !m_MapDocument->GetAsset().brushes.empty())
            m_MapDocument->SetPath((std::filesystem::path(filepath).parent_path() / "Maps" /
                                    (std::filesystem::path(filepath).stem().string() + ".dotmap")));
        if (!SaveMapIfNeeded())
            return false;

        SceneSerializer serializer;
        if (!SaveSceneSettings(filepath))
            return false;
        serializer.SetSceneSettingsReference(ResolveSceneSettingsPath(filepath).filename().generic_string());
        if (serializer.Save(m_ViewportPanel->GetWorld(), filepath))
        {
            if (m_SceneContext)
            {
                m_SceneContext->SetScenePath(filepath);
                m_SceneContext->SetSceneDirty(false);
            }
            CrashHandling::SetScenePath(filepath);
            CommitSceneSnapshotBaseline();
            UpdateWindowTitle();
            std::printf("Scene saved: %s\n", filepath.c_str());
            return true;
        }
        else
        {
            std::printf("Failed to save scene: %s\n", serializer.GetLastError().c_str());
        }
    }

    return false;
}

void Application::RequestExit()
{
    ExecuteSceneActionWithSavePrompt([this]() { RequestShutdown(); }, "exit the editor");
}

void Application::DrawModalDialogs()
{
    DrawToolboxModal();
    DrawExportGameModal();
    m_ConfirmationDialog.Draw();
}

void Application::OpenToolboxModal()
{
    ToolboxManager::Get().ResetDraft();
    m_ToolboxSelectedCategory = "All";
    m_RequestOpenToolboxModal = true;
}

void Application::RunAutoReflectionProbeUtility()
{
    if (!m_ViewportPanel)
    {
        m_ToolboxUtilityStatusMessage = "Viewport is unavailable, so probe generation could not run.";
        m_ToolboxUtilityStatusIsError = true;
        return;
    }

    const ReflectionProbeAutoGenerateResult result = m_ViewportPanel->RegenerateAutomaticReflectionProbes();
    m_ToolboxUtilityStatusMessage = "Generated " + std::to_string(result.createdProbeCount) + " auto probe" +
                                    (result.createdProbeCount == 1 ? "" : "s") + " from " +
                                    std::to_string(result.sourceBoundsCount) + " scene bounds and queued " +
                                    std::to_string(result.queuedBakeCount) + " bake" +
                                    (result.queuedBakeCount == 1 ? "" : "s") + ".";
    if (result.removedProbeCount > 0)
        m_ToolboxUtilityStatusMessage += " Replaced " + std::to_string(result.removedProbeCount) + " existing auto probe" +
                                         (result.removedProbeCount == 1 ? "" : "s") + ".";
    m_ToolboxUtilityStatusIsError = false;
}

void Application::ClearAutoReflectionProbeUtility()
{
    if (!m_ViewportPanel)
    {
        m_ToolboxUtilityStatusMessage = "Viewport is unavailable, so auto probes could not be cleared.";
        m_ToolboxUtilityStatusIsError = true;
        return;
    }

    const uint32_t removedCount = m_ViewportPanel->ClearAutomaticReflectionProbes();
    m_ToolboxUtilityStatusMessage =
        removedCount > 0 ? ("Cleared " + std::to_string(removedCount) + " auto reflection probe" +
                            (removedCount == 1 ? "." : "s."))
                         : "No auto reflection probes were found to clear.";
    m_ToolboxUtilityStatusIsError = false;
}

void Application::OpenExportGameModal()
{
    const std::filesystem::path scenePath = m_SceneContext ? std::filesystem::path(m_SceneContext->GetScenePath()) : std::filesystem::path{};
    const std::string sceneStem = scenePath.empty() ? "Game" : scenePath.stem().string();

    m_ExportGameDialog.outputDirectory = (std::filesystem::current_path() / "Exports" / sceneStem).string();
    m_ExportGameDialog.projectAsset = DotProjectAsset{};
    m_ExportGameDialog.projectAsset.gameName = sceneStem.empty() ? "Dot Game" : sceneStem;
    m_ExportGameDialog.projectAsset.windowWidth = m_Config.Width;
    m_ExportGameDialog.projectAsset.windowHeight = m_Config.Height;
    m_ExportGameDialog.projectAsset.captureMouseOnStart = true;
    m_ExportGameDialog.projectAsset.startFullscreen = false;
    m_ExportGameDialog.projectAsset.startupScene = ResolveRelativeScenePathInAssets().generic_string();
    m_ExportGameDialog.cookDependenciesOnly = true;
    m_ExportGameDialog.statusMessage.clear();
    m_ExportGameDialog.statusIsError = false;
    m_RequestOpenExportGameModal = true;
}

void Application::DrawExportGameModal()
{
    if (m_RequestOpenExportGameModal)
    {
        ImGui::OpenPopup("Export Game");
        m_RequestOpenExportGameModal = false;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal("Export Game", &keepOpen, ImGuiWindowFlags_NoResize))
        return;

    if (!keepOpen)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    char outputBuffer[512] = {};
    std::strncpy(outputBuffer, m_ExportGameDialog.outputDirectory.c_str(), sizeof(outputBuffer) - 1);
    ImGui::TextDisabled("Stages DotGame.exe, Game.dotproject, Assets, and Shaders into a runnable folder.");
    ImGui::Spacing();

    ImGui::Text("Output Folder");
    ImGui::SetNextItemWidth(-110.0f);
    if (ImGui::InputText("##ExportOutputFolder", outputBuffer, sizeof(outputBuffer)))
        m_ExportGameDialog.outputDirectory = outputBuffer;
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(90.0f, 0.0f)))
    {
        FileFilter filter{"Dot Project File", "*.dotproject"};
        const std::string selected = FileDialogs::SaveFile(filter, "Game.dotproject");
        if (!selected.empty())
            m_ExportGameDialog.outputDirectory = std::filesystem::path(selected).parent_path().string();
    }

    char gameNameBuffer[256] = {};
    std::strncpy(gameNameBuffer, m_ExportGameDialog.projectAsset.gameName.c_str(), sizeof(gameNameBuffer) - 1);
    if (ImGui::InputText("Game Name", gameNameBuffer, sizeof(gameNameBuffer)))
        m_ExportGameDialog.projectAsset.gameName = gameNameBuffer;

    char startupSceneBuffer[256] = {};
    std::strncpy(startupSceneBuffer, m_ExportGameDialog.projectAsset.startupScene.c_str(), sizeof(startupSceneBuffer) - 1);
    ImGui::BeginDisabled();
    ImGui::InputText("Startup Scene", startupSceneBuffer, sizeof(startupSceneBuffer));
    ImGui::EndDisabled();

    ImGui::InputScalar("Window Width", ImGuiDataType_U32, &m_ExportGameDialog.projectAsset.windowWidth);
    ImGui::InputScalar("Window Height", ImGuiDataType_U32, &m_ExportGameDialog.projectAsset.windowHeight);
    ImGui::Checkbox("Start Fullscreen", &m_ExportGameDialog.projectAsset.startFullscreen);
    ImGui::Checkbox("Capture Mouse On Start", &m_ExportGameDialog.projectAsset.captureMouseOnStart);
    ImGui::Checkbox("Cook Dependencies Only", &m_ExportGameDialog.cookDependenciesOnly);

    if (!m_ExportGameDialog.statusMessage.empty())
    {
        const ImVec4 color =
            m_ExportGameDialog.statusIsError ? ImVec4(0.92f, 0.35f, 0.35f, 1.0f) : ImVec4(0.40f, 0.88f, 0.52f, 1.0f);
        ImGui::Spacing();
        ImGui::TextColored(color, "%s", m_ExportGameDialog.statusMessage.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
    {
        const bool exported = ExportGamePackage();
        if (exported)
            ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool Application::ExportGamePackage()
{
    if (!m_SceneContext)
        return false;

    const std::filesystem::path relativeScene = ResolveRelativeScenePathInAssets();
    if (relativeScene.empty())
    {
        m_ExportGameDialog.statusMessage = "Save the startup scene inside the project Assets folder before exporting.";
        m_ExportGameDialog.statusIsError = true;
        return false;
    }

    if (m_SceneContext->GetScenePath().empty() || HasUnsavedSceneChanges())
    {
        if (!SaveScene())
        {
            m_ExportGameDialog.statusMessage = "Export failed because the current scene could not be saved.";
            m_ExportGameDialog.statusIsError = true;
            return false;
        }
    }

    GameExportOptions options;
    options.outputDirectory = m_ExportGameDialog.outputDirectory;
    options.gameExecutablePath = GetEditorBinaryDirectory() / "DotGame.exe";
    options.crashReporterExecutablePath = GetEditorBinaryDirectory() / "DotCrashReporter.exe";
    options.assetsRoot = AssetManager::Get().GetRootPath();
    options.shadersRoot = FindSourceShadersDirectory();
    options.projectAsset = m_ExportGameDialog.projectAsset;
    options.projectAsset.startupScene = relativeScene.generic_string();
    options.cookDependenciesOnly = m_ExportGameDialog.cookDependenciesOnly;

    GameExporter exporter;
    if (!exporter.Export(options))
    {
        m_ExportGameDialog.statusMessage = exporter.GetLastError();
        m_ExportGameDialog.statusIsError = true;
        return false;
    }

    m_ExportGameDialog.statusMessage = "Exported game to " + options.outputDirectory.string();
    m_ExportGameDialog.statusIsError = false;
    return true;
}

std::filesystem::path Application::GetEditorBinaryDirectory() const
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path();
}

std::filesystem::path Application::ResolveRelativeScenePathInAssets() const
{
    if (!m_SceneContext || m_SceneContext->GetScenePath().empty())
        return {};

    const std::filesystem::path scenePath = m_SceneContext->GetScenePath();
    const std::filesystem::path assetsRoot = AssetManager::Get().GetRootPath();
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(scenePath, assetsRoot, ec);
    if (ec || relative.empty())
        return {};

    const std::string relativeString = relative.generic_string();
    if (relativeString.starts_with(".."))
        return {};
    return relative;
}

std::filesystem::path Application::FindSourceShadersDirectory() const
{
    std::filesystem::path search = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth)
    {
        const std::filesystem::path candidate = search / "Editor" / "Shaders";
        if (std::filesystem::exists(candidate))
            return candidate;
        if (!search.has_parent_path())
            break;
        search = search.parent_path();
    }
    return {};
}

void Application::RebuildRuntimeFromToolbox()
{
    if (!m_SceneRuntime || !m_ViewportPanel)
        return;

    if (IsInPlayMode())
        Stop();

    if (m_ScriptingWorkspace)
        m_ScriptingWorkspace->SetScriptSystem(nullptr);

    m_SceneRuntime->Shutdown();
    m_SceneRuntime->SetModuleConfig(ToolboxManager::Get().BuildSceneRuntimeConfig());
    if (!m_SceneRuntime->Initialize(&m_ViewportPanel->GetWorld()))
    {
        DOT_LOG_ERROR("Failed to rebuild runtime from Toolbox state");
        return;
    }

    if (m_MapDocument)
        m_SceneRuntime->SetStaticWorldGeometry(m_MapDocument->GetStaticWorldGeometry());

    SyncToolboxBindings();
    EnsureWorkspaceSupported();
}

void Application::SyncToolboxBindings()
{
    if (m_ViewportPanel)
    {
        NavigationSystem* navigationSystem = nullptr;
        if (m_SceneRuntime && ToolboxManager::Get().IsNavigationEnabled() && ToolboxManager::Get().IsNavMeshGizmoEnabled())
            navigationSystem = m_SceneRuntime->GetNavigationSystem();
        m_ViewportPanel->SetNavigationSystem(navigationSystem);

        PhysicsSystem* physicsSystem = nullptr;
        if (m_SceneRuntime && ToolboxManager::Get().IsPhysicsEnabled())
            physicsSystem = m_SceneRuntime->GetPhysicsSystem();
        m_ViewportPanel->SetPhysicsSystem(physicsSystem);

        if (!ToolboxManager::Get().IsMapEditorEnabled())
            m_ViewportPanel->SetMapEditingEnabled(false);
    }

    if (m_ScriptingWorkspace)
    {
        ScriptSystem* scriptSystem = nullptr;
        if (m_SceneRuntime && ToolboxManager::Get().IsScriptingEnabled())
            scriptSystem = m_SceneRuntime->GetScriptSystem();
        m_ScriptingWorkspace->SetScriptSystem(scriptSystem);
    }
}

void Application::EnsureWorkspaceSupported()
{
    if (!ToolboxManager::Get().IsWorkspaceEnabled(m_CurrentWorkspace))
        SwitchWorkspace(WorkspaceType::Layout);
}

void Application::ProcessDeferredAssetActions()
{
    if (m_DeferredAssetActionType == DeferredAssetActionType::None || !m_DeferredAssetActionPath.has_value())
        return;

    const DeferredAssetActionType actionType = m_DeferredAssetActionType;
    const std::filesystem::path actionPath = *m_DeferredAssetActionPath;
    m_DeferredAssetActionType = DeferredAssetActionType::None;
    m_DeferredAssetActionPath.reset();

    switch (actionType)
    {
        case DeferredAssetActionType::OpenScene:
            OpenScene(actionPath);
            break;
        case DeferredAssetActionType::OpenMap:
            OpenMap(actionPath);
            break;
        case DeferredAssetActionType::OpenUI:
            OpenUI(actionPath);
            break;
        case DeferredAssetActionType::None:
        default:
            break;
    }
}

void Application::ApplyToolboxChanges(bool notifyMcpTools)
{
    ToolboxManager& toolbox = ToolboxManager::Get();
    toolbox.ApplyDraft();
    ProjectSettingsStorage::Save();
    RebuildRuntimeFromToolbox();
    RefreshMcpBridgeState();

    if (notifyMcpTools && m_McpBridgeService && toolbox.IsMcpBridgeEnabled())
        m_McpBridgeService->NotifyToolsChanged();
}

void Application::RefreshMcpBridgeState()
{
    if (!m_McpBridgeService)
        return;

    m_McpBridgeService->Configure(ToolboxManager::Get().IsMcpBridgeEnabled(), GetProjectRootPath(), MCP_EDITOR_VERSION);
}

std::filesystem::path Application::GetProjectRootPath() const
{
    return std::filesystem::current_path();
}

void Application::HandleMcpBridgeRequest(const McpBridgeCommandRequest& request, McpBridgeService::Completion completion)
{
    if (!completion)
        return;

    DOT_LOG_INFO("MCP tool call: %s", request.method.c_str());

    const auto completeToolSuccess =
        [&completion, &request](const McpJson::Value& structuredContent, const std::string& text)
        {
            completion(MakeBridgeSuccess(request.id, MakeToolResult(structuredContent, text, false)));
        };

    const auto completeBridgeError =
        [&completion, &request](const std::string& code, const std::string& message)
        {
            completion(MakeBridgeError(request.id, code, message, MakeToolResult(McpJson::Value::MakeObject(), message, true)));
        };

    const auto makeNoParamsSchema = []()
    {
        McpJson::Value schema = McpJson::Value::MakeObject();
        schema.objectValue["type"] = "object";
        schema.objectValue["additionalProperties"] = false;
        return schema;
    };

    const auto makeWorkspaceArray = [](bool includeOnlyEnabled)
    {
        McpJson::Value values = McpJson::Value::MakeArray();
        values.arrayValue.emplace_back("layout");

        for (WorkspaceType type : {WorkspaceType::Map, WorkspaceType::UI, WorkspaceType::Material, WorkspaceType::Scripting})
        {
            if (includeOnlyEnabled && !ToolboxManager::Get().IsWorkspaceEnabled(type))
                continue;
            values.arrayValue.emplace_back(WorkspaceTypeToMcpName(type));
        }

        return values;
    };

    const auto addToolDescriptor =
        [](McpJson::Value& tools, const char* name, const char* title, const std::string& description, McpJson::Value inputSchema)
        {
            McpJson::Value tool = McpJson::Value::MakeObject();
            tool.objectValue["name"] = name;
            tool.objectValue["title"] = title;
            tool.objectValue["description"] = description;
            tool.objectValue["inputSchema"] = std::move(inputSchema);
            tools.arrayValue.push_back(std::move(tool));
        };

    const auto makeVec3Schema = []()
    {
        McpJson::Value schema = McpJson::Value::MakeObject();
        schema.objectValue["type"] = "object";
        schema.objectValue["additionalProperties"] = false;

        McpJson::Value properties = McpJson::Value::MakeObject();
        McpJson::Value x = McpJson::Value::MakeObject();
        x.objectValue["type"] = "number";
        properties.objectValue["x"] = std::move(x);
        McpJson::Value y = McpJson::Value::MakeObject();
        y.objectValue["type"] = "number";
        properties.objectValue["y"] = std::move(y);
        McpJson::Value z = McpJson::Value::MakeObject();
        z.objectValue["type"] = "number";
        properties.objectValue["z"] = std::move(z);
        schema.objectValue["properties"] = std::move(properties);

        McpJson::Value required = McpJson::Value::MakeArray();
        required.arrayValue.emplace_back("x");
        required.arrayValue.emplace_back("y");
        required.arrayValue.emplace_back("z");
        schema.objectValue["required"] = std::move(required);
        return schema;
    };

    if (request.method == "bridge.list_tools")
    {
        McpJson::Value result = McpJson::Value::MakeObject();
        McpJson::Value tools = McpJson::Value::MakeArray();

        addToolDescriptor(
            tools,
            "project.get_state",
            "Project State",
            "Inspect the current project, active workspace, open documents, dirty flags, toolbox state, and MCP bridge status.",
            makeNoParamsSchema());

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value requestedWorkspace = McpJson::Value::MakeObject();
            requestedWorkspace.objectValue["type"] = "string";
            requestedWorkspace.objectValue["enum"] = makeWorkspaceArray(false);
            properties.objectValue["requestedWorkspace"] = std::move(requestedWorkspace);
            schema.objectValue["properties"] = std::move(properties);
            addToolDescriptor(tools, "workspace.get_state", "Workspace State",
                              "Inspect the current workspace and which workspaces are enabled by Toolbox gating.", std::move(schema));
        }

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value workspace = McpJson::Value::MakeObject();
            workspace.objectValue["type"] = "string";
            workspace.objectValue["enum"] = makeWorkspaceArray(true);
            properties.objectValue["workspace"] = std::move(workspace);
            schema.objectValue["properties"] = std::move(properties);
            McpJson::Value required = McpJson::Value::MakeArray();
            required.arrayValue.emplace_back("workspace");
            schema.objectValue["required"] = std::move(required);
            addToolDescriptor(tools, "workspace.switch", "Switch Workspace",
                              "Switch the editor to another enabled workspace.", std::move(schema));
        }

        addToolDescriptor(tools, "toolbox.list_modules", "List Toolbox Modules",
                          "Inspect Toolbox module descriptors, categories, dependency summaries, and active versus draft state.",
                          makeNoParamsSchema());

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value states = McpJson::Value::MakeObject();
            states.objectValue["type"] = "object";
            McpJson::Value boolSchema = McpJson::Value::MakeObject();
            boolSchema.objectValue["type"] = "boolean";
            states.objectValue["additionalProperties"] = std::move(boolSchema);
            properties.objectValue["states"] = std::move(states);
            McpJson::Value showAdvanced = McpJson::Value::MakeObject();
            showAdvanced.objectValue["type"] = "boolean";
            properties.objectValue["showAdvanced"] = std::move(showAdvanced);
            schema.objectValue["properties"] = std::move(properties);
            addToolDescriptor(tools, "toolbox.set_modules", "Set Toolbox Modules",
                              "Apply Toolbox module state changes, persist project settings, and rebuild editor/runtime state.",
                              std::move(schema));
        }

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value relativePath = McpJson::Value::MakeObject();
            relativePath.objectValue["type"] = "string";
            properties.objectValue["relativePath"] = std::move(relativePath);
            schema.objectValue["properties"] = std::move(properties);
            addToolDescriptor(tools, "assets.list", "List Assets",
                              "List files and folders under the project Assets root.", std::move(schema));
        }

        {
            std::string supportedKinds = "Open supported assets relative to the Assets root. Supported file kinds right now: scenes, maps, UI assets, text files, textures";
            if (ToolboxManager::Get().IsMaterialEditorEnabled())
                supportedKinds += ", materials";
            supportedKinds += ".";

            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value relativePath = McpJson::Value::MakeObject();
            relativePath.objectValue["type"] = "string";
            properties.objectValue["relativePath"] = std::move(relativePath);
            schema.objectValue["properties"] = std::move(properties);
            McpJson::Value required = McpJson::Value::MakeArray();
            required.arrayValue.emplace_back("relativePath");
            schema.objectValue["required"] = std::move(required);
            addToolDescriptor(tools, "assets.open", "Open Asset", supportedKinds, std::move(schema));
        }

        addToolDescriptor(tools, "assets.refresh", "Refresh Assets",
                          "Refresh the Asset Manager panel and rescan the current assets directory.", makeNoParamsSchema());
        addToolDescriptor(tools, "session.save_active", "Save Active Session",
                          "Save the active workspace document when the current workspace supports saving without a Save As dialog.",
                          makeNoParamsSchema());

        addToolDescriptor(tools, "entities.list", "List Entities",
                          "List entities in the active scene with names, hierarchy, and component summaries.", makeNoParamsSchema());

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value entityId = McpJson::Value::MakeObject();
            entityId.objectValue["type"] = "number";
            properties.objectValue["entityId"] = std::move(entityId);
            schema.objectValue["properties"] = std::move(properties);
            McpJson::Value required = McpJson::Value::MakeArray();
            required.arrayValue.emplace_back("entityId");
            schema.objectValue["required"] = std::move(required);
            addToolDescriptor(tools, "entities.get", "Get Entity",
                              "Inspect one entity and all reflected component properties that MCP can currently see.",
                              std::move(schema));
        }

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();

            McpJson::Value kind = McpJson::Value::MakeObject();
            kind.objectValue["type"] = "string";
            McpJson::Value kinds = McpJson::Value::MakeArray();
            for (const char* value : {"empty", "folder", "primitive", "mesh", "camera", "directional_light", "point_light",
                                      "spot_light", "skybox"})
            {
                kinds.arrayValue.emplace_back(value);
            }
            kind.objectValue["enum"] = std::move(kinds);
            properties.objectValue["kind"] = std::move(kind);

            McpJson::Value name = McpJson::Value::MakeObject();
            name.objectValue["type"] = "string";
            properties.objectValue["name"] = std::move(name);
            properties.objectValue["position"] = makeVec3Schema();
            properties.objectValue["rotation"] = makeVec3Schema();
            properties.objectValue["scale"] = makeVec3Schema();

            McpJson::Value select = McpJson::Value::MakeObject();
            select.objectValue["type"] = "boolean";
            properties.objectValue["select"] = std::move(select);

            schema.objectValue["properties"] = std::move(properties);
            McpJson::Value required = McpJson::Value::MakeArray();
            required.arrayValue.emplace_back("kind");
            schema.objectValue["required"] = std::move(required);
            addToolDescriptor(tools, "entities.create", "Create Entity",
                              "Create a new scene entity using the editor command system, with optional name and transform overrides.",
                              std::move(schema));
        }

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value entityId = McpJson::Value::MakeObject();
            entityId.objectValue["type"] = "number";
            properties.objectValue["entityId"] = std::move(entityId);
            properties.objectValue["position"] = makeVec3Schema();
            properties.objectValue["rotation"] = makeVec3Schema();
            properties.objectValue["scale"] = makeVec3Schema();
            McpJson::Value select = McpJson::Value::MakeObject();
            select.objectValue["type"] = "boolean";
            properties.objectValue["select"] = std::move(select);
            schema.objectValue["properties"] = std::move(properties);
            McpJson::Value required = McpJson::Value::MakeArray();
            required.arrayValue.emplace_back("entityId");
            schema.objectValue["required"] = std::move(required);
            addToolDescriptor(tools, "entities.set_transform", "Set Entity Transform",
                              "Change an entity's position, rotation, and scale with undo support.", std::move(schema));
        }

        {
            McpJson::Value schema = McpJson::Value::MakeObject();
            schema.objectValue["type"] = "object";
            schema.objectValue["additionalProperties"] = false;
            McpJson::Value properties = McpJson::Value::MakeObject();
            McpJson::Value entityId = McpJson::Value::MakeObject();
            entityId.objectValue["type"] = "number";
            properties.objectValue["entityId"] = std::move(entityId);
            McpJson::Value componentName = McpJson::Value::MakeObject();
            componentName.objectValue["type"] = "string";
            properties.objectValue["componentName"] = std::move(componentName);
            McpJson::Value propertyName = McpJson::Value::MakeObject();
            propertyName.objectValue["type"] = "string";
            properties.objectValue["propertyName"] = std::move(propertyName);
            properties.objectValue["value"] = McpJson::Value::MakeObject();
            schema.objectValue["properties"] = std::move(properties);
            McpJson::Value required = McpJson::Value::MakeArray();
            required.arrayValue.emplace_back("entityId");
            required.arrayValue.emplace_back("componentName");
            required.arrayValue.emplace_back("propertyName");
            required.arrayValue.emplace_back("value");
            schema.objectValue["required"] = std::move(required);
            addToolDescriptor(tools, "entities.set_component_property", "Set Component Property",
                              "Set one reflected component property on an entity with undo support.", std::move(schema));
        }

        result.objectValue["tools"] = std::move(tools);
        completion(MakeBridgeSuccess(request.id, std::move(result)));
        return;
    }

    if (request.method == "project.get_state")
    {
        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["projectRoot"] = GetProjectRootPath().generic_string();
        result.objectValue["assetsRoot"] = AssetManager::Get().GetRootPath();
        result.objectValue["activeWorkspace"] = WorkspaceTypeToMcpName(m_CurrentWorkspace);

        McpJson::Value documents = McpJson::Value::MakeObject();
        if (m_SceneContext)
        {
            McpJson::Value scene = McpJson::Value::MakeObject();
            scene.objectValue["path"] = m_SceneContext->GetScenePath();
            scene.objectValue["dirty"] = m_SceneContext->IsSceneDirty();
            documents.objectValue["scene"] = std::move(scene);
        }
        if (m_MapDocument)
        {
            McpJson::Value map = McpJson::Value::MakeObject();
            map.objectValue["path"] = m_MapDocument->GetPath().generic_string();
            map.objectValue["dirty"] = m_MapDocument->IsDirty();
            documents.objectValue["map"] = std::move(map);
        }
        if (m_UIWorkspace)
        {
            McpJson::Value ui = McpJson::Value::MakeObject();
            ui.objectValue["path"] = m_UIWorkspace->GetAssetPath().generic_string();
            ui.objectValue["dirty"] = m_UIWorkspace->IsAssetDirty();
            documents.objectValue["ui"] = std::move(ui);
        }
        if (m_ScriptingWorkspace)
        {
            McpJson::Value script = McpJson::Value::MakeObject();
            script.objectValue["path"] = m_ScriptingWorkspace->GetActiveFilePath().generic_string();
            script.objectValue["dirty"] = m_ScriptingWorkspace->IsActiveFileDirty();
            documents.objectValue["script"] = std::move(script);
        }
        result.objectValue["documents"] = std::move(documents);

        McpJson::Value toolboxSummary = McpJson::Value::MakeObject();
        McpJson::Value enabledModules = McpJson::Value::MakeArray();
        for (const ToolboxModuleDescriptor& descriptor : ToolboxManager::Get().GetModules())
        {
            if (ToolboxManager::Get().IsModuleEnabled(descriptor.id))
                enabledModules.arrayValue.emplace_back(descriptor.id);
        }
        toolboxSummary.objectValue["enabledModuleIds"] = std::move(enabledModules);
        result.objectValue["toolbox"] = std::move(toolboxSummary);

        if (m_McpBridgeService)
        {
            const McpBridgeService::Status status = m_McpBridgeService->GetStatus();
            McpJson::Value bridge = McpJson::Value::MakeObject();
            bridge.objectValue["enabled"] = status.enabled;
            bridge.objectValue["running"] = status.running;
            bridge.objectValue["connectedClients"] = static_cast<uint64_t>(status.connectedClients);
            bridge.objectValue["pipeName"] = status.pipeName;
            bridge.objectValue["manifestPath"] = status.manifestPath.generic_string();
            bridge.objectValue["lastError"] = status.lastError;
            result.objectValue["bridgeStatus"] = std::move(bridge);
        }

        completeToolSuccess(result, "Fetched project state.");
        return;
    }

    if (request.method == "workspace.get_state")
    {
        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["currentWorkspace"] = WorkspaceTypeToMcpName(m_CurrentWorkspace);

        McpJson::Value workspaces = McpJson::Value::MakeArray();
        for (WorkspaceType type : {WorkspaceType::Layout, WorkspaceType::Map, WorkspaceType::UI, WorkspaceType::Material, WorkspaceType::Scripting})
        {
            McpJson::Value workspace = McpJson::Value::MakeObject();
            workspace.objectValue["name"] = WorkspaceTypeToMcpName(type);
            workspace.objectValue["enabled"] = ToolboxManager::Get().IsWorkspaceEnabled(type);
            workspaces.arrayValue.push_back(std::move(workspace));
        }
        result.objectValue["workspaces"] = std::move(workspaces);

        if (const std::optional<std::string> requested = McpJson::GetString(request.params, "requestedWorkspace"))
        {
            if (const std::optional<WorkspaceType> workspaceType = ParseWorkspaceTypeName(*requested))
                result.objectValue["requestedWorkspaceAllowed"] = ToolboxManager::Get().IsWorkspaceEnabled(*workspaceType);
            else
                result.objectValue["requestedWorkspaceAllowed"] = false;
        }

        completeToolSuccess(result, "Fetched workspace state.");
        return;
    }

    if (request.method == "workspace.switch")
    {
        const std::optional<std::string> workspaceName = McpJson::GetString(request.params, "workspace");
        if (!workspaceName.has_value())
        {
            completeBridgeError("invalid_params", "workspace.switch requires a workspace name.");
            return;
        }

        const std::optional<WorkspaceType> workspaceType = ParseWorkspaceTypeName(*workspaceName);
        if (!workspaceType.has_value())
        {
            completeBridgeError("invalid_params", "Unknown workspace target.");
            return;
        }

        if (!ToolboxManager::Get().IsWorkspaceEnabled(*workspaceType))
        {
            completeBridgeError("tool_disabled", "That workspace is currently disabled in Toolbox.");
            return;
        }

        SwitchWorkspace(*workspaceType);

        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["currentWorkspace"] = WorkspaceTypeToMcpName(m_CurrentWorkspace);
        completeToolSuccess(result, "Switched workspace.");
        return;
    }

    if (request.method == "toolbox.list_modules")
    {
        McpJson::Value result = McpJson::Value::MakeObject();
        McpJson::Value modules = McpJson::Value::MakeArray();
        McpJson::Value categories = McpJson::Value::MakeArray();

        for (const std::string& category : ToolboxManager::Get().GetVisibleCategories(true))
            categories.arrayValue.emplace_back(category);

        for (const ToolboxModuleDescriptor& descriptor : ToolboxManager::Get().GetModules())
        {
            McpJson::Value module = McpJson::Value::MakeObject();
            module.objectValue["id"] = descriptor.id;
            module.objectValue["displayName"] = descriptor.displayName;
            module.objectValue["description"] = descriptor.description;
            module.objectValue["category"] = descriptor.category;
            module.objectValue["bundle"] = descriptor.bundle;
            module.objectValue["defaultEnabled"] = descriptor.defaultEnabled;
            module.objectValue["alwaysEnabled"] = descriptor.alwaysEnabled;
            module.objectValue["advancedOnly"] = descriptor.advancedOnly;
            module.objectValue["activeEnabled"] = ToolboxManager::Get().IsModuleEnabled(descriptor.id);
            module.objectValue["draftEnabled"] = ToolboxManager::Get().IsDraftModuleEnabled(descriptor.id);
            module.objectValue["dependencySummary"] = ToolboxManager::Get().GetDependencySummary(descriptor);

            McpJson::Value dependencies = McpJson::Value::MakeArray();
            for (const std::string& dependency : descriptor.dependencies)
                dependencies.arrayValue.emplace_back(dependency);
            module.objectValue["dependencies"] = std::move(dependencies);
            modules.arrayValue.push_back(std::move(module));
        }

        result.objectValue["categories"] = std::move(categories);
        result.objectValue["modules"] = std::move(modules);
        completeToolSuccess(result, "Fetched Toolbox module list.");
        return;
    }

    if (request.method == "toolbox.set_modules")
    {
        ToolboxManager& toolbox = ToolboxManager::Get();
        toolbox.ResetDraft();

        if (const McpJson::Value* states = McpJson::FindObjectMember(request.params, "states"))
        {
            if (!states->IsObject())
            {
                completeBridgeError("invalid_params", "toolbox.set_modules states must be an object of boolean values.");
                return;
            }

            for (const auto& [moduleId, value] : states->objectValue)
            {
                const std::optional<bool> enabled = McpJson::GetBool(value);
                if (!enabled.has_value())
                {
                    completeBridgeError("invalid_params", "toolbox.set_modules states entries must be booleans.");
                    return;
                }
                toolbox.SetDraftModuleEnabled(moduleId, *enabled);
            }
        }

        if (const std::optional<bool> showAdvanced = McpJson::GetBool(request.params, "showAdvanced"))
            toolbox.SetDraftAdvancedView(*showAdvanced);

        if (!toolbox.HasDraftChanges())
        {
            McpJson::Value result = McpJson::Value::MakeObject();
            result.objectValue["changed"] = false;
            completeToolSuccess(result, "No Toolbox changes were required.");
            return;
        }

        std::vector<std::string> disabledModules;
        for (const ToolboxModuleDescriptor& descriptor : toolbox.GetModules())
        {
            if (toolbox.IsModuleEnabled(descriptor.id) && !toolbox.IsDraftModuleEnabled(descriptor.id))
                disabledModules.emplace_back(descriptor.displayName);
        }

        const auto applyChanges =
            [this, completion, request]()
            {
                ApplyToolboxChanges(true);
                McpJson::Value result = McpJson::Value::MakeObject();
                result.objectValue["changed"] = true;
                completion(MakeBridgeSuccess(request.id, MakeToolResult(result, "Applied Toolbox module changes.", false)));
            };

        if (disabledModules.empty())
        {
            applyChanges();
            return;
        }

        if (m_ConfirmationDialog.IsOpen())
        {
            completeBridgeError("confirmation_busy", "Another editor confirmation dialog is already open.");
            return;
        }

        std::string message = "Apply Toolbox changes that disable: ";
        for (size_t index = 0; index < disabledModules.size(); ++index)
        {
            message += disabledModules[index];
            if (index + 1 < disabledModules.size())
                message += ", ";
        }
        message += "?";

        m_ConfirmationDialog.Open(
            "Confirm Toolbox Changes",
            std::move(message),
            ConfirmationDialog::Button{
                "Apply",
                [applyChanges]() mutable
                {
                    applyChanges();
                    return true;
                },
                true,
                false,
            },
            ConfirmationDialog::Button{
                "Cancel",
                [completion, request]()
                {
                    completion(MakeBridgeError(request.id, "user_cancelled", "Toolbox changes were cancelled by the user.",
                                               MakeToolResult(McpJson::Value::MakeObject(),
                                                              "Toolbox changes were cancelled by the user.", true)));
                    return true;
                },
                false,
                false,
            },
            ConfirmationDialog::Button{
                "Reset Draft",
                [&toolbox, completion, request]()
                {
                    toolbox.ResetDraft();
                    completion(MakeBridgeError(request.id, "user_cancelled", "Toolbox changes were cancelled and the draft was reset.",
                                               MakeToolResult(McpJson::Value::MakeObject(),
                                                              "Toolbox changes were cancelled and the draft was reset.", true)));
                    return true;
                },
                false,
                true,
            });
        return;
    }

    if (request.method == "entities.list")
    {
        if (!m_ViewportPanel)
        {
            completeBridgeError("not_available", "The scene viewport is unavailable.");
            return;
        }

        World& world = m_ViewportPanel->GetWorld();
        McpJson::Value result = McpJson::Value::MakeObject();
        McpJson::Value entities = McpJson::Value::MakeArray();
        world.EachEntity(
            [&entities, &world](Entity entity)
            {
                entities.arrayValue.push_back(BuildEntitySummary(world, entity, false));
            });

        result.objectValue["entityCount"] = static_cast<uint64_t>(entities.arrayValue.size());
        result.objectValue["entities"] = std::move(entities);
        completeToolSuccess(result, "Listed scene entities.");
        return;
    }

    if (request.method == "entities.get")
    {
        if (!m_ViewportPanel)
        {
            completeBridgeError("not_available", "The scene viewport is unavailable.");
            return;
        }

        const std::optional<double> entityId = McpJson::GetNumber(request.params, "entityId");
        if (!entityId.has_value())
        {
            completeBridgeError("invalid_params", "entities.get requires an entityId.");
            return;
        }

        World& world = m_ViewportPanel->GetWorld();
        const Entity entity(static_cast<uint32_t>(*entityId));
        if (!world.IsAlive(entity))
        {
            completeBridgeError("not_found", "That entity does not exist.");
            return;
        }

        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["entity"] = BuildEntitySummary(world, entity, true);
        completeToolSuccess(result, "Fetched entity details.");
        return;
    }

    if (request.method == "entities.create")
    {
        if (!m_ViewportPanel)
        {
            completeBridgeError("not_available", "The scene viewport is unavailable.");
            return;
        }

        const std::optional<std::string> kind = McpJson::GetString(request.params, "kind");
        if (!kind.has_value())
        {
            completeBridgeError("invalid_params", "entities.create requires a kind.");
            return;
        }

        std::string commandPath;
        if (*kind == "empty")
            commandPath = "Create/Empty Entity";
        else if (*kind == "folder")
            commandPath = "Create/Folder";
        else if (*kind == "primitive")
            commandPath = "Create/Primitive";
        else if (*kind == "mesh")
            commandPath = "Create/Mesh";
        else if (*kind == "camera")
            commandPath = "Create/Camera";
        else if (*kind == "directional_light")
            commandPath = "Create/Light/Directional Light";
        else if (*kind == "point_light")
            commandPath = "Create/Light/Point Light";
        else if (*kind == "spot_light")
            commandPath = "Create/Light/Spot Light";
        else if (*kind == "skybox")
            commandPath = "Create/Skybox";
        else
        {
            completeBridgeError("invalid_params", "Unsupported entity kind.");
            return;
        }

        std::optional<Vec3> requestedPosition;
        std::optional<Vec3> requestedRotation;
        std::optional<Vec3> requestedScale;
        if (const McpJson::Value* position = McpJson::FindObjectMember(request.params, "position"))
        {
            Vec3 parsed;
            if (!ParseVec3Value(*position, parsed))
            {
                completeBridgeError("invalid_params", "entities.create position must be a Vec3 object or array.");
                return;
            }
            requestedPosition = parsed;
        }
        if (const McpJson::Value* rotation = McpJson::FindObjectMember(request.params, "rotation"))
        {
            Vec3 parsed;
            if (!ParseVec3Value(*rotation, parsed))
            {
                completeBridgeError("invalid_params", "entities.create rotation must be a Vec3 object or array.");
                return;
            }
            requestedRotation = parsed;
        }
        if (const McpJson::Value* scale = McpJson::FindObjectMember(request.params, "scale"))
        {
            Vec3 parsed;
            if (!ParseVec3Value(*scale, parsed))
            {
                completeBridgeError("invalid_params", "entities.create scale must be a Vec3 object or array.");
                return;
            }
            requestedScale = parsed;
        }

        World& world = m_ViewportPanel->GetWorld();
        Entity createdEntity = kNullEntity;
        CommandRegistry::Get().Execute(commandPath, &world, &createdEntity);
        if (!createdEntity.IsValid() || !world.IsAlive(createdEntity))
        {
            completeBridgeError("create_failed", "Failed to create the requested entity.");
            return;
        }

        if (const std::optional<std::string> requestedName = McpJson::GetString(request.params, "name"))
        {
            const McpSceneComponentBinding* nameBinding = FindMcpSceneComponentBinding("NameComponent");
            const Property* nameProperty = nameBinding != nullptr ? FindSceneComponentProperty(*nameBinding, "name") : nullptr;
            if (nameBinding != nullptr && nameProperty != nullptr)
            {
                const void* nameObject = nameBinding->getConst(world, createdEntity);
                const McpJson::Value beforeName = SerializeScenePropertyValue(*nameProperty, nameObject);
                CommandRegistry::Get().ExecuteCommand(std::make_unique<ReflectedScenePropertyCommand>(
                    &world, createdEntity, nameBinding->name, "name", beforeName, McpJson::Value(*requestedName)));
            }
        }

        if (auto* transform = world.GetComponent<TransformComponent>(createdEntity))
        {
            Vec3 newPosition = transform->position;
            Vec3 newRotation = transform->rotation;
            Vec3 newScale = transform->scale;
            bool hasTransformOverride = false;

            if (requestedPosition.has_value())
            {
                newPosition = *requestedPosition;
                hasTransformOverride = true;
            }
            if (requestedRotation.has_value())
            {
                newRotation = *requestedRotation;
                hasTransformOverride = true;
            }
            if (requestedScale.has_value())
            {
                newScale = *requestedScale;
                hasTransformOverride = true;
            }

            if (hasTransformOverride)
            {
                CommandRegistry::Get().ExecuteCommand(std::make_unique<TransformCommand>(
                    &world, createdEntity, transform->position, transform->rotation, transform->scale, newPosition, newRotation,
                    newScale));
            }
        }

        if (McpJson::GetBoolOr(request.params, "select", true) && m_SceneContext)
        {
            m_SceneContext->GetEntitySelection().SetPrimaryEntity(&world, createdEntity);
            m_ViewportPanel->SetSelectedEntity(createdEntity);
            if (m_HierarchyPanel)
                m_HierarchyPanel->SetSelectedEntity(createdEntity);
        }

        if (m_SceneContext)
        {
            m_SceneContext->SetSceneDirty(true);
            m_SceneContext->SetForceSceneDirtyCheck(true);
        }
        UpdateWindowTitle();

        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["entity"] = BuildEntitySummary(world, createdEntity, true);
        completeToolSuccess(result, "Created entity.");
        return;
    }

    if (request.method == "entities.set_transform")
    {
        if (!m_ViewportPanel)
        {
            completeBridgeError("not_available", "The scene viewport is unavailable.");
            return;
        }

        const std::optional<double> entityId = McpJson::GetNumber(request.params, "entityId");
        if (!entityId.has_value())
        {
            completeBridgeError("invalid_params", "entities.set_transform requires an entityId.");
            return;
        }

        World& world = m_ViewportPanel->GetWorld();
        const Entity entity(static_cast<uint32_t>(*entityId));
        if (!world.IsAlive(entity))
        {
            completeBridgeError("not_found", "That entity does not exist.");
            return;
        }

        auto* transform = world.GetComponent<TransformComponent>(entity);
        if (!transform)
        {
            completeBridgeError("not_available", "That entity does not have a TransformComponent.");
            return;
        }

        Vec3 newPosition = transform->position;
        Vec3 newRotation = transform->rotation;
        Vec3 newScale = transform->scale;
        bool changed = false;

        if (const McpJson::Value* position = McpJson::FindObjectMember(request.params, "position"))
        {
            if (!ParseVec3Value(*position, newPosition))
            {
                completeBridgeError("invalid_params", "position must be a Vec3 object or array.");
                return;
            }
            changed = true;
        }
        if (const McpJson::Value* rotation = McpJson::FindObjectMember(request.params, "rotation"))
        {
            if (!ParseVec3Value(*rotation, newRotation))
            {
                completeBridgeError("invalid_params", "rotation must be a Vec3 object or array.");
                return;
            }
            changed = true;
        }
        if (const McpJson::Value* scale = McpJson::FindObjectMember(request.params, "scale"))
        {
            if (!ParseVec3Value(*scale, newScale))
            {
                completeBridgeError("invalid_params", "scale must be a Vec3 object or array.");
                return;
            }
            changed = true;
        }

        if (!changed)
        {
            completeBridgeError("invalid_params", "entities.set_transform requires at least one of position, rotation, or scale.");
            return;
        }

        CommandRegistry::Get().ExecuteCommand(std::make_unique<TransformCommand>(
            &world, entity, transform->position, transform->rotation, transform->scale, newPosition, newRotation, newScale));

        if (McpJson::GetBoolOr(request.params, "select", false) && m_SceneContext)
        {
            m_SceneContext->GetEntitySelection().SetPrimaryEntity(&world, entity);
            m_ViewportPanel->SetSelectedEntity(entity);
            if (m_HierarchyPanel)
                m_HierarchyPanel->SetSelectedEntity(entity);
        }

        if (m_SceneContext)
        {
            m_SceneContext->SetSceneDirty(true);
            m_SceneContext->SetForceSceneDirtyCheck(true);
        }
        UpdateWindowTitle();

        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["entity"] = BuildEntitySummary(world, entity, true);
        completeToolSuccess(result, "Updated entity transform.");
        return;
    }

    if (request.method == "entities.set_component_property")
    {
        if (!m_ViewportPanel)
        {
            completeBridgeError("not_available", "The scene viewport is unavailable.");
            return;
        }

        const std::optional<double> entityId = McpJson::GetNumber(request.params, "entityId");
        const std::optional<std::string> componentName = McpJson::GetString(request.params, "componentName");
        const std::optional<std::string> propertyName = McpJson::GetString(request.params, "propertyName");
        const McpJson::Value* value = McpJson::FindObjectMember(request.params, "value");
        if (!entityId.has_value() || !componentName.has_value() || !propertyName.has_value() || value == nullptr)
        {
            completeBridgeError("invalid_params",
                                "entities.set_component_property requires entityId, componentName, propertyName, and value.");
            return;
        }

        World& world = m_ViewportPanel->GetWorld();
        const Entity entity(static_cast<uint32_t>(*entityId));
        if (!world.IsAlive(entity))
        {
            completeBridgeError("not_found", "That entity does not exist.");
            return;
        }

        const McpSceneComponentBinding* binding = FindMcpSceneComponentBinding(*componentName);
        if (binding == nullptr)
        {
            completeBridgeError("invalid_params", "Unknown component name.");
            return;
        }

        const Property* property = FindSceneComponentProperty(*binding, *propertyName);
        if (property == nullptr)
        {
            completeBridgeError("invalid_params", "Unknown component property.");
            return;
        }

        const void* object = binding->getConst != nullptr ? binding->getConst(world, entity) : nullptr;
        if (object == nullptr)
        {
            completeBridgeError("not_available", "That entity does not have the requested component.");
            return;
        }

        const McpJson::Value beforeValue = SerializeScenePropertyValue(*property, object);
        std::string validationError;
        if (!ApplySceneComponentPropertyValue(world, entity, *binding, *propertyName, *value, &validationError))
        {
            completeBridgeError("invalid_params", validationError.empty() ? "Failed to apply component property." : validationError);
            return;
        }

        const McpJson::Value afterValue = SerializeScenePropertyValue(*property, binding->getConst(world, entity));
        CommandRegistry::Get().PushCommand(std::make_unique<ReflectedScenePropertyCommand>(
            &world, entity, binding->name, *propertyName, beforeValue, afterValue));

        if (m_SceneContext)
        {
            m_SceneContext->SetSceneDirty(true);
            m_SceneContext->SetForceSceneDirtyCheck(true);
        }
        UpdateWindowTitle();

        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["entity"] = BuildEntitySummary(world, entity, true);
        result.objectValue["componentName"] = binding->name;
        result.objectValue["propertyName"] = *propertyName;
        result.objectValue["value"] = afterValue;
        completeToolSuccess(result, "Updated component property.");
        return;
    }

    if (request.method == "assets.list")
    {
        const std::string assetsRoot = AssetManager::Get().GetRootPath();
        const std::string relativePath = McpJson::GetStringOr(request.params, "relativePath", "");
        const std::optional<std::filesystem::path> resolvedPath =
            ResolveAssetPathWithinRoot(std::filesystem::path(assetsRoot), relativePath);
        if (!resolvedPath.has_value())
        {
            completeBridgeError("invalid_params", "The requested asset path escapes the Assets root.");
            return;
        }
        if (!std::filesystem::exists(*resolvedPath))
        {
            completeBridgeError("not_found", "That asset path does not exist.");
            return;
        }
        if (!std::filesystem::is_directory(*resolvedPath))
        {
            completeBridgeError("invalid_params", "assets.list requires a directory path relative to Assets.");
            return;
        }

        std::vector<std::filesystem::directory_entry> entries;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(*resolvedPath))
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
                  [](const std::filesystem::directory_entry& left, const std::filesystem::directory_entry& right)
                  {
                      if (left.is_directory() != right.is_directory())
                          return left.is_directory() && !right.is_directory();
                      return ToLowerCopy(left.path().filename().string()) < ToLowerCopy(right.path().filename().string());
                  });

        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["relativePath"] = relativePath;
        McpJson::Value items = McpJson::Value::MakeArray();
        const std::filesystem::path assetsRootPath = std::filesystem::weakly_canonical(std::filesystem::path(assetsRoot));
        for (const std::filesystem::directory_entry& entry : entries)
        {
            McpJson::Value item = McpJson::Value::MakeObject();
            item.objectValue["name"] = entry.path().filename().string();
            item.objectValue["relativePath"] = std::filesystem::relative(entry.path(), assetsRootPath).generic_string();
            item.objectValue["isDirectory"] = entry.is_directory();
            item.objectValue["extension"] = entry.path().extension().string();
            item.objectValue["type"] = entry.is_directory() ? "directory" : "file";
            item.objectValue["size"] = entry.is_directory() ? 0ULL : static_cast<uint64_t>(entry.file_size());
            item.objectValue["modifiedTime"] = FormatUtcTime(entry.last_write_time());

            const std::string extension = ToLowerCopy(entry.path().extension().string());
            item.objectValue["openable"] = entry.is_directory()
                                               ? false
                                               : extension == ".dotscene" || extension == ".dotmap" || extension == ".dotui" ||
                                                     extension == ".dotmat" || IsTextAssetExtension(extension) ||
                                                     IsTextureAssetExtension(extension);
            items.arrayValue.push_back(std::move(item));
        }

        result.objectValue["items"] = std::move(items);
        completeToolSuccess(result, "Listed assets.");
        return;
    }

    if (request.method == "assets.open")
    {
        const std::optional<std::string> relativePath = McpJson::GetString(request.params, "relativePath");
        if (!relativePath.has_value() || relativePath->empty())
        {
            completeBridgeError("invalid_params", "assets.open requires a relativePath.");
            return;
        }

        const std::optional<std::filesystem::path> resolvedPath =
            ResolveAssetPathWithinRoot(std::filesystem::path(AssetManager::Get().GetRootPath()), *relativePath);
        if (!resolvedPath.has_value())
        {
            completeBridgeError("invalid_params", "The requested asset path escapes the Assets root.");
            return;
        }
        if (!std::filesystem::exists(*resolvedPath) || std::filesystem::is_directory(*resolvedPath))
        {
            completeBridgeError("not_found", "That asset file does not exist.");
            return;
        }

        const std::string extension = ToLowerCopy(resolvedPath->extension().string());
        const auto completeOpened = [completeToolSuccess, relativePath]()
        {
            McpJson::Value result = McpJson::Value::MakeObject();
            result.objectValue["relativePath"] = *relativePath;
            completeToolSuccess(result, "Opened asset.");
        };

        if (extension == ".dotscene")
        {
            const auto loadScene =
                [this, path = *resolvedPath, completeOpened, completeBridgeError]()
                {
                    if (!LoadSceneFromPath(path))
                    {
                        completeBridgeError("open_failed", "Failed to open the requested scene.");
                        return;
                    }
                    completeOpened();
                };

            if (!HasUnsavedSceneChanges())
            {
                loadScene();
                return;
            }

            if (m_ConfirmationDialog.IsOpen())
            {
                completeBridgeError("confirmation_busy", "Another editor confirmation dialog is already open.");
                return;
            }

            m_ConfirmationDialog.Open(
                "Unsaved Changes",
                "Save the current scene before opening another scene?",
                ConfirmationDialog::Button{
                    "Save",
                    [this, loadScene]()
                    {
                        if (!SaveScene())
                            return false;
                        loadScene();
                        return true;
                    },
                    true,
                    false,
                },
                ConfirmationDialog::Button{
                    "Don't Save",
                    [loadScene]()
                    {
                        loadScene();
                        return true;
                    },
                    false,
                    true,
                },
                ConfirmationDialog::Button{
                    "Cancel",
                    [completion, request]()
                    {
                        completion(MakeBridgeError(request.id, "user_cancelled", "Scene open was cancelled by the user.",
                                                   MakeToolResult(McpJson::Value::MakeObject(),
                                                                  "Scene open was cancelled by the user.", true)));
                        return true;
                    },
                    false,
                    false,
                });
            return;
        }

        if (extension == ".dotmap")
        {
            if (!ToolboxManager::Get().IsMapEditorEnabled())
            {
                completeBridgeError("tool_disabled", "Map Editor is disabled in Toolbox.");
                return;
            }
            if (!m_MapDocument || !m_MapDocument->Load(*resolvedPath))
            {
                completeBridgeError("open_failed", m_MapDocument ? m_MapDocument->GetLastError() : "Map document is unavailable.");
                return;
            }
            EnsureSceneMapReference(*resolvedPath);
            SyncSceneMapDocumentFromScene();
            SwitchWorkspace(WorkspaceType::Map);
            completeOpened();
            return;
        }

        if (extension == ".dotui")
        {
            if (!ToolboxManager::Get().IsUIEditorEnabled())
            {
                completeBridgeError("tool_disabled", "UI Editor is disabled in Toolbox.");
                return;
            }
            if (!m_UIWorkspace || !m_UIWorkspace->OpenAsset(*resolvedPath))
            {
                completeBridgeError("open_failed", "Failed to open the requested UI asset.");
                return;
            }
            SwitchWorkspace(WorkspaceType::UI);
            completeOpened();
            return;
        }

        if (extension == ".dotmat")
        {
            if (!ToolboxManager::Get().IsMaterialEditorEnabled())
            {
                completeBridgeError("tool_disabled", "Material Editor is disabled in Toolbox.");
                return;
            }
            if (!m_MaterialGraphPanel)
            {
                completeBridgeError("open_failed", "Material editor panel is unavailable.");
                return;
            }
            m_MaterialGraphPanel->LoadMaterial(resolvedPath->string());
            SwitchWorkspace(WorkspaceType::Material);
            completeOpened();
            return;
        }

        if (IsTextureAssetExtension(extension))
        {
            if (!m_TextureViewerPanel)
            {
                completeBridgeError("open_failed", "Texture viewer is unavailable.");
                return;
            }
            m_TextureViewerPanel->OpenTexture(*resolvedPath);
            m_TextureViewerPanel->SetOpen(true);
            completeOpened();
            return;
        }

        if (IsTextAssetExtension(extension))
        {
            if (!m_TextEditorPanel)
            {
                completeBridgeError("open_failed", "Text editor panel is unavailable.");
                return;
            }
            m_TextEditorPanel->OpenFile(*resolvedPath);
            completeOpened();
            return;
        }

        completeBridgeError("unsupported_asset", "That asset type is not supported by assets.open yet.");
        return;
    }

    if (request.method == "assets.refresh")
    {
        if (!m_AssetManagerPanel)
        {
            completeBridgeError("not_available", "Asset Manager panel is unavailable.");
            return;
        }

        m_AssetManagerPanel->Refresh();
        McpJson::Value result = McpJson::Value::MakeObject();
        result.objectValue["assetsRoot"] = AssetManager::Get().GetRootPath();
        completeToolSuccess(result, "Refreshed Asset Manager.");
        return;
    }

    if (request.method == "session.save_active")
    {
        McpJson::Value result = McpJson::Value::MakeObject();
        switch (m_CurrentWorkspace)
        {
            case WorkspaceType::Layout:
                if (!m_SceneContext || m_SceneContext->GetScenePath().empty())
                {
                    completeBridgeError("save_as_required", "The current scene has no path yet, so MCP cannot save it without a Save As dialog.");
                    return;
                }
                if (!SaveScene())
                {
                    completeBridgeError("save_failed", "Failed to save the active scene.");
                    return;
                }
                result.objectValue["workspace"] = "layout";
                result.objectValue["path"] = m_SceneContext->GetScenePath();
                completeToolSuccess(result, "Saved active scene.");
                return;
            case WorkspaceType::Map:
                if (!SaveMapIfNeeded())
                {
                    completeBridgeError("save_failed", "Failed to save the active map.");
                    return;
                }
                result.objectValue["workspace"] = "map";
                result.objectValue["path"] = m_MapDocument ? m_MapDocument->GetPath().generic_string() : "";
                completeToolSuccess(result, "Saved active map.");
                return;
            case WorkspaceType::UI:
                if (!m_UIWorkspace || m_UIWorkspace->GetAssetPath().empty())
                {
                    completeBridgeError("save_as_required", "The active UI document has no path yet, so MCP cannot save it without a Save As dialog.");
                    return;
                }
                if (!m_UIWorkspace->SaveAsset())
                {
                    completeBridgeError("save_failed", "Failed to save the active UI document.");
                    return;
                }
                result.objectValue["workspace"] = "ui";
                result.objectValue["path"] = m_UIWorkspace->GetAssetPath().generic_string();
                completeToolSuccess(result, "Saved active UI document.");
                return;
            case WorkspaceType::Scripting:
                if (!m_ScriptingWorkspace || !m_ScriptingWorkspace->HasActiveFile())
                {
                    completeBridgeError("not_available", "There is no active script file to save.");
                    return;
                }
                if (m_ScriptingWorkspace->GetActiveFilePath().empty())
                {
                    completeBridgeError("save_as_required", "The active script has no path yet, so MCP cannot save it without a Save As dialog.");
                    return;
                }
                if (!m_ScriptingWorkspace->SaveActiveFile())
                {
                    completeBridgeError("save_failed", "Failed to save the active script file.");
                    return;
                }
                result.objectValue["workspace"] = "scripting";
                result.objectValue["path"] = m_ScriptingWorkspace->GetActiveFilePath().generic_string();
                completeToolSuccess(result, "Saved active script file.");
                return;
            case WorkspaceType::Material:
                completeBridgeError("unsupported_workspace", "The material workspace does not expose a save operation through MCP v1.");
                return;
        }
    }

    completeBridgeError("unknown_method", "The requested MCP bridge method is not implemented.");
}

void Application::DrawToolboxModal()
{
    if (m_RequestOpenToolboxModal)
    {
        ImGui::OpenPopup("Toolbox");
        m_RequestOpenToolboxModal = false;
    }

    ToolboxManager& toolbox = ToolboxManager::Get();
    const auto closeToolboxWithoutApplying = [&toolbox]()
    {
        toolbox.ResetDraft();
        ImGui::CloseCurrentPopup();
    };

    ImGui::SetNextWindowSize(ImVec2(920.0f, 620.0f), ImGuiCond_FirstUseEver);
    bool keepToolboxOpen = true;
    if (!ImGui::BeginPopupModal("Toolbox", &keepToolboxOpen, ImGuiWindowFlags_NoResize))
        return;

    if (!keepToolboxOpen)
    {
        closeToolboxWithoutApplying();
        ImGui::EndPopup();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        closeToolboxWithoutApplying();
        ImGui::EndPopup();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeightWithSpacing() +
                               style.WindowPadding.y + style.ItemSpacing.y * 2.0f;

    ImGui::TextDisabled("Feature modules are applied per project and rebuild the editor/runtime immediately.");
    ImGui::Separator();
    ImGui::Text("Utilities");
    if (ImGui::Button("Auto Generate Reflection Probes"))
        RunAutoReflectionProbeUtility();
    ImGui::SameLine();
    if (ImGui::Button("Clear Auto Reflection Probes"))
        ClearAutoReflectionProbeUtility();
    ImGui::TextDisabled("Scans visible scene geometry, places regional box probes, and bakes them in the background.");
    if (!m_ToolboxUtilityStatusMessage.empty())
    {
        if (m_ToolboxUtilityStatusIsError)
            ImGui::TextColored(ImVec4(0.92f, 0.44f, 0.44f, 1.0f), "%s", m_ToolboxUtilityStatusMessage.c_str());
        else
            ImGui::TextColored(ImVec4(0.60f, 0.82f, 0.62f, 1.0f), "%s", m_ToolboxUtilityStatusMessage.c_str());
    }
    ImGui::Spacing();

    const bool includeAdvanced = toolbox.IsDraftAdvancedViewEnabled();
    const std::vector<std::string> categories = toolbox.GetVisibleCategories(includeAdvanced);
    if (std::find(categories.begin(), categories.end(), m_ToolboxSelectedCategory) == categories.end())
        m_ToolboxSelectedCategory = "All";

    ImGui::BeginChild("##ToolboxCategories", ImVec2(180.0f, -footerHeight), true);
    for (const std::string& category : categories)
    {
        const bool selected = (m_ToolboxSelectedCategory == category);
        if (ImGui::Selectable(category.c_str(), selected))
            m_ToolboxSelectedCategory = category;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##ToolboxModules", ImVec2(0.0f, -footerHeight), false);
    for (const ToolboxModuleDescriptor* descriptor : toolbox.GetVisibleModulesForCategory(m_ToolboxSelectedCategory, includeAdvanced))
    {
        ImGui::PushID(descriptor->id);
        ImGui::BeginChild("##ModuleCard", ImVec2(0.0f, 96.0f), true);

        bool enabled = toolbox.IsDraftModuleEnabled(descriptor->id);
        if (descriptor->alwaysEnabled)
            ImGui::BeginDisabled();
        if (ImGui::Checkbox(descriptor->displayName, &enabled))
            toolbox.SetDraftModuleEnabled(descriptor->id, enabled);
        if (descriptor->alwaysEnabled)
            ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", descriptor->bundle);
        if (descriptor->advancedOnly)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Advanced");
        }
        if (descriptor->alwaysEnabled)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "Always On");
        }

        ImGui::TextWrapped("%s", descriptor->description);
        if (std::string_view(descriptor->id) == ToolboxModuleIds::kMcpBridge && m_McpBridgeService)
        {
            const McpBridgeService::Status status = m_McpBridgeService->GetStatus();
            if (!status.enabled)
                ImGui::TextDisabled("Bridge status: disabled");
            else if (!status.lastError.empty())
                ImGui::TextColored(ImVec4(0.85f, 0.45f, 0.45f, 1.0f), "Bridge error: %s", status.lastError.c_str());
            else
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.62f, 1.0f), "Bridge ready on %s (%zu clients)",
                                   status.pipeName.c_str(), status.connectedClients);
        }
        const std::string dependencySummary = toolbox.GetDependencySummary(*descriptor);
        if (!dependencySummary.empty())
            ImGui::TextDisabled("%s", dependencySummary.c_str());

        if (!descriptor->alwaysEnabled)
        {
            const std::vector<const ToolboxModuleDescriptor*> dependents = toolbox.GetDependents(descriptor->id);
            if (!dependents.empty())
            {
                std::string dependentSummary = "Affects ";
                for (size_t i = 0; i < dependents.size(); ++i)
                {
                    dependentSummary += dependents[i]->displayName;
                    if (i + 1 < dependents.size())
                        dependentSummary += ", ";
                }
                ImGui::TextDisabled("%s", dependentSummary.c_str());
            }
        }

        ImGui::EndChild();
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::EndChild();

    ImGui::BeginChild("##ToolboxFooter", ImVec2(0.0f, footerHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Separator();
    bool showAdvanced = toolbox.IsDraftAdvancedViewEnabled();
    if (ImGui::Checkbox("Show advanced modules", &showAdvanced))
        toolbox.SetDraftAdvancedView(showAdvanced);

    ImGui::SameLine();
    ImGui::TextDisabled("Apply stops Play Mode, rebuilds runtime/editor bindings, and preserves scene data.");

    constexpr float restoreButtonWidth = 110.0f;
    constexpr float revertButtonWidth = 80.0f;
    constexpr float applyButtonWidth = 100.0f;
    constexpr float closeButtonWidth = 80.0f;
    const float footerButtonRowWidth = restoreButtonWidth + revertButtonWidth + applyButtonWidth + closeButtonWidth +
                                       style.ItemSpacing.x * 3.0f;
    const float footerButtonStartX =
        std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - footerButtonRowWidth);

    ImGui::SetCursorPosX(footerButtonStartX);
    if (ImGui::Button("Restore Defaults", ImVec2(restoreButtonWidth, 0.0f)))
        toolbox.RestoreDraftDefaults();

    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(revertButtonWidth, 0.0f)))
        toolbox.ResetDraft();

    ImGui::SameLine();
    ImGui::BeginDisabled(!toolbox.HasDraftChanges());
    if (ImGui::Button("Apply", ImVec2(applyButtonWidth, 0.0f)))
    {
        ApplyToolboxChanges();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(closeButtonWidth, 0.0f)))
    {
        closeToolboxWithoutApplying();
    }
    ImGui::EndChild();

    ImGui::EndPopup();
}

void Application::CreateFreshScene()
{
    if (!m_ViewportPanel)
        return;

    m_ViewportPanel->SetSelectedEntity(Entity{});
    if (m_HierarchyPanel)
        m_HierarchyPanel->SetSelectedEntity(Entity{});
    if (m_SceneContext)
        m_SceneContext->GetEntitySelection().Clear();

    m_ViewportPanel->ResetWorld();
    if (m_SceneContext)
        m_SceneContext->BindWorld(&m_ViewportPanel->GetWorld());

    if (m_HierarchyPanel)
        m_HierarchyPanel->SetWorld(&m_ViewportPanel->GetWorld());
    if (m_DebugPanel)
        m_DebugPanel->SetWorld(&m_ViewportPanel->GetWorld());
    if (m_DebugPanel)
        m_DebugPanel->SetFrameGraph(&m_ViewportPanel->GetFrameGraph());
    RebuildRuntimeFromToolbox();

    World& world = m_ViewportPanel->GetWorld();
    Entity cube = world.CreateEntity();

    auto& name = world.AddComponent<NameComponent>(cube);
    name.name = "Cube";

    auto& transform = world.AddComponent<TransformComponent>(cube);
    transform.position = {0.0f, 0.0f, 0.0f};
    transform.rotation = {0.0f, 0.0f, 0.0f};
    transform.scale = {1.0f, 1.0f, 1.0f};

    world.AddComponent<HierarchyComponent>(cube);

    if (m_MapDocument)
        m_MapDocument->New();
    if (m_SceneMapDocument)
        m_SceneMapDocument->New();
    if (m_SceneContext)
    {
        m_SceneContext->ResetSceneSettings();
        m_SceneContext->ClearScenePath();
    }
    CrashHandling::SetScenePath({});
    if (m_SceneRuntime && m_SceneMapDocument)
        m_SceneRuntime->SetStaticWorldGeometry(m_SceneMapDocument->GetStaticWorldGeometry());

    CommandRegistry::Get().ClearHistory();
    CommitSceneSnapshotBaseline();
    UpdateWindowTitle();

    DOT_LOG_INFO("New Scene created");
}

bool Application::LoadSceneFromPath(const std::filesystem::path& path)
{
    if (!m_ViewportPanel)
        return false;

    DOT_LOG_INFO("LoadSceneFromPath: begin");

    m_ViewportPanel->SetSelectedEntity(Entity{});
    if (m_HierarchyPanel)
        m_HierarchyPanel->SetSelectedEntity(Entity{});

    DOT_LOG_INFO("LoadSceneFromPath: before ResetWorld");
    m_ViewportPanel->ResetWorld();
    DOT_LOG_INFO("LoadSceneFromPath: after ResetWorld");
    if (m_SceneContext)
    {
        m_SceneContext->BindWorld(&m_ViewportPanel->GetWorld());
        m_SceneContext->GetEntitySelection().Clear();
    }

    if (m_HierarchyPanel)
        m_HierarchyPanel->SetWorld(&m_ViewportPanel->GetWorld());
    if (m_DebugPanel)
        m_DebugPanel->SetWorld(&m_ViewportPanel->GetWorld());
    if (m_DebugPanel)
        m_DebugPanel->SetFrameGraph(&m_ViewportPanel->GetFrameGraph());
    DOT_LOG_INFO("LoadSceneFromPath: before RebuildRuntimeFromToolbox");
    RebuildRuntimeFromToolbox();
    DOT_LOG_INFO("LoadSceneFromPath: after RebuildRuntimeFromToolbox");

    SceneSerializer serializer;
    DOT_LOG_INFO("LoadSceneFromPath: before serializer.Load");
    if (!serializer.Load(m_ViewportPanel->GetWorld(), path.string()))
    {
        std::printf("Failed to load scene: %s\n", serializer.GetLastError().c_str());
        return false;
    }
    DOT_LOG_INFO("LoadSceneFromPath: after serializer.Load");

    if (m_SceneContext)
        m_SceneContext->SetScenePath(path.string());
    CrashHandling::SetScenePath(path.string());
    LoadSceneSettingsFromSerializer(path, serializer);
    MigrateLegacySceneGlobalsToSceneSettings();
    DOT_LOG_INFO("LoadSceneFromPath: before SyncMapDocumentFromScene");
    SyncMapDocumentFromScene();
    DOT_LOG_INFO("LoadSceneFromPath: after SyncMapDocumentFromScene");
    if (m_SceneContext)
        m_SceneContext->SetSceneDirty(false);
    CommandRegistry::Get().ClearHistory();
    DOT_LOG_INFO("LoadSceneFromPath: before CommitSceneSnapshotBaseline");
    CommitSceneSnapshotBaseline();
    DOT_LOG_INFO("LoadSceneFromPath: after CommitSceneSnapshotBaseline");
    m_ViewportPanel->SetSelectedEntity(Entity{});
    if (m_HierarchyPanel)
        m_HierarchyPanel->SetSelectedEntity(Entity{});
    UpdateWindowTitle();
    DOT_LOG_INFO("LoadSceneFromPath: end");
    std::printf("Scene loaded: %s\n", path.string().c_str());
    return true;
}

void Application::ExecuteSceneActionWithSavePrompt(std::function<void()> action, const char* actionDescription)
{
    if (!action)
        return;

    const bool hasUnsavedChanges = HasUnsavedSceneChanges();
    UpdateWindowTitle();

    if (!hasUnsavedChanges)
    {
        action();
        return;
    }

    std::string message = "Do you want to save the current scene before you ";
    message += actionDescription;
    message += "?";

    m_ConfirmationDialog.Open(
        "Unsaved Changes",
        std::move(message),
        ConfirmationDialog::Button{
            "Save",
            [this, action = action]() mutable
            {
                if (!SaveScene())
                    return false;
                action();
                return true;
            },
            true,
            false,
        },
        ConfirmationDialog::Button{
            "Don't Save",
            [action = action]() mutable
            {
                action();
                return true;
            },
            false,
            true,
        },
        ConfirmationDialog::Button{
            "Cancel",
            []() { return true; },
            false,
            false,
        });
}

std::string Application::CaptureSceneSnapshot() const
{
    if (!m_ViewportPanel || !m_SceneContext)
        return {};

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "dot_editor_scene_snapshot.dotscene";
    const std::filesystem::path tempSettingsPath =
        std::filesystem::temp_directory_path() / "dot_editor_scene_snapshot.dotscene.settings.json";

    SceneSerializer serializer;
    SceneSettingsSerializer settingsSerializer;
    if (!settingsSerializer.Save(m_SceneContext->GetSceneSettings(), tempSettingsPath))
        return {};
    serializer.SetSceneSettingsReference(tempSettingsPath.filename().generic_string());
    if (!serializer.Save(m_ViewportPanel->GetWorld(), tempPath.string()))
        return {};

    std::ifstream file(tempPath, std::ios::binary);
    if (!file)
        return {};

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void Application::CommitSceneSnapshotBaseline()
{
    if (!m_SceneContext)
        return;
    m_SceneContext->SetLastSavedSceneSnapshot(CaptureSceneSnapshot());
    m_SceneContext->SetSceneDirty(false);
    m_SceneContext->SetSceneDirtyCheckAccumulator(0.0f);
    m_SceneContext->SetForceSceneDirtyCheck(false);
}

bool Application::HasUnsavedSceneChanges()
{
    if (!m_ViewportPanel || !m_SceneContext)
        return false;

    if (m_MapDocument && m_MapDocument->IsDirty())
        return true;

    if (m_SceneContext->IsSceneDirty())
        return true;

    const std::string currentSnapshot = CaptureSceneSnapshot();
    m_SceneContext->SetSceneDirty(currentSnapshot != m_SceneContext->GetLastSavedSceneSnapshot());
    return m_SceneContext->IsSceneDirty();
}

void Application::RefreshSceneDirtyState(float deltaTime, bool force)
{
    if (IsInPlayMode() || !m_ViewportPanel || !m_SceneContext)
        return;
    if (!WorkspaceUsesSceneDirtyPolling(m_CurrentWorkspace))
        return;

    m_SceneContext->SetSceneDirtyCheckAccumulator(m_SceneContext->GetSceneDirtyCheckAccumulator() + deltaTime);
    m_SceneContext->SetForceSceneDirtyCheck(m_SceneContext->GetForceSceneDirtyCheck() || force);

    constexpr float kSceneDirtyCheckInterval = 0.25f;
    if (!m_SceneContext->GetForceSceneDirtyCheck() &&
        m_SceneContext->GetSceneDirtyCheckAccumulator() < kSceneDirtyCheckInterval)
        return;

    const bool previousDirty = m_SceneContext->IsSceneDirty();
    HasUnsavedSceneChanges();
    m_SceneContext->SetSceneDirtyCheckAccumulator(0.0f);
    m_SceneContext->SetForceSceneDirtyCheck(false);

    if (m_SceneContext->IsSceneDirty() != previousDirty)
        UpdateWindowTitle();
}

void Application::UpdateWindowTitle()
{
    std::wstring title = m_Config.Title;

    if (m_SceneContext && !m_SceneContext->GetScenePath().empty())
    {
        // Extract filename and convert to wide string
        const std::string& scenePath = m_SceneContext->GetScenePath();
        size_t pos = scenePath.find_last_of("\\/");
        std::string filename = (pos != std::string::npos) ? scenePath.substr(pos + 1) : scenePath;

        title += L" - ";
        title += std::wstring(filename.begin(), filename.end());
    }
    else
    {
        title += L" - Untitled";
    }

    if (m_SceneContext && m_SceneContext->IsSceneDirty())
    {
        title += L" *";
    }

    SetWindowTextW(m_Window, title.c_str());
}

// =============================================================================
// Play Mode Implementation
// =============================================================================

void Application::Play()
{
    if (m_PlayState == PlayState::Playing)
        return;

    const bool wasStopped = (m_PlayState == PlayState::Stopped);

    if (wasStopped)
    {
        // First time playing - save scene snapshot
        m_SavedSceneSnapshot.clear();
        m_SavedSceneSettingsSnapshot.clear();

        // Serialize current scene to string (using SceneSerializer)
        // For now, we'll save to a temp file and read it back
        std::string tempPath = "temp_play_snapshot.dotscene";
        std::string tempSettingsPath = "temp_play_snapshot.dotscene.settings.json";
        SceneSerializer serializer;
        SceneSettingsSerializer settingsSerializer;
        if (m_SceneContext)
        {
            settingsSerializer.Save(m_SceneContext->GetSceneSettings(), tempSettingsPath);
            serializer.SetSceneSettingsReference(std::filesystem::path(tempSettingsPath).filename().generic_string());
        }
        serializer.Save(m_ViewportPanel->GetWorld(), tempPath);

        // Read back the file content
        std::ifstream file(tempPath);
        if (file)
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            m_SavedSceneSnapshot = buffer.str();
            file.close();
        }
        std::ifstream settingsFile(tempSettingsPath);
        if (settingsFile)
        {
            std::stringstream settingsBuffer;
            settingsBuffer << settingsFile.rdbuf();
            m_SavedSceneSettingsSnapshot = settingsBuffer.str();
            settingsFile.close();
        }

        m_PlayTime = 0.0f;
        std::printf("[PLAY] Starting play mode (scene snapshot saved)\n");
    }
    else
    {
        std::printf("[PLAY] Resuming from pause\n");
    }

    m_PlayState = PlayState::Playing;
    m_ViewportPanel->SetPlayMode(true); // Hide gizmos/grid

    if (m_SceneRuntime)
    {
        if (wasStopped)
            m_SceneRuntime->Start();
        else
            m_SceneRuntime->Resume();
    }

    // Enable mouse capture for FPS controls
    if (m_SceneRuntime)
    {
        m_SceneRuntime->SetMouseCaptured(true);
        ShowCursor(FALSE);
    }

    UpdateWindowTitle();
}

void Application::Pause()
{
    if (m_PlayState == PlayState::Playing)
    {
        m_PlayState = PlayState::Paused;
        if (m_SceneRuntime)
            m_SceneRuntime->Pause();
        std::printf("[PLAY] Paused\n");
        UpdateWindowTitle();
    }
}

void Application::Stop()
{
    if (m_PlayState == PlayState::Stopped)
        return;

    std::printf("[PLAY] Stopping play mode\n");

    // Restore scene from snapshot
    if (!m_SavedSceneSnapshot.empty())
    {
        // Write snapshot to temp file and reload
        std::string tempPath = "temp_play_snapshot.dotscene";
        std::string tempSettingsPath = "temp_play_snapshot.dotscene.settings.json";
        std::ofstream file(tempPath);
        if (file)
        {
            file << m_SavedSceneSnapshot;
            file.close();
            if (!m_SavedSceneSettingsSnapshot.empty())
            {
                std::ofstream settingsFile(tempSettingsPath);
                if (settingsFile)
                {
                    settingsFile << m_SavedSceneSettingsSnapshot;
                    settingsFile.close();
                }
            }

            // Clear current world and reload
            m_ViewportPanel->GetWorld().Clear();
            if (m_SceneContext)
                m_SceneContext->BindWorld(&m_ViewportPanel->GetWorld());
            SceneSerializer serializer;
            serializer.Load(m_ViewportPanel->GetWorld(), tempPath);
            LoadSceneSettingsFromSerializer(std::filesystem::path(tempPath), serializer);
            std::printf("[PLAY] Scene restored from snapshot\n");
        }
        m_SavedSceneSnapshot.clear();
        m_SavedSceneSettingsSnapshot.clear();
    }

    m_PlayState = PlayState::Stopped;
    m_PlayTime = 0.0f;
    m_ViewportPanel->SetPlayMode(false); // Show gizmos/grid again

    if (m_SceneRuntime)
    {
        m_SceneRuntime->Stop();
    }

    // Release mouse capture
    if (m_SceneRuntime)
    {
        m_SceneRuntime->SetMouseCaptured(false);
        ShowCursor(TRUE);
    }

    UpdateWindowTitle();
}

void Application::DrawDebugVisOverlay()
{
    auto& viewSettings = ViewSettings::Get();
    if (!IsDebugVisModeAvailable(viewSettings.debugVisMode))
    {
        viewSettings.debugVisMode = SanitizeDebugVisMode(viewSettings.debugVisMode);
        viewSettings.SyncLegacyFromDebugVis();
    }
    const DebugVisMode currentMode = viewSettings.debugVisMode;
    const char* currentModeName = GetDebugVisModeName(currentMode);
    const bool isNonDefaultMode = (currentMode != DebugVisMode::Lit);

    // Position the overlay window at top-left of the main viewport, below the menu bar
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 overlayPos = ImVec2(viewport->WorkPos.x + 400.0f, viewport->WorkPos.y + 6.0f);
    ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f); // Fully transparent background

    ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
                                    ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##DebugVisOverlay", nullptr, overlayFlags))
    {
        // Button color: subtle gray for Lit, tinted for debug modes
        ImVec4 buttonColor = isNonDefaultMode ? ImVec4(0.15f, 0.35f, 0.55f, 0.92f)
                                              : ImVec4(0.12f, 0.12f, 0.14f, 0.85f);
        ImVec4 buttonHoverColor = isNonDefaultMode ? ImVec4(0.2f, 0.4f, 0.6f, 0.95f)
                                                   : ImVec4(0.18f, 0.18f, 0.22f, 0.95f);

        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHoverColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.45f, 0.65f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));

        char buttonLabel[64];
        snprintf(buttonLabel, sizeof(buttonLabel), "%s  " "\xe2\x96\xbc" "##DebugVisMode", currentModeName);

        if (ImGui::Button(buttonLabel))
            ImGui::OpenPopup("##DebugVisPopup");

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        // Colored indicator bar below button when non-default mode
        if (isNonDefaultMode)
        {
            ImVec2 btnMin = ImGui::GetItemRectMin();
            ImVec2 btnMax = ImGui::GetItemRectMax();
            ImU32 indicatorColor = IM_COL32(80, 160, 240, 255);
            DebugVisCategory category = GetDebugVisModeCategory(currentMode);
            switch (category)
            {
            case DebugVisCategory::Lighting:   indicatorColor = IM_COL32(240, 200, 60, 255); break;
            case DebugVisCategory::Geometry:   indicatorColor = IM_COL32(100, 220, 120, 255); break;
            case DebugVisCategory::Materials:  indicatorColor = IM_COL32(220, 120, 220, 255); break;
            case DebugVisCategory::Performance:indicatorColor = IM_COL32(240, 100, 80, 255); break;
            default: break;
            }
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(btnMin.x, btnMax.y), ImVec2(btnMax.x, btnMax.y + 2.0f), indicatorColor);
        }

        // Popup dropdown
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.1f, 0.97f));

        if (ImGui::BeginPopup("##DebugVisPopup"))
        {
            DebugVisCategory lastCategory = DebugVisCategory::Count;
            for (uint8_t i = 0; i < static_cast<uint8_t>(DebugVisMode::Count); ++i)
            {
                DebugVisMode mode = static_cast<DebugVisMode>(i);
                if (!IsDebugVisModeAvailable(mode))
                    continue;
                DebugVisCategory category = GetDebugVisModeCategory(mode);

                if (category != lastCategory)
                {
                    if (lastCategory != DebugVisCategory::Count)
                        ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.5f, 0.65f, 0.8f, 1.0f), "%s",
                                       GetDebugVisCategoryName(category));
                    lastCategory = category;
                }

                const bool isSelected = (mode == currentMode);
                if (isSelected)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));

                char entryLabel[80];
                snprintf(entryLabel, sizeof(entryLabel), "  %s %s",
                         isSelected ? "\xe2\x97\x8f" : "\xe2\x97\x8b",
                         GetDebugVisModeName(mode));

                if (ImGui::Selectable(entryLabel, isSelected, 0, ImVec2(180, 0)))
                {
                    viewSettings.debugVisMode = mode;
                    viewSettings.SyncLegacyFromDebugVis();
                }

                if (isSelected)
                    ImGui::PopStyleColor();
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    ImGui::End();
}

void Application::DrawToolbar()
{
    ImGuiWindowFlags toolbarFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

    // Use a dockable window for the toolbar
    ImGui::SetNextWindowSize(ImVec2(400, 45), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Play Controls", nullptr, toolbarFlags))
    {
        ImGui::End();
        return;
    }

    // Play/Resume button
    bool isPlaying = (m_PlayState == PlayState::Playing);
    bool isPaused = (m_PlayState == PlayState::Paused);
    bool isStopped = (m_PlayState == PlayState::Stopped);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,
                          isPlaying ? ImVec4(0.19f, 0.45f, 0.28f, 1.0f) : ImVec4(0.20f, 0.27f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          isPlaying ? ImVec4(0.24f, 0.55f, 0.35f, 1.0f) : ImVec4(0.26f, 0.35f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          isPlaying ? ImVec4(0.27f, 0.60f, 0.39f, 1.0f) : ImVec4(0.30f, 0.41f, 0.52f, 1.0f));

    if (ImGui::Button(isPlaying ? "||" : ">", ImVec2(30, 30)))
    {
        if (isPlaying)
            Pause();
        else
            Play();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(isPlaying ? "Pause (F6)" : isStopped ? "Play (F5)" : "Resume (F5)");
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    // Stop button
    ImGui::BeginDisabled(isStopped);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.19f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.44f, 0.24f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.52f, 0.28f, 0.31f, 1.0f));
    if (ImGui::Button("X", ImVec2(30, 30)))
    {
        Stop();
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop (Shift+F5)");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Status display
    if (isPlaying)
    {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "PLAYING");
    }
    else if (isPaused)
    {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "PAUSED");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "STOPPED");
    }

    // FPS display always visible - use ImGui's built-in accurate framerate
    ImGui::SameLine();
    ImGui::Text("  |  FPS: %.1f", ImGui::GetIO().Framerate);

    // Time display only when playing
    if (!isStopped)
    {
        ImGui::SameLine();
        ImGui::Text("  |  Time: %.1fs", m_PlayTime);
    }

    ImGui::End();
}
// Workspace System
// =============================================================================

void Application::DrawWorkspaceTabs()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tabWidth = 92.0f;
    struct WorkspaceTabEntry
    {
        const char* label;
        WorkspaceType type;
    };

    std::vector<WorkspaceTabEntry> tabs = {{"Layout", WorkspaceType::Layout}};
    if (ToolboxManager::Get().IsMapEditorEnabled())
        tabs.push_back({"Map", WorkspaceType::Map});
    if (ToolboxManager::Get().IsUIEditorEnabled())
        tabs.push_back({"UI", WorkspaceType::UI});
    if (ToolboxManager::Get().IsMaterialEditorEnabled())
        tabs.push_back({"Material", WorkspaceType::Material});
    if (ToolboxManager::Get().IsScriptingEnabled())
        tabs.push_back({"Scripting", WorkspaceType::Scripting});

    const float totalWidth =
        (tabWidth * static_cast<float>(tabs.size())) + (style.ItemSpacing.x * static_cast<float>(tabs.size() > 0 ? tabs.size() - 1 : 0));
    ImGui::SameLine((ImGui::GetWindowWidth() * 0.5f) - (totalWidth * 0.5f));

    ImVec2 tabSize(tabWidth, 0.0f);

    auto drawWorkspaceTab = [&](const char* label, bool selected, WorkspaceType type)
    {
        ImGui::PushStyleColor(ImGuiCol_Header, selected ? ImVec4(0.23f, 0.34f, 0.47f, 1.0f)
                                                        : ImVec4(0.14f, 0.17f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                              selected ? ImVec4(0.28f, 0.40f, 0.55f, 1.0f) : ImVec4(0.20f, 0.25f, 0.31f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.31f, 0.45f, 0.61f, 1.0f));
        if (ImGui::Selectable(label, selected, 0, tabSize))
            SwitchWorkspace(type);
        ImGui::PopStyleColor(3);
    };

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        drawWorkspaceTab(tabs[i].label, m_CurrentWorkspace == tabs[i].type, tabs[i].type);
        if (i + 1 < tabs.size())
            ImGui::SameLine();
    }
}

void Application::SwitchWorkspace(WorkspaceType type)
{
    if (!ToolboxManager::Get().IsWorkspaceEnabled(type))
        type = WorkspaceType::Layout;

    if (m_CurrentWorkspace == type)
        return;

    // Deactivate current workspace
    switch (m_CurrentWorkspace)
    {
        case WorkspaceType::Layout:
            if (m_LayoutWorkspace)
                m_LayoutWorkspace->OnDeactivate();
            break;
        case WorkspaceType::Map:
            if (m_MapWorkspace)
                m_MapWorkspace->OnDeactivate();
            break;
        case WorkspaceType::UI:
            if (m_UIWorkspace)
                m_UIWorkspace->OnDeactivate();
            break;
        case WorkspaceType::Scripting:
            if (m_ScriptingWorkspace)
                m_ScriptingWorkspace->OnDeactivate();
            break;
        case WorkspaceType::Material:
            if (m_MaterialWorkspace)
                m_MaterialWorkspace->OnDeactivate();
            break;
    }

    m_CurrentWorkspace = type;

    // Activate new workspace
    switch (m_CurrentWorkspace)
    {
        case WorkspaceType::Layout:
            if (m_LayoutWorkspace)
                m_LayoutWorkspace->OnActivate();
            break;
        case WorkspaceType::Map:
            if (m_MapWorkspace)
                m_MapWorkspace->OnActivate();
            break;
        case WorkspaceType::UI:
            if (m_UIWorkspace)
                m_UIWorkspace->OnActivate();
            break;
        case WorkspaceType::Scripting:
            if (m_ScriptingWorkspace)
                m_ScriptingWorkspace->OnActivate();
            break;
        case WorkspaceType::Material:
            if (m_MaterialWorkspace)
                m_MaterialWorkspace->OnActivate();
            break;
    }
}

} // namespace Dot
