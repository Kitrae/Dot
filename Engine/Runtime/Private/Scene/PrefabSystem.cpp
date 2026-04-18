// =============================================================================
// Dot Engine - Prefab System Implementation
// =============================================================================

#include "Core/Scene/PrefabSystem.h"

#include "Core/ECS/World.h"
#include "Core/Gameplay/HealthComponent.h"
#include "Core/Input/PlayerInputComponent.h"
#include "Core/Log.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MaterialComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/PrefabComponent.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scene/SkyboxComponent.h"
#include "Core/Scene/StaticLightingComponent.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace Dot
{

namespace
{

void BuildPrefabIndexMap(World& world, Entity entity, std::unordered_map<uint32, int>& entityToPrefabIndex)
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return;

    entityToPrefabIndex.emplace(entity.GetIndex(), static_cast<int>(entityToPrefabIndex.size()));

    if (HierarchyComponent* hierarchy = world.GetComponent<HierarchyComponent>(entity))
    {
        for (Entity child : hierarchy->children)
        {
            if (child.IsValid())
                BuildPrefabIndexMap(world, child, entityToPrefabIndex);
        }
    }
}

} // namespace

// Static instance ID counter
uint32 PrefabSystem::s_NextInstanceId = 1;

uint32 PrefabSystem::GenerateInstanceId()
{
    return s_NextInstanceId++;
}

Prefab PrefabSystem::CreateFromEntity(World& world, Entity root, const std::string& prefabName)
{
    Prefab prefab;

    // Get entity name
    std::string name = prefabName;
    if (name.empty())
    {
        NameComponent* nameComp = world.GetComponent<NameComponent>(root);
        if (nameComp)
            name = nameComp->name;
        else
            name = "Prefab";
    }
    prefab.SetName(name);

    std::unordered_map<uint32, int> entityToPrefabIndex;
    BuildPrefabIndexMap(world, root, entityToPrefabIndex);

    // Recursively collect entity hierarchy
    CollectEntityHierarchy(world, root, prefab, -1, entityToPrefabIndex);

    DOT_LOG_INFO("Created prefab '%s' with %zu entities", name.c_str(), prefab.GetEntityCount());

    return prefab;
}

void PrefabSystem::CollectEntityHierarchy(World& world, Entity entity, Prefab& prefab, int parentIndex,
                                          const std::unordered_map<uint32, int>& entityToPrefabIndex)
{
    PrefabEntity prefabEntity;

    // Get name
    NameComponent* nameComp = world.GetComponent<NameComponent>(entity);
    prefabEntity.name = nameComp ? nameComp->name : "Entity";
    prefabEntity.parentIndex = parentIndex;
    prefabEntity.sourceIndex = static_cast<int>(entity.GetIndex());

    // Serialize components
    SerializeEntityComponents(world, entity, prefabEntity, entityToPrefabIndex);

    // Add to prefab
    int myIndex = static_cast<int>(prefab.GetEntityCount());
    prefab.AddEntity(prefabEntity);

    // Process children
    HierarchyComponent* hierarchy = world.GetComponent<HierarchyComponent>(entity);
    if (hierarchy)
    {
        for (Entity child : hierarchy->children)
        {
            if (child.IsValid())
            {
                CollectEntityHierarchy(world, child, prefab, myIndex, entityToPrefabIndex);
            }
        }
    }
}

