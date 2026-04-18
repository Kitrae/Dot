// =============================================================================
// Dot Engine - Component Reflection Registration
// =============================================================================
// Registers scene components with the reflection system.
// =============================================================================

#include "Core/Gameplay/HealthComponent.h"
#include "Core/Input/PlayerInputComponent.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Reflect/Registry.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/AttachmentResolver.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MapComponent.h"
#include "Core/Scene/MaterialComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/PrefabComponent.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scene/SkyboxComponent.h"
#include "Core/Scene/StaticLightingComponent.h"
#include "Core/UI/UIComponent.h"

namespace Dot
{

/// Register all scene components with the type registry
void RegisterSceneComponents()
{
    static bool s_Registered = false;
    if (s_Registered)
        return;
    s_Registered = true;

    // TransformComponent - position, rotation, scale (core transform data)
    DOT_REFLECT_TYPE(TransformComponent)
        .Property("position", &TransformComponent::position, PropertyType::Vec3)
        .Property("rotation", &TransformComponent::rotation, PropertyType::Vec3)
        .Property("scale", &TransformComponent::scale, PropertyType::Vec3);

    // NameComponent - entity display name
    DOT_REFLECT_TYPE(NameComponent).Property("name", &NameComponent::name, PropertyType::String);

    // HierarchyComponent - parent reference (children are derived, not serialized)
    DOT_REFLECT_TYPE(HierarchyComponent).Property("parent", &HierarchyComponent::parent, PropertyType::Entity);

    // PrimitiveComponent - shape type for rendering
    // Note: Storing as uint8 since PrimitiveType is enum class : uint8
    DOT_REFLECT_TYPE(PrimitiveComponent)
        .Property("type", &PrimitiveComponent::type, PropertyType::UInt32)
        .Property("overrideLodThresholds", &PrimitiveComponent::overrideLodThresholds, PropertyType::Bool)
        .Property("lod1ScreenHeight", &PrimitiveComponent::lod1ScreenHeight, PropertyType::Float)
        .Property("lod2ScreenHeight", &PrimitiveComponent::lod2ScreenHeight, PropertyType::Float);

    // ActiveComponent - enabled state
    DOT_REFLECT_TYPE(ActiveComponent).Property("active", &ActiveComponent::active, PropertyType::Bool);

    DOT_REFLECT_TYPE(AttachmentBindingComponent)
        .Property("enabled", &AttachmentBindingComponent::enabled, PropertyType::Bool)
        .Property("targetMode", &AttachmentBindingComponent::targetMode, PropertyType::UInt32)
        .Property("targetEntity", &AttachmentBindingComponent::targetEntity, PropertyType::Entity)
        .Property("socketName", &AttachmentBindingComponent::socketName, PropertyType::String)
        .Property("followPosition", &AttachmentBindingComponent::followPosition, PropertyType::Bool)
        .Property("followRotation", &AttachmentBindingComponent::followRotation, PropertyType::Bool)
        .Property("followScale", &AttachmentBindingComponent::followScale, PropertyType::Bool)
        .Property("positionAxes", &AttachmentBindingComponent::positionAxes, PropertyType::UInt32)
        .Property("rotationAxes", &AttachmentBindingComponent::rotationAxes, PropertyType::UInt32)
        .Property("scaleAxes", &AttachmentBindingComponent::scaleAxes, PropertyType::UInt32);

    DOT_REFLECT_TYPE(AttachmentPointComponent)
        .Property("socketName", &AttachmentPointComponent::socketName, PropertyType::String);

    DOT_REFLECT_TYPE(RenderLayerComponent).Property("mask", &RenderLayerComponent::mask, PropertyType::UInt32);

    DOT_REFLECT_TYPE(UIComponent)
        .Property("uiAssetPath", &UIComponent::uiAssetPath, PropertyType::String)
        .Property("drawWidth", &UIComponent::drawWidth, PropertyType::Float)
        .Property("drawHeight", &UIComponent::drawHeight, PropertyType::Float)
        .Property("pixelsPerUnit", &UIComponent::pixelsPerUnit, PropertyType::Float)
        .Property("billboard", &UIComponent::billboard, PropertyType::Bool)
        .Property("interactionEnabled", &UIComponent::interactionEnabled, PropertyType::Bool)
        .Property("renderLayer", &UIComponent::renderLayer, PropertyType::UInt32)
        .Property("visible", &UIComponent::visible, PropertyType::Bool)
        .Property("isLoaded", &UIComponent::isLoaded, PropertyType::Bool, PropertyFlags::Transient);

    // =============================================================================
    // Light Components
    // =============================================================================

    // DirectionalLightComponent - sun-like parallel light
    DOT_REFLECT_TYPE(DirectionalLightComponent)
        .Property("color", &DirectionalLightComponent::color, PropertyType::Vec3)
        .Property("intensity", &DirectionalLightComponent::intensity, PropertyType::Float)
        .Property("lightingMode", &DirectionalLightComponent::lightingMode, PropertyType::Int32)
        .Property("castShadows", &DirectionalLightComponent::castShadows, PropertyType::Bool);

    // PointLightComponent - omnidirectional light from a point
    DOT_REFLECT_TYPE(PointLightComponent)
        .Property("color", &PointLightComponent::color, PropertyType::Vec3)
        .Property("intensity", &PointLightComponent::intensity, PropertyType::Float)
        .Property("lightingMode", &PointLightComponent::lightingMode, PropertyType::Int32)
        .Property("range", &PointLightComponent::range, PropertyType::Float)
        .Property("castShadows", &PointLightComponent::castShadows, PropertyType::Bool)
        .Property("shadowBias", &PointLightComponent::shadowBias, PropertyType::Float);

    // SpotLightComponent - cone-shaped light
    DOT_REFLECT_TYPE(SpotLightComponent)
        .Property("color", &SpotLightComponent::color, PropertyType::Vec3)
        .Property("intensity", &SpotLightComponent::intensity, PropertyType::Float)
        .Property("range", &SpotLightComponent::range, PropertyType::Float)
        .Property("innerConeAngle", &SpotLightComponent::innerConeAngle, PropertyType::Float)
        .Property("outerConeAngle", &SpotLightComponent::outerConeAngle, PropertyType::Float)
        .Property("lightingMode", &SpotLightComponent::lightingMode, PropertyType::Int32)
        .Property("castShadows", &SpotLightComponent::castShadows, PropertyType::Bool)
        .Property("shadowBias", &SpotLightComponent::shadowBias, PropertyType::Float);

    // AmbientLightComponent - global fill light
    DOT_REFLECT_TYPE(AmbientLightComponent)
        .Property("color", &AmbientLightComponent::color, PropertyType::Vec3)
        .Property("intensity", &AmbientLightComponent::intensity, PropertyType::Float);

    // ReflectionProbeComponent - local cubemap environment reflection
    DOT_REFLECT_TYPE(ReflectionProbeComponent)
        .Property("sourceMode", &ReflectionProbeComponent::sourceMode, PropertyType::Int32)
        .Property("cubemapPath", &ReflectionProbeComponent::cubemapPath, PropertyType::String)
        .Property("tint", &ReflectionProbeComponent::tint, PropertyType::Vec3)
        .Property("intensity", &ReflectionProbeComponent::intensity, PropertyType::Float)
        .Property("radius", &ReflectionProbeComponent::radius, PropertyType::Float)
        .Property("boxExtents", &ReflectionProbeComponent::boxExtents, PropertyType::Vec3)
        .Property("falloff", &ReflectionProbeComponent::falloff, PropertyType::Float)
        .Property("enabled", &ReflectionProbeComponent::enabled, PropertyType::Bool);

    // =============================================================================
    // Mesh Components
    // =============================================================================

    // MeshComponent - external mesh file reference
    DOT_REFLECT_TYPE(MeshComponent)
        .Property("meshPath", &MeshComponent::meshPath, PropertyType::String)
        .Property("isLoaded", &MeshComponent::isLoaded, PropertyType::Bool);

    // MaterialComponent - surface material properties
    DOT_REFLECT_TYPE(MaterialComponent)
        .Property("materialPath", &MaterialComponent::materialPath, PropertyType::String)
        .Property("useMaterialFile", &MaterialComponent::useMaterialFile, PropertyType::Bool)
        .Property("baseColor", &MaterialComponent::baseColor, PropertyType::Vec3)
        .Property("metallic", &MaterialComponent::metallic, PropertyType::Float)
        .Property("roughness", &MaterialComponent::roughness, PropertyType::Float)
        .Property("emissiveColor", &MaterialComponent::emissiveColor, PropertyType::Vec3)
        .Property("emissiveStrength", &MaterialComponent::emissiveStrength, PropertyType::Float);

    DOT_REFLECT_TYPE(UIComponent)
        .Property("assetPath", &UIComponent::assetPath, PropertyType::String)
        .Property("drawSize", &UIComponent::drawSize, PropertyType::Vec2)
        .Property("pixelsPerUnit", &UIComponent::pixelsPerUnit, PropertyType::Float)
        .Property("billboard", &UIComponent::billboard, PropertyType::Bool)
        .Property("renderLayer", &UIComponent::renderLayer, PropertyType::UInt32)
        .Property("interactionEnabled", &UIComponent::interactionEnabled, PropertyType::Bool)
        .Property("visible", &UIComponent::visible, PropertyType::Bool);

    DOT_REFLECT_TYPE(StaticLightingComponent)
        .Property("participateInBake", &StaticLightingComponent::participateInBake, PropertyType::Bool)
        .Property("receiveBakedLighting", &StaticLightingComponent::receiveBakedLighting, PropertyType::Bool)
        .Property("castBakedShadows", &StaticLightingComponent::castBakedShadows, PropertyType::Bool)
        .Property("resolutionScale", &StaticLightingComponent::resolutionScale, PropertyType::Float)
        .Property("bakeValid", &StaticLightingComponent::bakeValid, PropertyType::Bool)
        .Property("bakeStale", &StaticLightingComponent::bakeStale, PropertyType::Bool)
        .Property("useBakedLighting", &StaticLightingComponent::useBakedLighting, PropertyType::Bool)
        .Property("lightmapIntensity", &StaticLightingComponent::lightmapIntensity, PropertyType::Float)
        .Property("lightmapTexturePath", &StaticLightingComponent::lightmapTexturePath, PropertyType::String)
        .Property("lightmapSidecarPath", &StaticLightingComponent::lightmapSidecarPath, PropertyType::String)
        .Property("bakeSignature", &StaticLightingComponent::bakeSignature, PropertyType::String)
        .Property("lightmapScaleU", &StaticLightingComponent::lightmapScaleU, PropertyType::Float)
        .Property("lightmapScaleV", &StaticLightingComponent::lightmapScaleV, PropertyType::Float)
        .Property("lightmapOffsetU", &StaticLightingComponent::lightmapOffsetU, PropertyType::Float)
        .Property("lightmapOffsetV", &StaticLightingComponent::lightmapOffsetV, PropertyType::Float);

    // SkyboxComponent - environment skybox
    DOT_REFLECT_TYPE(SkyboxComponent)
        .Property("cubemapPath", &SkyboxComponent::cubemapPath, PropertyType::String)
        .Property("tintR", &SkyboxComponent::tintR, PropertyType::Float)
        .Property("tintG", &SkyboxComponent::tintG, PropertyType::Float)
        .Property("tintB", &SkyboxComponent::tintB, PropertyType::Float)
        .Property("wrapMode", &SkyboxComponent::wrapMode, PropertyType::Int32)
        .Property("rotation", &SkyboxComponent::rotation, PropertyType::Float)
        .Property("showMarkers", &SkyboxComponent::showMarkers, PropertyType::Bool)
        .Property("ambientEnabled", &SkyboxComponent::ambientEnabled, PropertyType::Bool)
        .Property("ambientColorR", &SkyboxComponent::ambientColorR, PropertyType::Float)
        .Property("ambientColorG", &SkyboxComponent::ambientColorG, PropertyType::Float)
        .Property("ambientColorB", &SkyboxComponent::ambientColorB, PropertyType::Float)
        .Property("ambientIntensity", &SkyboxComponent::ambientIntensity, PropertyType::Float)
        .Property("isLoaded", &SkyboxComponent::isLoaded, PropertyType::Bool, PropertyFlags::Transient);

    // CameraComponent - scene camera for play mode
    DOT_REFLECT_TYPE(CameraComponent)
        .Property("fov", &CameraComponent::fov, PropertyType::Float)
        .Property("nearPlane", &CameraComponent::nearPlane, PropertyType::Float)
        .Property("farPlane", &CameraComponent::farPlane, PropertyType::Float)
        .Property("isActive", &CameraComponent::isActive, PropertyType::Bool)
        .Property("renderMask", &CameraComponent::renderMask, PropertyType::UInt32)
        .Property("enableViewmodelPass", &CameraComponent::enableViewmodelPass, PropertyType::Bool)
        .Property("viewmodelMask", &CameraComponent::viewmodelMask, PropertyType::UInt32)
        .Property("viewmodelFov", &CameraComponent::viewmodelFov, PropertyType::Float)
        .Property("viewmodelNearPlane", &CameraComponent::viewmodelNearPlane, PropertyType::Float);

    // =============================================================================
    // Physics Components
    // =============================================================================

    // RigidBodyComponent - physics simulation properties
    DOT_REFLECT_TYPE(RigidBodyComponent)
        .Property("mass", &RigidBodyComponent::mass, PropertyType::Float)
        .Property("drag", &RigidBodyComponent::drag, PropertyType::Float)
        .Property("angularDrag", &RigidBodyComponent::angularDrag, PropertyType::Float)
        .Property("useGravity", &RigidBodyComponent::useGravity, PropertyType::Bool)
        .Property("isKinematic", &RigidBodyComponent::isKinematic, PropertyType::Bool)
        .Property("friction", &RigidBodyComponent::friction, PropertyType::Float)
        .Property("bounciness", &RigidBodyComponent::bounciness, PropertyType::Float)
        .Property("freezeRotation", &RigidBodyComponent::freezeRotation, PropertyType::Bool);

    // BoxColliderComponent - box collision shape
    DOT_REFLECT_TYPE(BoxColliderComponent)
        .Property("size", &BoxColliderComponent::size, PropertyType::Vec3)
        .Property("center", &BoxColliderComponent::center, PropertyType::Vec3)
        .Property("isTrigger", &BoxColliderComponent::isTrigger, PropertyType::Bool);

    // SphereColliderComponent - sphere collision shape
    DOT_REFLECT_TYPE(SphereColliderComponent)
        .Property("radius", &SphereColliderComponent::radius, PropertyType::Float)
        .Property("center", &SphereColliderComponent::center, PropertyType::Vec3)
        .Property("isTrigger", &SphereColliderComponent::isTrigger, PropertyType::Bool);

    // =============================================================================
    // Character Controller Components
    // =============================================================================

    // CharacterControllerComponent - kinematic character movement
    DOT_REFLECT_TYPE(CharacterControllerComponent)
        // Movement
        .Property("moveSpeed", &CharacterControllerComponent::moveSpeed, PropertyType::Float)
        .Property("sprintMultiplier", &CharacterControllerComponent::sprintMultiplier, PropertyType::Float)
        .Property("airControl", &CharacterControllerComponent::airControl, PropertyType::Float)
        .Property("acceleration", &CharacterControllerComponent::acceleration, PropertyType::Float)
        .Property("deceleration", &CharacterControllerComponent::deceleration, PropertyType::Float)
        .Property("airAcceleration", &CharacterControllerComponent::airAcceleration, PropertyType::Float)
        // Gravity & Jumping
        .Property("useGravity", &CharacterControllerComponent::useGravity, PropertyType::Bool)
        .Property("gravityMultiplier", &CharacterControllerComponent::gravityMultiplier, PropertyType::Float)
        .Property("jumpHeight", &CharacterControllerComponent::jumpHeight, PropertyType::Float)
        .Property("maxJumps", &CharacterControllerComponent::maxJumps, PropertyType::Int32)
        .Property("coyoteTime", &CharacterControllerComponent::coyoteTime, PropertyType::Float)
        .Property("jumpBufferTime", &CharacterControllerComponent::jumpBufferTime, PropertyType::Float)
        // Ground Detection
        .Property("groundCheckDistance", &CharacterControllerComponent::groundCheckDistance, PropertyType::Float)
        .Property("groundCheckRadius", &CharacterControllerComponent::groundCheckRadius, PropertyType::Float)
        // Slopes
        .Property("slopeLimit", &CharacterControllerComponent::slopeLimit, PropertyType::Float)
        .Property("slideOnSteepSlopes", &CharacterControllerComponent::slideOnSteepSlopes, PropertyType::Bool)
        .Property("slideSpeed", &CharacterControllerComponent::slideSpeed, PropertyType::Float)
        .Property("maintainVelocityOnSlopes", &CharacterControllerComponent::maintainVelocityOnSlopes,
                  PropertyType::Bool)
        // Stepping
        .Property("enableStepping", &CharacterControllerComponent::enableStepping, PropertyType::Bool)
        .Property("stepHeight", &CharacterControllerComponent::stepHeight, PropertyType::Float)
        .Property("stepSmoothing", &CharacterControllerComponent::stepSmoothing, PropertyType::Bool)
        // Physics Interaction
        .Property("pushRigidbodies", &CharacterControllerComponent::pushRigidbodies, PropertyType::Bool)
        .Property("pushForce", &CharacterControllerComponent::pushForce, PropertyType::Float)
        .Property("canBePushed", &CharacterControllerComponent::canBePushed, PropertyType::Bool)
        // Collision
        .Property("skinWidth", &CharacterControllerComponent::skinWidth, PropertyType::Float)
        .Property("slideAlongWalls", &CharacterControllerComponent::slideAlongWalls, PropertyType::Bool)
        .Property("maxSlideIterations", &CharacterControllerComponent::maxSlideIterations, PropertyType::Int32);

    // NavAgentComponent - default navigation movement tuning
    DOT_REFLECT_TYPE(NavAgentComponent)
        .Property("moveSpeed", &NavAgentComponent::moveSpeed, PropertyType::Float)
        .Property("stoppingDistance", &NavAgentComponent::stoppingDistance, PropertyType::Float)
        .Property("projectionExtent", &NavAgentComponent::projectionExtent, PropertyType::Vec3);

    // =============================================================================
    // Input Components
    // =============================================================================

    // PlayerInputComponent - configurable WASD input
    DOT_REFLECT_TYPE(PlayerInputComponent)
        // Movement Keys
        .Property("keyForward", &PlayerInputComponent::keyForward, PropertyType::Int32)
        .Property("keyBackward", &PlayerInputComponent::keyBackward, PropertyType::Int32)
        .Property("keyLeft", &PlayerInputComponent::keyLeft, PropertyType::Int32)
        .Property("keyRight", &PlayerInputComponent::keyRight, PropertyType::Int32)
        .Property("keyJump", &PlayerInputComponent::keyJump, PropertyType::Int32)
        .Property("keySprint", &PlayerInputComponent::keySprint, PropertyType::Int32)
        // Mouse Look
        .Property("enableMouseLook", &PlayerInputComponent::enableMouseLook, PropertyType::Bool)
        .Property("mouseSensitivity", &PlayerInputComponent::mouseSensitivity, PropertyType::Float)
        .Property("invertY", &PlayerInputComponent::invertY, PropertyType::Bool)
        // Controller
        .Property("useController", &PlayerInputComponent::useController, PropertyType::Bool)
        .Property("controllerIndex", &PlayerInputComponent::controllerIndex, PropertyType::Int32)
        .Property("deadzone", &PlayerInputComponent::deadzone, PropertyType::Float)
        // Pitch limits
        .Property("pitchMin", &PlayerInputComponent::pitchMin, PropertyType::Float)
        .Property("pitchMax", &PlayerInputComponent::pitchMax, PropertyType::Float);

    // =============================================================================
    // Script Components
    // =============================================================================

    // ScriptComponent - Lua script attachment
    DOT_REFLECT_TYPE(ScriptComponent)
        .Property("scriptPath", &ScriptComponent::scriptPath, PropertyType::String)
        .Property("enabled", &ScriptComponent::enabled, PropertyType::Bool)
        // Mark internal state as transient (not serialized)
        .Property("scriptRef", &ScriptComponent::scriptRef, PropertyType::Int32, PropertyFlags::Transient)
        .Property("hasStarted", &ScriptComponent::hasStarted, PropertyType::Bool, PropertyFlags::Transient);

    // HealthComponent - shared gameplay health state
    DOT_REFLECT_TYPE(HealthComponent)
        .Property("currentHealth", &HealthComponent::currentHealth, PropertyType::Float)
        .Property("maxHealth", &HealthComponent::maxHealth, PropertyType::Float)
        .Property("invulnerable", &HealthComponent::invulnerable, PropertyType::Bool)
        .Property("destroyEntityOnDeath", &HealthComponent::destroyEntityOnDeath, PropertyType::Bool);

    // =============================================================================
    // Prefab Components
    // =============================================================================

    // PrefabComponent - marks entity as prefab instance
    DOT_REFLECT_TYPE(PrefabComponent)
        .Property("prefabPath", &PrefabComponent::prefabPath, PropertyType::String)
        .Property("isRootInstance", &PrefabComponent::isRootInstance, PropertyType::Bool)
        .Property("entityIndex", &PrefabComponent::entityIndex, PropertyType::Int32)
        .Property("instanceId", &PrefabComponent::instanceId, PropertyType::UInt32);

    // MapComponent - scene-linked authored map asset
    DOT_REFLECT_TYPE(MapComponent)
        .Property("mapPath", &MapComponent::mapPath, PropertyType::String)
        .Property("visible", &MapComponent::visible, PropertyType::Bool)
        .Property("collisionEnabled", &MapComponent::collisionEnabled, PropertyType::Bool);
}

} // namespace Dot
