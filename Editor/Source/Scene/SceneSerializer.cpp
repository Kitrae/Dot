// =============================================================================
// Dot Engine - Scene Serializer Implementation
// =============================================================================
// Uses reflection system to serialize all components on entities.
// =============================================================================

#include "SceneSerializer.h"

#include <Core/ECS/World.h>
#include <Core/Gameplay/HealthComponent.h>
#include <Core/Input/PlayerInputComponent.h>
#include <Core/Navigation/NavAgentComponent.h>
#include <Core/Physics/BoxColliderComponent.h>
#include <Core/Physics/CharacterControllerComponent.h>
#include <Core/Physics/RigidBodyComponent.h>
#include <Core/Physics/SphereColliderComponent.h>
#include <Core/Reflect/Registry.h>
#include <Core/Reflect/Serialization.h>
#include <Core/Scene/CameraComponent.h>
#include <Core/Scene/Components.h>
#include <Core/Scene/LightComponent.h>
#include <Core/Scene/MaterialComponent.h>
#include <Core/Scene/MapComponent.h>
#include <Core/Scene/MeshComponent.h>
#include <Core/Scene/ScriptComponent.h>
#include <Core/Scene/SkyboxComponent.h>
#include <Core/Scene/StaticLightingComponent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace Dot
{

// =============================================================================
// Helper Structures
// =============================================================================

/// Component serializer entry - pairs type checking with serialization
struct ComponentSerializer
{
    const char* jsonName;
    const char* typeName;
    std::function<bool(const World&, Entity)> hasComponent;
    std::function<void*(World&, Entity)> getComponent;
    std::function<void(World&, Entity)> addComponent;
};

// Static list of serializable components
static const std::vector<ComponentSerializer>& GetComponentSerializers()
{
    static std::vector<ComponentSerializer> serializers = {
        {"transform", "TransformComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<TransformComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<TransformComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<TransformComponent>(e); }},

        {"name", "NameComponent", [](const World& w, Entity e)
         { return const_cast<World&>(w).HasComponent<NameComponent>(e); }, [](World& w, Entity e) -> void*
         { return w.GetComponent<NameComponent>(e); }, [](World& w, Entity e) { w.AddComponent<NameComponent>(e); }},

        {"hierarchy", "HierarchyComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<HierarchyComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<HierarchyComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<HierarchyComponent>(e); }},

        {"primitive", "PrimitiveComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<PrimitiveComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<PrimitiveComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<PrimitiveComponent>(e); }},

        {"active", "ActiveComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<ActiveComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<ActiveComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<ActiveComponent>(e); }},

        {"attachmentBinding", "AttachmentBindingComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<AttachmentBindingComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<AttachmentBindingComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<AttachmentBindingComponent>(e); }},

        {"attachmentPoint", "AttachmentPointComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<AttachmentPointComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<AttachmentPointComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<AttachmentPointComponent>(e); }},

        {"renderLayer", "RenderLayerComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<RenderLayerComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<RenderLayerComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<RenderLayerComponent>(e); }},

        {"directionalLight", "DirectionalLightComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<DirectionalLightComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<DirectionalLightComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<DirectionalLightComponent>(e); }},

        {"pointLight", "PointLightComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<PointLightComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<PointLightComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<PointLightComponent>(e); }},

        {"spotLight", "SpotLightComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<SpotLightComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<SpotLightComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<SpotLightComponent>(e); }},

        {"reflectionProbe", "ReflectionProbeComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<ReflectionProbeComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<ReflectionProbeComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<ReflectionProbeComponent>(e); }},

        {"mesh", "MeshComponent", [](const World& w, Entity e)
         { return const_cast<World&>(w).HasComponent<MeshComponent>(e); }, [](World& w, Entity e) -> void*
         { return w.GetComponent<MeshComponent>(e); }, [](World& w, Entity e) { w.AddComponent<MeshComponent>(e); }},

        {"material", "MaterialComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<MaterialComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<MaterialComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<MaterialComponent>(e); }},

        {"staticLighting", "StaticLightingComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<StaticLightingComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<StaticLightingComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<StaticLightingComponent>(e); }},

        {"map", "MapComponent", [](const World& w, Entity e)
         { return const_cast<World&>(w).HasComponent<MapComponent>(e); }, [](World& w, Entity e) -> void*
         { return w.GetComponent<MapComponent>(e); }, [](World& w, Entity e) { w.AddComponent<MapComponent>(e); }},

        {"navAgent", "NavAgentComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<NavAgentComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<NavAgentComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<NavAgentComponent>(e); }},

        {"skybox", "SkyboxComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<SkyboxComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<SkyboxComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<SkyboxComponent>(e); }},

        {"camera", "CameraComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<CameraComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<CameraComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<CameraComponent>(e); }},

        {"health", "HealthComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<HealthComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<HealthComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<HealthComponent>(e); }},

        {"rigidBody", "RigidBodyComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<RigidBodyComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<RigidBodyComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<RigidBodyComponent>(e); }},

        {"boxCollider", "BoxColliderComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<BoxColliderComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<BoxColliderComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<BoxColliderComponent>(e); }},

        {"sphereCollider", "SphereColliderComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<SphereColliderComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<SphereColliderComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<SphereColliderComponent>(e); }},

        {"characterController", "CharacterControllerComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<CharacterControllerComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<CharacterControllerComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<CharacterControllerComponent>(e); }},

        {"playerInput", "PlayerInputComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<PlayerInputComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<PlayerInputComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<PlayerInputComponent>(e); }},

        {"script", "ScriptComponent",
         [](const World& w, Entity e) { return const_cast<World&>(w).HasComponent<ScriptComponent>(e); },
         [](World& w, Entity e) -> void* { return w.GetComponent<ScriptComponent>(e); },
         [](World& w, Entity e) { w.AddComponent<ScriptComponent>(e); }},
    };
    return serializers;
}

// =============================================================================
// JSON Helpers
// =============================================================================

static std::string Trim(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

static std::string FindJsonValue(const std::string& json, const char* key)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t keyStart = json.find(token);
    if (keyStart == std::string::npos)
        return {};

    const size_t colonPos = json.find(':', keyStart + token.size());
    if (colonPos == std::string::npos)
        return {};

    size_t valueStart = json.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos)
        return {};

    if (json[valueStart] == '"')
    {
        const size_t valueEnd = json.find('"', valueStart + 1);
        if (valueEnd == std::string::npos)
            return {};
        return json.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    const size_t valueEnd = json.find_first_of(",}\r\n", valueStart);
    return Trim(json.substr(valueStart, valueEnd - valueStart));
}

// =============================================================================
// Save Implementation
// =============================================================================

bool SceneSerializer::Save(const World& world, const std::string& filepath)
{
    m_LastError.clear();
    std::ofstream file(filepath);
    if (!file.is_open())
    {
        m_LastError = "Failed to open file for writing: " + filepath;
        return false;
    }

    // Collect all entities
    std::vector<Entity> entities;
    const_cast<World&>(world).EachEntity([&](Entity entity) { entities.push_back(entity); });

    // Build entity index map (entity -> save index)
    std::unordered_map<uint32, int32> entityIndexMap;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        entityIndexMap[entities[i].GetIndex()] = static_cast<int32>(i);
    }

    // Write scene header
    file << "{\n";
    file << "  \"scene\": {\n";
    file << "    \"version\": 3,\n";
    if (!m_SceneSettingsReference.empty())
        file << "    \"settingsAsset\": \"" << m_SceneSettingsReference << "\",\n";
    file << "    \"entities\": [\n";

    // Write each entity
    for (size_t i = 0; i < entities.size(); ++i)
    {
        Entity entity = entities[i];
        file << "      {\n";
        file << "        \"id\": " << i << ",\n";

        // Serialize each component using reflection
        bool firstComponent = true;
        for (const auto& serializer : GetComponentSerializers())
        {
            if (!serializer.hasComponent(world, entity))
                continue;

            void* component = serializer.getComponent(const_cast<World&>(world), entity);
            const TypeInfo* typeInfo = TypeRegistry::Get().GetType(serializer.typeName);

            if (!component || !typeInfo)
                continue;

            if (!firstComponent)
                file << ",\n";
            firstComponent = false;

            file << "        \"" << serializer.jsonName << "\": {\n";

            // Serialize properties
            auto props = typeInfo->GetAllProperties();
            for (size_t p = 0; p < props.size(); ++p)
            {
                const Property* prop = props[p];
                if (HasFlag(prop->flags, PropertyFlags::Transient))
                    continue;

                file << "          \"" << prop->name << "\": ";

                // Scene files store entity references as save-local indices so they can be remapped on load.
                if (prop->type == PropertyType::Entity)
                {
                    Entity* entityPtr = static_cast<Entity*>(prop->getter(component));
                    if (entityPtr->IsValid())
                    {
                        auto it = entityIndexMap.find(entityPtr->GetIndex());
                        file << (it != entityIndexMap.end() ? it->second : -1);
                    }
                    else
                    {
                        file << -1;
                    }
                }
                else
                {
                    // Use standard serialization
                    std::ostringstream oss;
                    SerializePropertyToJson(oss, *prop, component);
                    file << oss.str();
                }

                if (p < props.size() - 1)
                    file << ",";
                file << "\n";
            }

            file << "        }";
        }

        file << "\n      }";
        if (i < entities.size() - 1)
            file << ",";
        file << "\n";
    }

    file << "    ]\n";
    file << "  }\n";
    file << "}\n";

    file.close();
    return true;
}

// =============================================================================
// Load Implementation
// =============================================================================

/// Parse a simple JSON object into key-value pairs (single level)
static std::unordered_map<std::string, std::string> ParseSimpleJsonObject(const std::string& json)
{
    std::unordered_map<std::string, std::string> result;

    size_t pos = 0;
    while (pos < json.size())
    {
        // Find key
        size_t keyStart = json.find('"', pos);
        if (keyStart == std::string::npos)
            break;
        size_t keyEnd = json.find('"', keyStart + 1);
        if (keyEnd == std::string::npos)
            break;

        std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

        // Find colon
        size_t colonPos = json.find(':', keyEnd);
        if (colonPos == std::string::npos)
            break;

        // Find value
        size_t valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos)
            break;

        std::string value;
        if (json[valueStart] == '"')
        {
            // String value
            size_t valueEnd = json.find('"', valueStart + 1);
            value = json.substr(valueStart, valueEnd - valueStart + 1);
            pos = valueEnd + 1;
        }
        else if (json[valueStart] == '[')
        {
            // Array value
            size_t valueEnd = json.find(']', valueStart);
            value = json.substr(valueStart, valueEnd - valueStart + 1);
            pos = valueEnd + 1;
        }
        else if (json[valueStart] == '{')
        {
            // Nested object - skip for now
            int depth = 1;
            size_t scan = valueStart + 1;
            while (scan < json.size() && depth > 0)
            {
                if (json[scan] == '{')
                    depth++;
                else if (json[scan] == '}')
                    depth--;
                scan++;
            }
            value = json.substr(valueStart, scan - valueStart);
            pos = scan;
        }
        else
        {
            // Number or boolean
            size_t valueEnd = json.find_first_of(",}\n", valueStart);
            value = Trim(json.substr(valueStart, valueEnd - valueStart));
            pos = valueEnd;
        }

        result[key] = value;
    }

    return result;
}

bool SceneSerializer::Load(World& world, const std::string& filepath)
{
    m_LastError.clear();
    m_SceneSettingsReference.clear();
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        m_LastError = "Failed to open file for reading: " + filepath;
        return false;
    }

    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    m_SceneSettingsReference = FindJsonValue(content, "settingsAsset");

    // Parse entities - find each entity block
    std::vector<Entity> createdEntities;
    std::vector<int32> parentIndices;
    struct DeferredEntityReference
    {
        Entity owner = kNullEntity;
        const ComponentSerializer* serializer = nullptr;
        std::string propertyName;
        int32 targetIndex = -1;
    };
    std::vector<DeferredEntityReference> deferredEntityReferences;

    size_t searchPos = 0;
    while (true)
    {
        // Find entity block start
        size_t entityStart = content.find("\"id\":", searchPos);
        if (entityStart == std::string::npos)
            break;

        // Find the enclosing { for this entity
        size_t blockStart = content.rfind('{', entityStart);

        // Find matching }
        int depth = 1;
        size_t blockEnd = blockStart + 1;
        while (blockEnd < content.size() && depth > 0)
        {
            if (content[blockEnd] == '{')
                depth++;
            else if (content[blockEnd] == '}')
                depth--;
            blockEnd++;
        }

        std::string entityBlock = content.substr(blockStart, blockEnd - blockStart);

        // Create entity
        Entity entity = world.CreateEntity();
        createdEntities.push_back(entity);

        int32 parentIdx = -1;

        // Parse each component
        for (const auto& serializer : GetComponentSerializers())
        {
            std::string componentKey = "\"" + std::string(serializer.jsonName) + "\":";
            size_t compPos = entityBlock.find(componentKey);
            if (compPos == std::string::npos)
                continue;

            // Find component object
            size_t objStart = entityBlock.find('{', compPos);
            if (objStart == std::string::npos)
                continue;

            int objDepth = 1;
            size_t objEnd = objStart + 1;
            while (objEnd < entityBlock.size() && objDepth > 0)
            {
                if (entityBlock[objEnd] == '{')
                    objDepth++;
                else if (entityBlock[objEnd] == '}')
                    objDepth--;
                objEnd++;
            }

            std::string compJson = entityBlock.substr(objStart, objEnd - objStart);

            // Add component
            serializer.addComponent(world, entity);
            void* component = serializer.getComponent(world, entity);

            const TypeInfo* typeInfo = TypeRegistry::Get().GetType(serializer.typeName);
            if (!component || !typeInfo)
                continue;

            // Parse properties
            auto propMap = ParseSimpleJsonObject(compJson);
            for (const auto& [key, value] : propMap)
            {
                const Property* prop = typeInfo->GetProperty(key);
                if (!prop)
                    continue;

                // Scene files store entity references as save-local indices. Remap them after all entities exist.
                if (prop->type == PropertyType::Entity)
                {
                    const int32 entityIndex = std::stoi(value);
                    if (key == "parent")
                    {
                        parentIdx = entityIndex;
                    }
                    else
                    {
                        deferredEntityReferences.push_back({entity, &serializer, key, entityIndex});
                    }
                }
                else
                {
                    DeserializePropertyFromJson(*prop, component, value);
                }
            }
        }

        parentIndices.push_back(parentIdx);
        searchPos = blockEnd;
    }

    for (const DeferredEntityReference& deferred : deferredEntityReferences)
    {
        if (!deferred.owner.IsValid() || !deferred.serializer)
            continue;

        void* component = deferred.serializer->getComponent(world, deferred.owner);
        const TypeInfo* typeInfo = TypeRegistry::Get().GetType(deferred.serializer->typeName);
        if (!component || !typeInfo)
            continue;

        const Property* prop = typeInfo->GetProperty(deferred.propertyName);
        if (!prop || prop->type != PropertyType::Entity)
            continue;

        Entity resolved = kNullEntity;
        if (deferred.targetIndex >= 0 && deferred.targetIndex < static_cast<int32>(createdEntities.size()))
            resolved = createdEntities[deferred.targetIndex];
        prop->setter(component, &resolved);
    }

    // Resolve parent references
    for (size_t i = 0; i < createdEntities.size(); ++i)
    {
        int32 parentIdx = parentIndices[i];
        if (parentIdx >= 0 && parentIdx < static_cast<int32>(createdEntities.size()))
        {
            Entity child = createdEntities[i];
            Entity parent = createdEntities[parentIdx];

            // Set parent reference
            auto* hierarchy = world.GetComponent<HierarchyComponent>(child);
            if (hierarchy)
            {
                hierarchy->parent = parent;

                // Add to parent's children
                auto* parentHierarchy = world.GetComponent<HierarchyComponent>(parent);
                if (parentHierarchy)
                {
                    parentHierarchy->AddChild(child);
                }
            }
        }
    }

    return true;
}

} // namespace Dot