void PrefabSystem::SerializeEntityComponents(World& world, Entity entity, PrefabEntity& prefabEntity,
                                             const std::unordered_map<uint32, int>& entityToPrefabIndex)
{
    // Serialize TransformComponent manually
    if (TransformComponent* transform = world.GetComponent<TransformComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"position\": [" << transform->position.x << ", " << transform->position.y << ", "
             << transform->position.z << "], ";
        json << "\"rotation\": [" << transform->rotation.x << ", " << transform->rotation.y << ", "
             << transform->rotation.z << "], ";
        json << "\"scale\": [" << transform->scale.x << ", " << transform->scale.y << ", " << transform->scale.z
             << "]}";
        prefabEntity.components["TransformComponent"] = json.str();
    }

    // Serialize PrimitiveComponent
    if (PrimitiveComponent* prim = world.GetComponent<PrimitiveComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"type\": " << static_cast<int>(prim->type) << ", ";
        json << "\"overrideLodThresholds\": " << (prim->overrideLodThresholds ? "true" : "false") << ", ";
        json << "\"lod1ScreenHeight\": " << prim->lod1ScreenHeight << ", ";
        json << "\"lod2ScreenHeight\": " << prim->lod2ScreenHeight << "}";
        prefabEntity.components["PrimitiveComponent"] = json.str();
    }

    // Serialize DirectionalLightComponent
    if (DirectionalLightComponent* light = world.GetComponent<DirectionalLightComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"color\": [" << light->color.x << ", " << light->color.y << ", " << light->color.z << "], ";
        json << "\"intensity\": " << light->intensity << ", ";
        json << "\"lightingMode\": " << static_cast<int>(light->lightingMode) << ", ";
        json << "\"castShadows\": " << (light->castShadows ? "true" : "false") << "}";
        prefabEntity.components["DirectionalLightComponent"] = json.str();
    }

    // Serialize PointLightComponent
    if (PointLightComponent* light = world.GetComponent<PointLightComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"color\": [" << light->color.x << ", " << light->color.y << ", " << light->color.z << "], ";
        json << "\"intensity\": " << light->intensity << ", ";
        json << "\"range\": " << light->range << ", ";
        json << "\"lightingMode\": " << static_cast<int>(light->lightingMode) << ", ";
        json << "\"castShadows\": " << (light->castShadows ? "true" : "false") << ", ";
        json << "\"shadowBias\": " << light->shadowBias << "}";
        prefabEntity.components["PointLightComponent"] = json.str();
    }

    // Serialize SpotLightComponent
    if (SpotLightComponent* light = world.GetComponent<SpotLightComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"color\": [" << light->color.x << ", " << light->color.y << ", " << light->color.z << "], ";
        json << "\"intensity\": " << light->intensity << ", ";
        json << "\"range\": " << light->range << ", ";
        json << "\"innerConeAngle\": " << light->innerConeAngle << ", ";
        json << "\"outerConeAngle\": " << light->outerConeAngle << ", ";
        json << "\"lightingMode\": " << static_cast<int>(light->lightingMode) << ", ";
        json << "\"castShadows\": " << (light->castShadows ? "true" : "false") << ", ";
        json << "\"shadowBias\": " << light->shadowBias << "}";
        prefabEntity.components["SpotLightComponent"] = json.str();
    }

    if (ReflectionProbeComponent* probe = world.GetComponent<ReflectionProbeComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"sourceMode\": " << static_cast<int>(probe->sourceMode) << ", ";
        json << "\"cubemapPath\": \"" << probe->cubemapPath << "\", ";
        json << "\"tint\": [" << probe->tint.x << ", " << probe->tint.y << ", " << probe->tint.z << "], ";
        json << "\"intensity\": " << probe->intensity << ", ";
        json << "\"radius\": " << probe->radius << ", ";
        json << "\"boxExtents\": [" << probe->boxExtents.x << ", " << probe->boxExtents.y << ", "
             << probe->boxExtents.z << "], ";
        json << "\"falloff\": " << probe->falloff << ", ";
        json << "\"enabled\": " << (probe->enabled ? "true" : "false") << "}";
        prefabEntity.components["ReflectionProbeComponent"] = json.str();
    }

    // Serialize RigidBodyComponent
    if (RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"mass\": " << rb->mass << ", ";
        json << "\"isKinematic\": " << (rb->isKinematic ? "true" : "false") << ", ";
        json << "\"useGravity\": " << (rb->useGravity ? "true" : "false") << "}";
        prefabEntity.components["RigidBodyComponent"] = json.str();
    }

    if (HealthComponent* health = world.GetComponent<HealthComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"currentHealth\": " << health->currentHealth << ", ";
        json << "\"maxHealth\": " << health->maxHealth << ", ";
        json << "\"invulnerable\": " << (health->invulnerable ? "true" : "false") << ", ";
        json << "\"destroyEntityOnDeath\": " << (health->destroyEntityOnDeath ? "true" : "false") << "}";
        prefabEntity.components["HealthComponent"] = json.str();
    }

    // Serialize BoxColliderComponent
    if (BoxColliderComponent* col = world.GetComponent<BoxColliderComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"size\": [" << col->size.x << ", " << col->size.y << ", " << col->size.z << "], ";
        json << "\"center\": [" << col->center.x << ", " << col->center.y << ", " << col->center.z << "], ";
        json << "\"isTrigger\": " << (col->isTrigger ? "true" : "false") << ", ";
        json << "\"collisionLayer\": " << static_cast<int>(col->collisionLayer) << ", ";
        json << "\"collisionMask\": " << col->collisionMask << "}";
        prefabEntity.components["BoxColliderComponent"] = json.str();
    }

    // Serialize SphereColliderComponent
    if (SphereColliderComponent* col = world.GetComponent<SphereColliderComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"radius\": " << col->radius << ", ";
        json << "\"center\": [" << col->center.x << ", " << col->center.y << ", " << col->center.z << "], ";
        json << "\"isTrigger\": " << (col->isTrigger ? "true" : "false") << ", ";
        json << "\"collisionLayer\": " << static_cast<int>(col->collisionLayer) << ", ";
        json << "\"collisionMask\": " << col->collisionMask << "}";
        prefabEntity.components["SphereColliderComponent"] = json.str();
    }

    // Serialize ScriptComponent
    if (ScriptComponent* script = world.GetComponent<ScriptComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"scriptPath\": \"" << script->scriptPath << "\"}";
        prefabEntity.components["ScriptComponent"] = json.str();
    }

    // Serialize CameraComponent
    if (CameraComponent* cam = world.GetComponent<CameraComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"fov\": " << cam->fov << ", ";
        json << "\"nearPlane\": " << cam->nearPlane << ", ";
        json << "\"farPlane\": " << cam->farPlane << ", ";
        json << "\"isActive\": " << (cam->isActive ? "true" : "false") << ", ";
        json << "\"renderMask\": " << cam->renderMask << ", ";
        json << "\"enableViewmodelPass\": " << (cam->enableViewmodelPass ? "true" : "false") << ", ";
        json << "\"viewmodelMask\": " << cam->viewmodelMask << ", ";
        json << "\"viewmodelFov\": " << cam->viewmodelFov << ", ";
        json << "\"viewmodelNearPlane\": " << cam->viewmodelNearPlane << "}";
        prefabEntity.components["CameraComponent"] = json.str();
    }

    if (AttachmentBindingComponent* binding = world.GetComponent<AttachmentBindingComponent>(entity))
    {
        std::ostringstream json;
        int targetPrefabIndex = -1;
        if (binding->targetMode == AttachmentTargetMode::Entity && binding->targetEntity.IsValid())
        {
            auto targetIt = entityToPrefabIndex.find(binding->targetEntity.GetIndex());
            if (targetIt != entityToPrefabIndex.end())
                targetPrefabIndex = targetIt->second;
        }
        json << "{\"enabled\": " << (binding->enabled ? "true" : "false") << ", ";
        json << "\"targetMode\": " << static_cast<uint32>(binding->targetMode) << ", ";
        json << "\"targetEntity\": " << targetPrefabIndex << ", ";
        json << "\"socketName\": \"" << binding->socketName << "\", ";
        json << "\"followPosition\": " << (binding->followPosition ? "true" : "false") << ", ";
        json << "\"followRotation\": " << (binding->followRotation ? "true" : "false") << ", ";
        json << "\"followScale\": " << (binding->followScale ? "true" : "false") << ", ";
        json << "\"positionAxes\": " << static_cast<uint32>(binding->positionAxes) << ", ";
        json << "\"rotationAxes\": " << static_cast<uint32>(binding->rotationAxes) << ", ";
        json << "\"scaleAxes\": " << static_cast<uint32>(binding->scaleAxes) << "}";
        prefabEntity.components["AttachmentBindingComponent"] = json.str();
    }

    if (AttachmentPointComponent* point = world.GetComponent<AttachmentPointComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"socketName\": \"" << point->socketName << "\"}";
        prefabEntity.components["AttachmentPointComponent"] = json.str();
    }

    if (RenderLayerComponent* renderLayer = world.GetComponent<RenderLayerComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"mask\": " << renderLayer->mask << "}";
        prefabEntity.components["RenderLayerComponent"] = json.str();
    }

    // Serialize CharacterControllerComponent
    if (CharacterControllerComponent* cc = world.GetComponent<CharacterControllerComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"moveSpeed\": " << cc->moveSpeed << ", ";
        json << "\"sprintMultiplier\": " << cc->sprintMultiplier << ", ";
        json << "\"airControl\": " << cc->airControl << ", ";
        json << "\"useGravity\": " << (cc->useGravity ? "true" : "false") << ", ";
        json << "\"gravityMultiplier\": " << cc->gravityMultiplier << ", ";
        json << "\"jumpHeight\": " << cc->jumpHeight << ", ";
        json << "\"maxJumps\": " << cc->maxJumps << ", ";
        json << "\"slopeLimit\": " << cc->slopeLimit << ", ";
        json << "\"stepHeight\": " << cc->stepHeight << ", ";
        json << "\"collisionLayer\": " << static_cast<int>(cc->collisionLayer) << ", ";
        json << "\"collisionMask\": " << cc->collisionMask << "}";
        prefabEntity.components["CharacterControllerComponent"] = json.str();
    }

    if (NavAgentComponent* navAgent = world.GetComponent<NavAgentComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"moveSpeed\": " << navAgent->moveSpeed << ", ";
        json << "\"stoppingDistance\": " << navAgent->stoppingDistance << ", ";
        json << "\"projectionExtent\": [" << navAgent->projectionExtent.x << ", " << navAgent->projectionExtent.y << ", "
             << navAgent->projectionExtent.z << "]}";
        prefabEntity.components["NavAgentComponent"] = json.str();
    }

    // Serialize PlayerInputComponent
    if (PlayerInputComponent* input = world.GetComponent<PlayerInputComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"keyForward\": " << input->keyForward << ", ";
        json << "\"keyBackward\": " << input->keyBackward << ", ";
        json << "\"keyLeft\": " << input->keyLeft << ", ";
        json << "\"keyRight\": " << input->keyRight << ", ";
        json << "\"keyJump\": " << input->keyJump << ", ";
        json << "\"keySprint\": " << input->keySprint << ", ";
        json << "\"enableMouseLook\": " << (input->enableMouseLook ? "true" : "false") << ", ";
        json << "\"mouseSensitivity\": " << input->mouseSensitivity << ", ";
        json << "\"invertY\": " << (input->invertY ? "true" : "false") << "}";
        prefabEntity.components["PlayerInputComponent"] = json.str();
    }

    // Serialize MaterialComponent
    if (MaterialComponent* mat = world.GetComponent<MaterialComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"materialPath\": \"" << mat->materialPath << "\", ";
        json << "\"baseColor\": [" << mat->baseColor.x << ", " << mat->baseColor.y << ", " << mat->baseColor.z << "], ";
        json << "\"emissiveColor\": [" << mat->emissiveColor.x << ", " << mat->emissiveColor.y << ", "
             << mat->emissiveColor.z << "], ";
        json << "\"metallic\": " << mat->metallic << ", ";
        json << "\"roughness\": " << mat->roughness << ", ";
        json << "\"emissiveStrength\": " << mat->emissiveStrength << ", ";
        json << "\"useMaterialFile\": " << (mat->useMaterialFile ? "true" : "false") << "}";
        prefabEntity.components["MaterialComponent"] = json.str();
    }

    // Serialize MeshComponent
    if (MeshComponent* mesh = world.GetComponent<MeshComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"meshPath\": \"" << mesh->meshPath << "\", ";
        json << "\"submeshIndex\": " << mesh->submeshIndex << ", ";
        json << "\"castShadow\": " << (mesh->castShadow ? "true" : "false") << "}";
        prefabEntity.components["MeshComponent"] = json.str();
    }

    if (StaticLightingComponent* staticLighting = world.GetComponent<StaticLightingComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"participateInBake\": " << (staticLighting->participateInBake ? "true" : "false") << ", ";
        json << "\"receiveBakedLighting\": " << (staticLighting->receiveBakedLighting ? "true" : "false") << ", ";
        json << "\"castBakedShadows\": " << (staticLighting->castBakedShadows ? "true" : "false") << ", ";
        json << "\"resolutionScale\": " << staticLighting->resolutionScale << ", ";
        json << "\"bakeValid\": " << (staticLighting->bakeValid ? "true" : "false") << ", ";
        json << "\"bakeStale\": " << (staticLighting->bakeStale ? "true" : "false") << ", ";
        json << "\"useBakedLighting\": " << (staticLighting->useBakedLighting ? "true" : "false") << ", ";
        json << "\"lightmapIntensity\": " << staticLighting->lightmapIntensity << ", ";
        json << "\"lightmapTexturePath\": \"" << staticLighting->lightmapTexturePath << "\", ";
        json << "\"lightmapSidecarPath\": \"" << staticLighting->lightmapSidecarPath << "\", ";
        json << "\"bakeSignature\": \"" << staticLighting->bakeSignature << "\", ";
        json << "\"lightmapScaleU\": " << staticLighting->lightmapScaleU << ", ";
        json << "\"lightmapScaleV\": " << staticLighting->lightmapScaleV << ", ";
        json << "\"lightmapOffsetU\": " << staticLighting->lightmapOffsetU << ", ";
        json << "\"lightmapOffsetV\": " << staticLighting->lightmapOffsetV << "}";
        prefabEntity.components["StaticLightingComponent"] = json.str();
    }

    // Serialize SkyboxComponent
    if (SkyboxComponent* sky = world.GetComponent<SkyboxComponent>(entity))
    {
        std::ostringstream json;
        json << "{\"cubemapPath\": \"" << sky->cubemapPath << "\", ";
        json << "\"tintR\": " << sky->tintR << ", ";
        json << "\"tintG\": " << sky->tintG << ", ";
        json << "\"tintB\": " << sky->tintB << ", ";
        json << "\"ambientEnabled\": " << (sky->ambientEnabled ? 1 : 0) << ", ";
        json << "\"ambientColorR\": " << sky->ambientColorR << ", ";
        json << "\"ambientColorG\": " << sky->ambientColorG << ", ";
        json << "\"ambientColorB\": " << sky->ambientColorB << ", ";
        json << "\"ambientIntensity\": " << sky->ambientIntensity << ", ";
        json << "\"rotation\": " << sky->rotation << "}";
        prefabEntity.components["SkyboxComponent"] = json.str();
    }

    // Note: We intentionally skip NameComponent as it's handled separately
    // Note: We intentionally skip HierarchyComponent as it's rebuilt on instantiate
    // Note: We intentionally skip PrefabComponent to avoid nesting issues
}

Entity PrefabSystem::Instantiate(World& world, const Prefab& prefab, const Vec3& position, Entity parent)
{
    if (!prefab.IsValid())
    {
        DOT_LOG_ERROR("Cannot instantiate invalid prefab");
        return kNullEntity;
    }

    uint32 instanceId = GenerateInstanceId();

    // Map from prefab entity index to created entity
    std::vector<Entity> createdEntities;
    createdEntities.reserve(prefab.GetEntityCount());

    // Create all entities first
    for (size_t i = 0; i < prefab.GetEntityCount(); i++)
    {
        Entity entity = world.CreateEntity();
        createdEntities.push_back(entity);

        const PrefabEntity& prefabEntity = prefab.GetEntities()[i];

        // Add name component
        auto& nameComp = world.AddComponent<NameComponent>(entity);
        nameComp.name = prefabEntity.name;

        // Add transform (will be filled in later)
        world.AddComponent<TransformComponent>(entity);

        // Add hierarchy component
        world.AddComponent<HierarchyComponent>(entity);

        // Add prefab component to track instance
        auto& prefabComp = world.AddComponent<PrefabComponent>(entity);
        prefabComp.prefabPath = prefab.GetSourcePath();
        prefabComp.isRootInstance = (i == 0);
        prefabComp.entityIndex = static_cast<int>(i);
        prefabComp.instanceId = instanceId;
    }

    // Set up hierarchy and apply component data
    for (size_t i = 0; i < prefab.GetEntityCount(); i++)
    {
        Entity entity = createdEntities[i];
        const PrefabEntity& prefabEntity = prefab.GetEntities()[i];

        // Set up parent-child relationship
        HierarchyComponent* hierarchy = world.GetComponent<HierarchyComponent>(entity);
        if (hierarchy)
        {
            if (prefabEntity.parentIndex >= 0)
            {
                // Link to parent within prefab
                Entity parentEntity = createdEntities[prefabEntity.parentIndex];
                hierarchy->parent = parentEntity;

                HierarchyComponent* parentHierarchy = world.GetComponent<HierarchyComponent>(parentEntity);
                if (parentHierarchy)
                {
                    parentHierarchy->children.push_back(entity);
                }
            }
            else if (parent.IsValid())
            {
                // Link to external parent
                hierarchy->parent = parent;

                HierarchyComponent* parentHierarchy = world.GetComponent<HierarchyComponent>(parent);
                if (parentHierarchy)
                {
                    parentHierarchy->children.push_back(entity);
                }
            }
        }

        // Deserialize components
        DeserializeEntityComponents(world, entity, prefabEntity, createdEntities);

        // Apply position offset to root
        if (i == 0)
        {
            TransformComponent* transform = world.GetComponent<TransformComponent>(entity);
            if (transform)
            {
                transform->position.x += position.x;
                transform->position.y += position.y;
                transform->position.z += position.z;
            }
        }
    }

    DOT_LOG_INFO("Instantiated prefab '%s' with %zu entities (instance %u)", prefab.GetName().c_str(),
                 createdEntities.size(), instanceId);

    return createdEntities.empty() ? kNullEntity : createdEntities[0];
}

void PrefabSystem::DeserializeEntityComponents(World& world, Entity entity, const PrefabEntity& prefabEntity,
                                               const std::vector<Entity>& createdEntities)
{
    for (const auto& [typeName, jsonData] : prefabEntity.components)
    {
        if (typeName == "TransformComponent")
        {
            TransformComponent* transform = world.GetComponent<TransformComponent>(entity);
            if (transform)
            {
                // Simple parsing for Vec3 properties
                // Format: {"position": [x, y, z], "rotation": [x, y, z], "scale": [x, y, z]}
                auto parseVec3 = [&jsonData](const std::string& key) -> Vec3
                {
                    Vec3 result(0, 0, 0);
                    size_t pos = jsonData.find("\"" + key + "\"");
                    if (pos == std::string::npos)
                        return result;

                    pos = jsonData.find('[', pos);
                    if (pos == std::string::npos)
                        return result;
                    pos++;

                    char* end;
                    result.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = end - jsonData.c_str();

                    pos = jsonData.find(',', pos);
                    if (pos == std::string::npos)
                        return result;
                    pos++;

                    result.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = end - jsonData.c_str();

                    pos = jsonData.find(',', pos);
                    if (pos == std::string::npos)
                        return result;
                    pos++;

                    result.z = std::strtof(jsonData.c_str() + pos, &end);
                    return result;
                };

                transform->position = parseVec3("position");
                transform->rotation = parseVec3("rotation");
                transform->scale = parseVec3("scale");
            }
        }
        else if (typeName == "PrimitiveComponent")
        {
            auto& prim = world.AddComponent<PrimitiveComponent>(entity);

            size_t pos = jsonData.find("\"type\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find(':', pos);
                if (pos != std::string::npos)
                {
                    pos++;
                    while (pos < jsonData.size() && (jsonData[pos] == ' ' || jsonData[pos] == '\t'))
                        pos++;

                    int typeVal = std::atoi(jsonData.c_str() + pos);
                    prim.type = static_cast<PrimitiveType>(typeVal);
                }
            }

            GetPrimitiveDefaultLodScreenHeightThresholds(prim.type, prim.lod1ScreenHeight, prim.lod2ScreenHeight);

            pos = jsonData.find("\"overrideLodThresholds\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find(':', pos);
                if (pos != std::string::npos)
                {
                    pos++;
                    while (pos < jsonData.size() && (jsonData[pos] == ' ' || jsonData[pos] == '\t'))
                        pos++;
                    prim.overrideLodThresholds = jsonData.compare(pos, 4, "true") == 0;
                }
            }

            pos = jsonData.find("\"lod1ScreenHeight\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find(':', pos);
                if (pos != std::string::npos)
                    prim.lod1ScreenHeight = std::strtof(jsonData.c_str() + pos + 1, nullptr);
            }

            pos = jsonData.find("\"lod2ScreenHeight\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find(':', pos);
                if (pos != std::string::npos)
                    prim.lod2ScreenHeight = std::strtof(jsonData.c_str() + pos + 1, nullptr);
            }

            prim.lod1ScreenHeight = std::max(prim.lod1ScreenHeight, 0.0f);
            prim.lod2ScreenHeight = std::clamp(prim.lod2ScreenHeight, 0.0f, prim.lod1ScreenHeight);
        }
        else if (typeName == "DirectionalLightComponent")
        {
            auto& light = world.AddComponent<DirectionalLightComponent>(entity);
            // Parse color
            size_t pos = jsonData.find("\"color\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end;
                    pos++;
                    light.color.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    light.color.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    light.color.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }
            // Parse intensity
            pos = jsonData.find("\"intensity\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find(':', pos) + 1;
                light.intensity = std::strtof(jsonData.c_str() + pos, nullptr);
            }
            pos = jsonData.find("\"lightingMode\"");
            if (pos != std::string::npos)
                light.lightingMode = static_cast<LightingMode>(std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1));
        }
        else if (typeName == "PointLightComponent")
        {
            auto& light = world.AddComponent<PointLightComponent>(entity);
            size_t pos = jsonData.find("\"color\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end;
                    pos++;
                    light.color.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    light.color.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    light.color.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }
            pos = jsonData.find("\"intensity\"");
            if (pos != std::string::npos)
                light.intensity = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"range\"");
            if (pos != std::string::npos)
                light.range = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightingMode\"");
            if (pos != std::string::npos)
                light.lightingMode = static_cast<LightingMode>(std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1));
            light.castShadows = jsonData.find("\"castShadows\": true") != std::string::npos;
            pos = jsonData.find("\"shadowBias\"");
            if (pos != std::string::npos)
                light.shadowBias = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
        }
        else if (typeName == "SpotLightComponent")
        {
            auto& light = world.AddComponent<SpotLightComponent>(entity);
            size_t pos = jsonData.find("\"intensity\"");
            if (pos != std::string::npos)
                light.intensity = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"range\"");
            if (pos != std::string::npos)
                light.range = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"innerConeAngle\"");
            if (pos != std::string::npos)
                light.innerConeAngle = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"outerConeAngle\"");
            if (pos != std::string::npos)
                light.outerConeAngle = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightingMode\"");
            if (pos != std::string::npos)
                light.lightingMode = static_cast<LightingMode>(std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1));
            light.castShadows = jsonData.find("\"castShadows\": true") != std::string::npos;
            pos = jsonData.find("\"shadowBias\"");
            if (pos != std::string::npos)
                light.shadowBias = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
        }
        else if (typeName == "ReflectionProbeComponent")
        {
            auto& probe = world.AddComponent<ReflectionProbeComponent>(entity);

            size_t pos = jsonData.find("\"sourceMode\"");
            if (pos != std::string::npos)
            {
                const int parsedSourceMode =
                    std::strtol(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10);
                if (parsedSourceMode == static_cast<int>(ReflectionProbeSourceMode::AutoSceneSkybox))
                    probe.sourceMode = ReflectionProbeSourceMode::AutoSceneSkybox;
                else
                    probe.sourceMode = ReflectionProbeSourceMode::ManualCubemap;
            }

            pos = jsonData.find("\"cubemapPath\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find(':', pos);
                if (pos != std::string::npos)
                {
                    size_t start = jsonData.find('"', pos + 1);
                    size_t end = start == std::string::npos ? std::string::npos : jsonData.find('"', start + 1);
                    if (start != std::string::npos && end != std::string::npos)
                        probe.cubemapPath = jsonData.substr(start + 1, end - start - 1);
                }
            }

            pos = jsonData.find("\"tint\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end = nullptr;
                    pos++;
                    probe.tint.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    probe.tint.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    probe.tint.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }

            pos = jsonData.find("\"intensity\"");
            if (pos != std::string::npos)
                probe.intensity = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);

            pos = jsonData.find("\"radius\"");
            if (pos != std::string::npos)
                probe.radius = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);

            pos = jsonData.find("\"boxExtents\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end = nullptr;
                    pos++;
                    probe.boxExtents.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    probe.boxExtents.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    probe.boxExtents.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }

            pos = jsonData.find("\"falloff\"");
            if (pos != std::string::npos)
                probe.falloff = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);

            probe.enabled = jsonData.find("\"enabled\": true") != std::string::npos;
            if (jsonData.find("\"sourceMode\"") == std::string::npos && probe.cubemapPath.empty())
                probe.sourceMode = ReflectionProbeSourceMode::AutoSceneSkybox;
            probe.radius = std::max(probe.radius, 0.0f);
            probe.boxExtents.x = std::max(probe.boxExtents.x, 0.0f);
            probe.boxExtents.y = std::max(probe.boxExtents.y, 0.0f);
            probe.boxExtents.z = std::max(probe.boxExtents.z, 0.0f);
            probe.falloff = std::clamp(probe.falloff, 0.0f, 1.0f);
        }
        else if (typeName == "RigidBodyComponent")
        {
            auto& rb = world.AddComponent<RigidBodyComponent>(entity);
            size_t pos = jsonData.find("\"mass\"");
            if (pos != std::string::npos)
                rb.mass = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            rb.isKinematic = jsonData.find("\"isKinematic\": true") != std::string::npos;
            rb.useGravity = jsonData.find("\"useGravity\": true") != std::string::npos;
        }
        else if (typeName == "HealthComponent")
        {
            auto& health = world.AddComponent<HealthComponent>(entity);
            size_t pos = jsonData.find("\"currentHealth\"");
            if (pos != std::string::npos)
                health.currentHealth = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"maxHealth\"");
            if (pos != std::string::npos)
                health.maxHealth = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            health.invulnerable = jsonData.find("\"invulnerable\": true") != std::string::npos;
            health.destroyEntityOnDeath = jsonData.find("\"destroyEntityOnDeath\": true") != std::string::npos;
            health.Clamp();
        }
        else if (typeName == "BoxColliderComponent")
        {
            auto& col = world.AddComponent<BoxColliderComponent>(entity);
            size_t pos = jsonData.find("\"size\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end;
                    pos++;
                    col.size.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    col.size.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    col.size.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }
            pos = jsonData.find("\"center\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end;
                    pos++;
                    col.center.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    col.center.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    col.center.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }
            col.isTrigger = jsonData.find("\"isTrigger\": true") != std::string::npos;
            pos = jsonData.find("\"collisionLayer\"");
            if (pos != std::string::npos)
                col.collisionLayer =
                    static_cast<uint8>(std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1));
            pos = jsonData.find("\"collisionMask\"");
            if (pos != std::string::npos)
                col.collisionMask =
                    static_cast<uint32>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
        }
        else if (typeName == "SphereColliderComponent")
        {
            auto& col = world.AddComponent<SphereColliderComponent>(entity);
            size_t pos = jsonData.find("\"radius\"");
            if (pos != std::string::npos)
                col.radius = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"center\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    char* end;
                    pos++;
                    col.center.x = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    col.center.y = std::strtof(jsonData.c_str() + pos, &end);
                    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
                    col.center.z = std::strtof(jsonData.c_str() + pos, &end);
                }
            }
            col.isTrigger = jsonData.find("\"isTrigger\": true") != std::string::npos;
            pos = jsonData.find("\"collisionLayer\"");
            if (pos != std::string::npos)
                col.collisionLayer =
                    static_cast<uint8>(std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1));
            pos = jsonData.find("\"collisionMask\"");
            if (pos != std::string::npos)
                col.collisionMask =
                    static_cast<uint32>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
        }
        else if (typeName == "ScriptComponent")
        {
            auto& script = world.AddComponent<ScriptComponent>(entity);
            size_t pos = jsonData.find("\"scriptPath\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('"', jsonData.find(':', pos) + 1);
                if (pos != std::string::npos)
                {
                    pos++;
                    size_t endPos = jsonData.find('"', pos);
                    if (endPos != std::string::npos)
                        script.scriptPath = jsonData.substr(pos, endPos - pos);
                }
            }
        }
        else if (typeName == "CameraComponent")
        {
            auto& cam = world.AddComponent<CameraComponent>(entity);
            size_t pos = jsonData.find("\"fov\"");
            if (pos != std::string::npos)
                cam.fov = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"nearPlane\"");
            if (pos != std::string::npos)
                cam.nearPlane = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"farPlane\"");
            if (pos != std::string::npos)
                cam.farPlane = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            cam.isActive = jsonData.find("\"isActive\": true") != std::string::npos;
            pos = jsonData.find("\"renderMask\"");
            if (pos != std::string::npos)
                cam.renderMask = static_cast<uint32>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
            cam.enableViewmodelPass = jsonData.find("\"enableViewmodelPass\": true") != std::string::npos;
            pos = jsonData.find("\"viewmodelMask\"");
            if (pos != std::string::npos)
                cam.viewmodelMask = static_cast<uint32>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
            pos = jsonData.find("\"viewmodelFov\"");
            if (pos != std::string::npos)
                cam.viewmodelFov = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"viewmodelNearPlane\"");
            if (pos != std::string::npos)
                cam.viewmodelNearPlane = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
        }
        else if (typeName == "AttachmentBindingComponent")
        {
            auto& binding = world.AddComponent<AttachmentBindingComponent>(entity);
            binding.enabled = jsonData.find("\"enabled\": true") != std::string::npos;

            size_t pos = jsonData.find("\"targetMode\"");
            if (pos != std::string::npos)
                binding.targetMode =
                    static_cast<AttachmentTargetMode>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
            pos = jsonData.find("\"targetEntity\"");
            if (pos != std::string::npos)
            {
                const int targetIndex = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
                if (targetIndex >= 0 && targetIndex < static_cast<int>(createdEntities.size()))
                    binding.targetEntity = createdEntities[static_cast<size_t>(targetIndex)];
            }
            pos = jsonData.find("\"socketName\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('"', jsonData.find(':', pos) + 1);
                if (pos != std::string::npos)
                {
                    ++pos;
                    const size_t endPos = jsonData.find('"', pos);
                    if (endPos != std::string::npos)
                        binding.socketName = jsonData.substr(pos, endPos - pos);
                }
            }
            binding.followPosition = jsonData.find("\"followPosition\": true") != std::string::npos;
            binding.followRotation = jsonData.find("\"followRotation\": true") != std::string::npos;
            binding.followScale = jsonData.find("\"followScale\": true") != std::string::npos;
            pos = jsonData.find("\"positionAxes\"");
            if (pos != std::string::npos)
                binding.positionAxes = static_cast<uint8>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
            pos = jsonData.find("\"rotationAxes\"");
            if (pos != std::string::npos)
                binding.rotationAxes = static_cast<uint8>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
            pos = jsonData.find("\"scaleAxes\"");
            if (pos != std::string::npos)
                binding.scaleAxes = static_cast<uint8>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
        }
        else if (typeName == "AttachmentPointComponent")
        {
            auto& point = world.AddComponent<AttachmentPointComponent>(entity);
            size_t pos = jsonData.find("\"socketName\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('"', jsonData.find(':', pos) + 1);
                if (pos != std::string::npos)
                {
                    ++pos;
                    const size_t endPos = jsonData.find('"', pos);
                    if (endPos != std::string::npos)
                        point.socketName = jsonData.substr(pos, endPos - pos);
                }
            }
        }
        else if (typeName == "RenderLayerComponent")
        {
            auto& renderLayer = world.AddComponent<RenderLayerComponent>(entity);
            const size_t pos = jsonData.find("\"mask\"");
            if (pos != std::string::npos)
                renderLayer.mask = static_cast<uint32>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
        }
        else if (typeName == "CharacterControllerComponent")
        {
            auto& cc = world.AddComponent<CharacterControllerComponent>(entity);
            size_t pos = jsonData.find("\"moveSpeed\"");
            if (pos != std::string::npos)
                cc.moveSpeed = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"sprintMultiplier\"");
            if (pos != std::string::npos)
                cc.sprintMultiplier = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"airControl\"");
            if (pos != std::string::npos)
                cc.airControl = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            cc.useGravity = jsonData.find("\"useGravity\": true") != std::string::npos;
            pos = jsonData.find("\"gravityMultiplier\"");
            if (pos != std::string::npos)
                cc.gravityMultiplier = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"jumpHeight\"");
            if (pos != std::string::npos)
                cc.jumpHeight = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"maxJumps\"");
            if (pos != std::string::npos)
                cc.maxJumps = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            pos = jsonData.find("\"slopeLimit\"");
            if (pos != std::string::npos)
                cc.slopeLimit = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"stepHeight\"");
            if (pos != std::string::npos)
                cc.stepHeight = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"collisionLayer\"");
            if (pos != std::string::npos)
                cc.collisionLayer =
                    static_cast<uint8>(std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1));
            pos = jsonData.find("\"collisionMask\"");
            if (pos != std::string::npos)
                cc.collisionMask =
                    static_cast<uint32>(std::strtoul(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10));
        }
        else if (typeName == "NavAgentComponent")
        {
            auto& navAgent = world.AddComponent<NavAgentComponent>(entity);
            size_t pos = jsonData.find("\"moveSpeed\"");
            if (pos != std::string::npos)
                navAgent.moveSpeed = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"stoppingDistance\"");
            if (pos != std::string::npos)
                navAgent.stoppingDistance = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"projectionExtent\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('[', pos);
                if (pos != std::string::npos)
                {
                    navAgent.projectionExtent.x = std::strtof(jsonData.c_str() + pos + 1, nullptr);
                    pos = jsonData.find(',', pos);
                    if (pos != std::string::npos)
                    {
                        navAgent.projectionExtent.y = std::strtof(jsonData.c_str() + pos + 1, nullptr);
                        pos = jsonData.find(',', pos + 1);
                        if (pos != std::string::npos)
                            navAgent.projectionExtent.z = std::strtof(jsonData.c_str() + pos + 1, nullptr);
                    }
                }
            }
        }
        else if (typeName == "PlayerInputComponent")
        {
            auto& input = world.AddComponent<PlayerInputComponent>(entity);
            size_t pos = jsonData.find("\"keyForward\"");
            if (pos != std::string::npos)
                input.keyForward = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            pos = jsonData.find("\"keyBackward\"");
            if (pos != std::string::npos)
                input.keyBackward = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            pos = jsonData.find("\"keyLeft\"");
            if (pos != std::string::npos)
                input.keyLeft = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            pos = jsonData.find("\"keyRight\"");
            if (pos != std::string::npos)
                input.keyRight = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            pos = jsonData.find("\"keyJump\"");
            if (pos != std::string::npos)
                input.keyJump = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            pos = jsonData.find("\"keySprint\"");
            if (pos != std::string::npos)
                input.keySprint = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            input.enableMouseLook = jsonData.find("\"enableMouseLook\": true") != std::string::npos;
            pos = jsonData.find("\"mouseSensitivity\"");
            if (pos != std::string::npos)
                input.mouseSensitivity = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            input.invertY = jsonData.find("\"invertY\": true") != std::string::npos;
        }
        else if (typeName == "MaterialComponent")
        {
            auto& mat = world.AddComponent<MaterialComponent>(entity);
            const auto parseVec3Field = [&jsonData](const char* key, Vec3& outValue)
            {
                const size_t pos = jsonData.find(key);
                if (pos == std::string::npos)
                    return;

                const size_t arrayPos = jsonData.find('[', pos);
                if (arrayPos == std::string::npos)
                    return;

                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (std::sscanf(jsonData.c_str() + arrayPos, "[%f, %f, %f]", &x, &y, &z) == 3)
                    outValue = Vec3(x, y, z);
            };

            size_t pos = jsonData.find("\"materialPath\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('"', jsonData.find(':', pos) + 1);
                if (pos != std::string::npos)
                {
                    pos++;
                    size_t endPos = jsonData.find('"', pos);
                    if (endPos != std::string::npos)
                        mat.materialPath = jsonData.substr(pos, endPos - pos);
                }
            }
            parseVec3Field("\"baseColor\"", mat.baseColor);
            parseVec3Field("\"emissiveColor\"", mat.emissiveColor);
            pos = jsonData.find("\"metallic\"");
            if (pos != std::string::npos)
                mat.metallic = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"roughness\"");
            if (pos != std::string::npos)
                mat.roughness = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"emissiveStrength\"");
            if (pos != std::string::npos)
                mat.emissiveStrength = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            mat.useMaterialFile = jsonData.find("\"useMaterialFile\": true") != std::string::npos;
        }
        else if (typeName == "MeshComponent")
        {
            auto& mesh = world.AddComponent<MeshComponent>(entity);
            size_t pos = jsonData.find("\"meshPath\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('"', jsonData.find(':', pos) + 1);
                if (pos != std::string::npos)
                {
                    pos++;
                    size_t endPos = jsonData.find('"', pos);
                    if (endPos != std::string::npos)
                        mesh.meshPath = jsonData.substr(pos, endPos - pos);
                }
            }
            pos = jsonData.find("\"submeshIndex\"");
            if (pos != std::string::npos)
                mesh.submeshIndex = std::atoi(jsonData.c_str() + jsonData.find(':', pos) + 1);
            mesh.castShadow = jsonData.find("\"castShadow\": true") != std::string::npos;
        }
        else if (typeName == "StaticLightingComponent")
        {
            auto& lighting = world.AddComponent<StaticLightingComponent>(entity);
            lighting.participateInBake = jsonData.find("\"participateInBake\": true") != std::string::npos;
            lighting.receiveBakedLighting = jsonData.find("\"receiveBakedLighting\": true") != std::string::npos;
            lighting.castBakedShadows = jsonData.find("\"castBakedShadows\": true") != std::string::npos;
            lighting.bakeValid = jsonData.find("\"bakeValid\": true") != std::string::npos;
            lighting.bakeStale = jsonData.find("\"bakeStale\": true") != std::string::npos;
            lighting.useBakedLighting = jsonData.find("\"useBakedLighting\": true") != std::string::npos;

            size_t pos = jsonData.find("\"resolutionScale\"");
            if (pos != std::string::npos)
                lighting.resolutionScale = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightmapIntensity\"");
            if (pos != std::string::npos)
                lighting.lightmapIntensity = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightmapScaleU\"");
            if (pos != std::string::npos)
                lighting.lightmapScaleU = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightmapScaleV\"");
            if (pos != std::string::npos)
                lighting.lightmapScaleV = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightmapOffsetU\"");
            if (pos != std::string::npos)
                lighting.lightmapOffsetU = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"lightmapOffsetV\"");
            if (pos != std::string::npos)
                lighting.lightmapOffsetV = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);

            const auto parseStringField = [&jsonData](const char* key) -> std::string
            {
                size_t fieldPos = jsonData.find(key);
                if (fieldPos == std::string::npos)
                    return {};
                fieldPos = jsonData.find('"', jsonData.find(':', fieldPos) + 1);
                if (fieldPos == std::string::npos)
                    return {};
                ++fieldPos;
                const size_t endPos = jsonData.find('"', fieldPos);
                if (endPos == std::string::npos)
                    return {};
                return jsonData.substr(fieldPos, endPos - fieldPos);
            };

            lighting.lightmapTexturePath = parseStringField("\"lightmapTexturePath\"");
            lighting.lightmapSidecarPath = parseStringField("\"lightmapSidecarPath\"");
            lighting.bakeSignature = parseStringField("\"bakeSignature\"");
        }
        else if (typeName == "SkyboxComponent")
        {
            auto& sky = world.AddComponent<SkyboxComponent>(entity);
            size_t pos = jsonData.find("\"cubemapPath\"");
            if (pos != std::string::npos)
            {
                pos = jsonData.find('"', jsonData.find(':', pos) + 1);
                if (pos != std::string::npos)
                {
                    pos++;
                    size_t endPos = jsonData.find('"', pos);
                    if (endPos != std::string::npos)
                        sky.cubemapPath = jsonData.substr(pos, endPos - pos);
                }
            }
            pos = jsonData.find("\"tintR\"");
            if (pos != std::string::npos)
                sky.tintR = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"tintG\"");
            if (pos != std::string::npos)
                sky.tintG = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"tintB\"");
            if (pos != std::string::npos)
                sky.tintB = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"ambientEnabled\"");
            if (pos != std::string::npos)
                sky.ambientEnabled = std::strtol(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr, 10) != 0;
            pos = jsonData.find("\"ambientColorR\"");
            if (pos != std::string::npos)
                sky.ambientColorR = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"ambientColorG\"");
            if (pos != std::string::npos)
                sky.ambientColorG = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"ambientColorB\"");
            if (pos != std::string::npos)
                sky.ambientColorB = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"ambientIntensity\"");
            if (pos != std::string::npos)
                sky.ambientIntensity = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
            pos = jsonData.find("\"rotation\"");
            if (pos != std::string::npos)
                sky.rotation = std::strtof(jsonData.c_str() + jsonData.find(':', pos) + 1, nullptr);
        }
    }
}

Entity PrefabSystem::InstantiateFromFile(World& world, const std::string& path, const Vec3& position, Entity parent)
{
    Prefab prefab;
    if (!prefab.LoadFromFile(path))
    {
        DOT_LOG_ERROR("Failed to load prefab from: %s", path.c_str());
        return kNullEntity;
    }

    return Instantiate(world, prefab, position, parent);
}

} // namespace Dot
