// =============================================================================
// Dot Engine - Scene Unit Tests
// =============================================================================

#include "Core/Scene/Scene.h"
#include "Core/ECS/World.h"
#include "Core/Gameplay/HealthComponent.h"
#include "Core/Reflect/Registry.h"
#include "Core/Scene/ComponentReflection.h"
#include "Core/Scene/AttachmentResolver.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MaterialComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/PrefabSystem.h"
#include "Core/Scene/StaticLightingComponent.h"
#include "Core/Scripting/EntityProxy.h"
#include "Core/Scripting/ScriptSystem.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scene/Components.h"
#include "../../Editor/Source/Rendering/Camera.h"
#include "../../Editor/Source/Rendering/FrustumCulling.h"
#include "../../Editor/Source/Scene/SceneSerializer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace Dot;

// =============================================================================
// Scene Tests
// =============================================================================

class SceneTests : public ::testing::Test
{
protected:
    void SetUp() override { m_Scene = std::make_unique<Scene>(); }

    void TearDown() override { m_Scene.reset(); }

    std::unique_ptr<Scene> m_Scene;
};

TEST_F(SceneTests, Creation)
{
    EXPECT_TRUE(m_Scene->GetRoot().IsValid());
    // Root exists with valid entity
    EXPECT_GT(m_Scene->GetNodeCount(), 0);
}

TEST_F(SceneTests, CreateNode)
{
    Node node = m_Scene->CreateNode("TestNode");
    EXPECT_TRUE(node.IsValid());
}

TEST_F(SceneTests, CreateChildNode)
{
    Node parent = m_Scene->CreateNode("Parent");
    Node child = m_Scene->CreateChildNode(parent, "Child");

    EXPECT_TRUE(child.IsValid());
    EXPECT_TRUE(parent.IsValid());
}

TEST_F(SceneTests, NodeHierarchy)
{
    Node a = m_Scene->CreateNode("A");
    Node b = m_Scene->CreateChildNode(a, "B");
    Node c = m_Scene->CreateChildNode(b, "C");

    // All nodes should be valid
    EXPECT_TRUE(a.IsValid());
    EXPECT_TRUE(b.IsValid());
    EXPECT_TRUE(c.IsValid());
}

TEST_F(SceneTests, TransformUpdate)
{
    // Test that UpdateTransforms() runs without crashing
    Node parent = m_Scene->CreateNode("Parent");
    Node child = m_Scene->CreateChildNode(parent, "Child");

    EXPECT_TRUE(parent.IsValid());
    EXPECT_TRUE(child.IsValid());

    // This should not crash
    m_Scene->UpdateTransforms();
}

TEST(HealthComponentTests, ClampDamageAndHealWork)
{
    HealthComponent health;
    health.currentHealth = 150.0f;
    health.maxHealth = 100.0f;
    health.Clamp();

    EXPECT_FLOAT_EQ(health.currentHealth, 100.0f);
    EXPECT_FLOAT_EQ(health.maxHealth, 100.0f);

    health.ApplyDamage(35.0f);
    EXPECT_FLOAT_EQ(health.currentHealth, 65.0f);
    EXPECT_FALSE(health.IsDead());

    health.Heal(50.0f);
    EXPECT_FLOAT_EQ(health.currentHealth, 100.0f);

    health.ApplyDamage(200.0f);
    EXPECT_FLOAT_EQ(health.currentHealth, 0.0f);
    EXPECT_TRUE(health.IsDead());
}

TEST(HealthComponentTests, EntityProxyAppliesDamageAndCanDestroyOnDeath)
{
    World world;
    Entity entity = world.CreateEntity();
    auto& health = world.AddComponent<HealthComponent>(entity);
    health.maxHealth = 75.0f;
    health.currentHealth = 75.0f;

    EntityProxy proxy(entity, &world);

    EXPECT_TRUE(proxy.HasHealth());
    EXPECT_FLOAT_EQ(proxy.GetHealth(), 75.0f);
    EXPECT_FLOAT_EQ(proxy.GetMaxHealth(), 75.0f);

    proxy.ApplyDamage(25.0f);
    EXPECT_FLOAT_EQ(proxy.GetHealth(), 50.0f);
    EXPECT_FLOAT_EQ(proxy.GetHealthPercent(), 50.0f / 75.0f);

    proxy.Heal(100.0f);
    EXPECT_FLOAT_EQ(proxy.GetHealth(), 75.0f);

    proxy.SetDestroyOnDeath(true);
    proxy.ApplyDamage(100.0f);
    EXPECT_FALSE(world.IsAlive(entity));
}

TEST(HealthComponentTests, ScriptHooksReceiveDamageAndDeathEvents)
{
    const std::filesystem::path scriptPath =
        std::filesystem::temp_directory_path() / "dot_health_hooks_test.lua";
    {
        std::ofstream scriptFile(scriptPath);
        scriptFile << "function OnDamaged(amount, source)\n";
        scriptFile << "    self.Name = 'Damaged_' .. tostring(math.floor(amount))\n";
        scriptFile << "    if source.IsValid then source.Name = 'SourceHit' end\n";
        scriptFile << "end\n";
        scriptFile << "function OnDeath(source)\n";
        scriptFile << "    self.Invulnerable = true\n";
        scriptFile << "    if source.IsValid then source.Name = 'SourceKilled' end\n";
        scriptFile << "end\n";
    }

    World world;

    Entity source = world.CreateEntity();
    world.AddComponent<NameComponent>(source).name = "Source";

    Entity target = world.CreateEntity();
    world.AddComponent<NameComponent>(target).name = "Target";
    auto& targetScript = world.AddComponent<ScriptComponent>(target);
    targetScript.scriptPath = scriptPath.string();
    auto& targetHealth = world.AddComponent<HealthComponent>(target);
    targetHealth.maxHealth = 100.0f;
    targetHealth.currentHealth = 100.0f;

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));
    scriptSystem.SetScriptBasePath("");
    scriptSystem.Start();

    EXPECT_FLOAT_EQ(scriptSystem.ApplyDamage(target, 25.0f, source), 75.0f);

    NameComponent* targetName = world.GetComponent<NameComponent>(target);
    ASSERT_NE(targetName, nullptr);
    EXPECT_EQ(targetName->name, "Damaged_25");

    NameComponent* sourceName = world.GetComponent<NameComponent>(source);
    ASSERT_NE(sourceName, nullptr);
    EXPECT_EQ(sourceName->name, "SourceHit");

    sourceName->name = "Source";
    EXPECT_FLOAT_EQ(scriptSystem.ApplyDamage(target, 100.0f, source), 0.0f);
    EXPECT_TRUE(targetHealth.IsDead());
    EXPECT_TRUE(targetHealth.invulnerable);
    EXPECT_EQ(sourceName->name, "SourceKilled");

    scriptSystem.Stop();
    std::filesystem::remove(scriptPath);
}

TEST(CameraFrustumTests, IncludesCenterAndRejectsBehindCamera)
{
    Camera camera;
    camera.SetPosition(0.0f, 0.0f, 0.0f);
    camera.LookAt(0.0f, 0.0f, 1.0f);
    camera.SetPerspective(90.0f, 1.0f, 0.1f, 100.0f);

    const Camera::Frustum frustum = camera.GetFrustum();

    EXPECT_TRUE(frustum.TestAABB(-0.5f, -0.5f, 4.0f, 0.5f, 0.5f, 6.0f));
    EXPECT_FALSE(frustum.TestAABB(-0.5f, -0.5f, -6.0f, 0.5f, 0.5f, -4.0f));
}

TEST(CameraFrustumTests, RespectsHorizontalAndVerticalFovBounds)
{
    Camera camera;
    camera.SetPosition(0.0f, 0.0f, 0.0f);
    camera.LookAt(0.0f, 0.0f, 1.0f);
    camera.SetPerspective(90.0f, 1.0f, 0.1f, 100.0f);

    const Camera::Frustum frustum = camera.GetFrustum();

    // At z = 10 with 90 degree FOV and aspect 1, visible half-width/height is about 10.
    EXPECT_TRUE(frustum.TestAABB(8.5f, -0.5f, 9.5f, 9.5f, 0.5f, 10.5f));
    EXPECT_TRUE(frustum.TestAABB(-0.5f, 8.5f, 9.5f, 0.5f, 9.5f, 10.5f));

    EXPECT_FALSE(frustum.TestAABB(10.6f, -0.5f, 9.5f, 11.4f, 0.5f, 10.5f));
    EXPECT_FALSE(frustum.TestAABB(-0.5f, 10.6f, 9.5f, 0.5f, 11.4f, 10.5f));
}

TEST(CameraFrustumTests, RespectsNearFarAndConfiguredFov)
{
    Camera wideCamera;
    wideCamera.SetPosition(0.0f, 0.0f, 0.0f);
    wideCamera.LookAt(0.0f, 0.0f, 1.0f);
    wideCamera.SetPerspective(90.0f, 1.0f, 0.1f, 100.0f);

    Camera narrowCamera;
    narrowCamera.SetPosition(0.0f, 0.0f, 0.0f);
    narrowCamera.LookAt(0.0f, 0.0f, 1.0f);
    narrowCamera.SetPerspective(60.0f, 1.0f, 0.1f, 100.0f);

    const Camera::Frustum wideFrustum = wideCamera.GetFrustum();
    const Camera::Frustum narrowFrustum = narrowCamera.GetFrustum();

    EXPECT_FALSE(wideFrustum.TestAABB(-0.25f, -0.25f, 0.001f, 0.25f, 0.25f, 0.05f));
    EXPECT_TRUE(wideFrustum.TestAABB(-0.25f, -0.25f, 0.15f, 0.25f, 0.25f, 0.25f));
    EXPECT_FALSE(wideFrustum.TestAABB(-0.5f, -0.5f, 100.5f, 0.5f, 0.5f, 101.5f));

    // This box should be visible for 90 degrees, but culled for 60 degrees.
    EXPECT_TRUE(wideFrustum.TestAABB(6.1f, -0.5f, 9.5f, 6.9f, 0.5f, 10.5f));
    EXPECT_FALSE(narrowFrustum.TestAABB(6.1f, -0.5f, 9.5f, 6.9f, 0.5f, 10.5f));
}

TEST(SpatialSplitFrustumTests, SmallBoxesUseWholeBoundsOnly)
{
    const SpatialSplitFrustumInfo info = BuildSpatialSplitFrustumInfo(-1.0f, -0.5f, 4.0f, 1.0f, 0.5f, 6.0f);

    EXPECT_FALSE(info.UsesSplits());
    EXPECT_EQ(info.ChunkCount(), 1);
}

TEST(SpatialSplitFrustumTests, LargeFloorLikeBoxesSplitAlongWideAxes)
{
    const SpatialSplitFrustumInfo info = BuildSpatialSplitFrustumInfo(-24.0f, -0.2f, -24.0f, 24.0f, 0.2f, 24.0f);

    EXPECT_TRUE(info.UsesSplits());
    EXPECT_GT(info.splitX, 1);
    EXPECT_EQ(info.splitY, 1);
    EXPECT_GT(info.splitZ, 1);
}

TEST(SpatialSplitFrustumTests, VisibleLargeFloorLikeBoxesRemainVisible)
{
    Camera camera;
    camera.SetPosition(0.0f, 2.0f, -4.0f);
    camera.LookAt(0.0f, 0.0f, 4.0f);
    camera.SetPerspective(90.0f, 1.0f, 0.1f, 100.0f);

    SpatialSplitFrustumCounters counters;
    EXPECT_TRUE(TestAABBWithSpatialSplits(camera.GetFrustum(), -24.0f, -0.2f, -24.0f, 24.0f, 0.2f, 24.0f, &counters));
    EXPECT_EQ(counters.frustumTestedObjects, 1);
    EXPECT_EQ(counters.chunkedObjects, 1);
    EXPECT_EQ(counters.acceptedViaChunkObjects, 1);
}

TEST(SpatialSplitFrustumTests, FullyOffscreenLargeBoxesStillCull)
{
    Camera camera;
    camera.SetPosition(0.0f, 0.0f, 0.0f);
    camera.LookAt(0.0f, 0.0f, 1.0f);
    camera.SetPerspective(90.0f, 1.0f, 0.1f, 100.0f);

    SpatialSplitFrustumCounters counters;
    EXPECT_FALSE(
        TestAABBWithSpatialSplits(camera.GetFrustum(), 80.0f, -1.0f, 12.0f, 140.0f, 1.0f, 44.0f, &counters));
    EXPECT_EQ(counters.frustumTestedObjects, 1);
    EXPECT_EQ(counters.chunkedObjects, 1);
    EXPECT_EQ(counters.acceptedViaChunkObjects, 0);
    EXPECT_GT(counters.frustumCulledChunks, 0);
}

TEST(SceneScriptingTests, CameraViewmodelApiUpdatesActiveCameraComponent)
{
    World world;
    Entity cameraEntity = world.CreateEntity();
    auto& camera = world.AddComponent<CameraComponent>(cameraEntity);
    camera.isActive = true;
    camera.farPlane = 250.0f;
    camera.enableViewmodelPass = false;
    camera.viewmodelFov = 75.0f;
    camera.viewmodelNearPlane = 0.01f;
    camera.renderMask = RenderLayerMask::World;

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));

    ASSERT_TRUE(scriptSystem.ExecuteCode("Camera.SetViewmodelPassEnabled(true)"));
    ASSERT_TRUE(scriptSystem.ExecuteCode("Camera.SetViewmodelFov(92.5)"));
    ASSERT_TRUE(scriptSystem.ExecuteCode("Camera.SetViewmodelNearPlane(0.025)"));
    ASSERT_TRUE(scriptSystem.ExecuteCode("Camera.SetViewmodelLayerVisible(true)"));

    EXPECT_TRUE(camera.enableViewmodelPass);
    EXPECT_FLOAT_EQ(camera.viewmodelFov, 92.5f);
    EXPECT_FLOAT_EQ(camera.viewmodelNearPlane, 0.025f);
    EXPECT_NE((camera.renderMask & RenderLayerMask::Viewmodel), 0u);

    ASSERT_TRUE(scriptSystem.ExecuteCode("Camera.SetViewmodelLayerVisible(false)"));
    EXPECT_EQ((camera.renderMask & RenderLayerMask::Viewmodel), 0u);

    scriptSystem.Shutdown();
}

TEST(SceneScriptingTests, EntityRenderLayerApiAddsAndUpdatesRenderLayerComponent)
{
    World world;
    Entity entity = world.CreateEntity();
    world.AddComponent<NameComponent>(entity).name = "Weapon";

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));

    ASSERT_TRUE(scriptSystem.ExecuteCode("local weapon = World.FindByName('Weapon'); weapon.InViewmodelLayer = true"));

    RenderLayerComponent* renderLayer = world.GetComponent<RenderLayerComponent>(entity);
    ASSERT_NE(renderLayer, nullptr);
    EXPECT_NE((renderLayer->mask & RenderLayerMask::Viewmodel), 0u);
    EXPECT_NE((renderLayer->mask & RenderLayerMask::World), 0u);

    ASSERT_TRUE(scriptSystem.ExecuteCode("local weapon = World.FindByName('Weapon'); weapon.InWorldLayer = false"));
    EXPECT_EQ((renderLayer->mask & RenderLayerMask::World), 0u);
    EXPECT_NE((renderLayer->mask & RenderLayerMask::Viewmodel), 0u);

    ASSERT_TRUE(scriptSystem.ExecuteCode("local weapon = World.FindByName('Weapon'); weapon.RenderLayerMask = Camera.WorldLayerMask"));
    EXPECT_EQ(renderLayer->mask, RenderLayerMask::World);

    scriptSystem.Shutdown();
}

TEST(SceneScriptingTests, EntityAttachmentApiRebindsBetweenEntityAndCameraTargets)
{
    World world;

    Entity cameraEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(cameraEntity).name = "Camera";
    auto& camera = world.AddComponent<CameraComponent>(cameraEntity);
    camera.isActive = true;
    world.AddComponent<TransformComponent>(cameraEntity);

    Entity muzzleSocket = world.CreateEntity();
    world.AddComponent<NameComponent>(muzzleSocket).name = "MuzzleSocket";
    world.AddComponent<TransformComponent>(muzzleSocket);
    world.AddComponent<AttachmentPointComponent>(muzzleSocket).socketName = "muzzle";
    auto& cameraHierarchy = world.AddComponent<HierarchyComponent>(cameraEntity);
    auto& muzzleHierarchy = world.AddComponent<HierarchyComponent>(muzzleSocket);
    muzzleHierarchy.parent = cameraEntity;
    cameraHierarchy.children.push_back(muzzleSocket);

    Entity targetEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(targetEntity).name = "Target";
    world.AddComponent<TransformComponent>(targetEntity);

    Entity weaponEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(weaponEntity).name = "Weapon";
    world.AddComponent<TransformComponent>(weaponEntity);

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));

    ASSERT_TRUE(scriptSystem.ExecuteCode(
        "local weapon = World.FindByName('Weapon'); "
        "local target = World.FindByName('Target'); "
        "weapon:AttachToEntity(target, 'grip')"));

    AttachmentBindingComponent* binding = world.GetComponent<AttachmentBindingComponent>(weaponEntity);
    ASSERT_NE(binding, nullptr);
    EXPECT_TRUE(binding->enabled);
    EXPECT_EQ(binding->targetMode, AttachmentTargetMode::Entity);
    EXPECT_EQ(binding->targetEntity, targetEntity);
    EXPECT_EQ(binding->socketName, "grip");

    ASSERT_TRUE(scriptSystem.ExecuteCode(
        "local weapon = World.FindByName('Weapon'); "
        "weapon:AttachToCamera('muzzle')"));
    EXPECT_TRUE(binding->enabled);
    EXPECT_EQ(binding->targetMode, AttachmentTargetMode::ActiveCamera);
    EXPECT_EQ(binding->socketName, "muzzle");

    ASSERT_TRUE(scriptSystem.ExecuteCode(
        "local weapon = World.FindByName('Weapon'); "
        "weapon:Detach()"));
    EXPECT_FALSE(binding->enabled);
    EXPECT_EQ(binding->targetEntity, kNullEntity);
    EXPECT_TRUE(binding->socketName.empty());

    scriptSystem.Shutdown();
}

TEST(SceneScriptingTests, SocketHelpersCanCreateAndFindEntityAndCameraSockets)
{
    World world;

    Entity cameraEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(cameraEntity).name = "Camera";
    auto& camera = world.AddComponent<CameraComponent>(cameraEntity);
    camera.isActive = true;
    world.AddComponent<TransformComponent>(cameraEntity);

    Entity weaponEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(weaponEntity).name = "Weapon";
    world.AddComponent<TransformComponent>(weaponEntity);

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));

    ASSERT_TRUE(scriptSystem.ExecuteCode(
        "local weapon = World.FindByName('Weapon'); "
        "local socket = weapon:CreateSocket('muzzle', Vec3(1, 2, 3), Vec3(0, 90, 0)); "
        "assert(socket ~= nil); "
        "assert(weapon:HasSocket('muzzle')); "
        "assert(weapon:GetSocket('muzzle') ~= nil); "
        "local camSocket = Camera.CreateSocket('viewmodel_root', Vec3(0.1, -0.2, 0.3)); "
        "assert(camSocket ~= nil); "
        "assert(Camera.HasSocket('viewmodel_root')); "
        "assert(Camera.GetSocket('viewmodel_root') ~= nil)"));

    const Entity muzzleSocket = FindAttachmentSocket(world, weaponEntity, "muzzle");
    ASSERT_TRUE(muzzleSocket.IsValid());
    AttachmentPointComponent* muzzlePoint = world.GetComponent<AttachmentPointComponent>(muzzleSocket);
    TransformComponent* muzzleTransform = world.GetComponent<TransformComponent>(muzzleSocket);
    HierarchyComponent* weaponHierarchy = world.GetComponent<HierarchyComponent>(weaponEntity);
    ASSERT_NE(muzzlePoint, nullptr);
    ASSERT_NE(muzzleTransform, nullptr);
    ASSERT_NE(weaponHierarchy, nullptr);
    EXPECT_EQ(muzzlePoint->socketName, "muzzle");
    EXPECT_EQ(muzzleTransform->position, Vec3(1, 2, 3));
    EXPECT_EQ(muzzleTransform->rotation, Vec3(0, 90, 0));
    EXPECT_NE(std::find(weaponHierarchy->children.begin(), weaponHierarchy->children.end(), muzzleSocket),
              weaponHierarchy->children.end());

    const Entity cameraSocket = FindAttachmentSocket(world, cameraEntity, "viewmodel_root");
    ASSERT_TRUE(cameraSocket.IsValid());
    AttachmentPointComponent* cameraPoint = world.GetComponent<AttachmentPointComponent>(cameraSocket);
    TransformComponent* cameraSocketTransform = world.GetComponent<TransformComponent>(cameraSocket);
    ASSERT_NE(cameraPoint, nullptr);
    ASSERT_NE(cameraSocketTransform, nullptr);
    EXPECT_EQ(cameraPoint->socketName, "viewmodel_root");
    EXPECT_EQ(cameraSocketTransform->position, Vec3(0.1f, -0.2f, 0.3f));

    scriptSystem.Shutdown();
}

TEST(SceneReflectionTests, MaterialComponentRegistersEmissiveProperties)
{
    if (!TypeRegistry::Get().HasType("MaterialComponent"))
        RegisterSceneComponents();

    const TypeInfo* info = TypeRegistry::Get().GetType("MaterialComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_NE(info->GetProperty("materialPath"), nullptr);
    EXPECT_NE(info->GetProperty("useMaterialFile"), nullptr);
    EXPECT_NE(info->GetProperty("emissiveColor"), nullptr);
    EXPECT_NE(info->GetProperty("emissiveStrength"), nullptr);
}

TEST(SceneReflectionTests, StaticLightingAndLightBakeModeRegister)
{
    if (!TypeRegistry::Get().HasType("StaticLightingComponent"))
        RegisterSceneComponents();

    const TypeInfo* staticLightingInfo = TypeRegistry::Get().GetType("StaticLightingComponent");
    ASSERT_NE(staticLightingInfo, nullptr);
    EXPECT_NE(staticLightingInfo->GetProperty("participateInBake"), nullptr);
    EXPECT_NE(staticLightingInfo->GetProperty("receiveBakedLighting"), nullptr);
    EXPECT_NE(staticLightingInfo->GetProperty("castBakedShadows"), nullptr);
    EXPECT_NE(staticLightingInfo->GetProperty("resolutionScale"), nullptr);
    EXPECT_NE(staticLightingInfo->GetProperty("lightmapTexturePath"), nullptr);

    const TypeInfo* directionalInfo = TypeRegistry::Get().GetType("DirectionalLightComponent");
    ASSERT_NE(directionalInfo, nullptr);
    EXPECT_NE(directionalInfo->GetProperty("lightingMode"), nullptr);
}

TEST(SceneReflectionTests, AttachmentAndViewmodelComponentsRegister)
{
    RegisterSceneComponents();

    const TypeInfo* bindingInfo = TypeRegistry::Get().GetType("AttachmentBindingComponent");
    ASSERT_NE(bindingInfo, nullptr);
    EXPECT_NE(bindingInfo->GetProperty("targetMode"), nullptr);
    EXPECT_NE(bindingInfo->GetProperty("targetEntity"), nullptr);
    EXPECT_NE(bindingInfo->GetProperty("socketName"), nullptr);
    EXPECT_NE(bindingInfo->GetProperty("positionAxes"), nullptr);
    EXPECT_NE(bindingInfo->GetProperty("rotationAxes"), nullptr);
    EXPECT_NE(bindingInfo->GetProperty("scaleAxes"), nullptr);

    const TypeInfo* pointInfo = TypeRegistry::Get().GetType("AttachmentPointComponent");
    ASSERT_NE(pointInfo, nullptr);
    EXPECT_NE(pointInfo->GetProperty("socketName"), nullptr);

    const TypeInfo* renderLayerInfo = TypeRegistry::Get().GetType("RenderLayerComponent");
    ASSERT_NE(renderLayerInfo, nullptr);
    EXPECT_NE(renderLayerInfo->GetProperty("mask"), nullptr);

    const TypeInfo* cameraInfo = TypeRegistry::Get().GetType("CameraComponent");
    ASSERT_NE(cameraInfo, nullptr);
    EXPECT_NE(cameraInfo->GetProperty("renderMask"), nullptr);
    EXPECT_NE(cameraInfo->GetProperty("enableViewmodelPass"), nullptr);
    EXPECT_NE(cameraInfo->GetProperty("viewmodelMask"), nullptr);
    EXPECT_NE(cameraInfo->GetProperty("viewmodelFov"), nullptr);
    EXPECT_NE(cameraInfo->GetProperty("viewmodelNearPlane"), nullptr);
}

TEST(SceneReflectionTests, ReRegisteringSceneComponentsKeepsStablePropertyMetadata)
{
    RegisterSceneComponents();
    RegisterSceneComponents();

    const TypeInfo* materialInfo = TypeRegistry::Get().GetType("MaterialComponent");
    ASSERT_NE(materialInfo, nullptr);

    size_t emissiveColorCount = 0;
    size_t emissiveStrengthCount = 0;
    for (const Property* prop : materialInfo->GetAllProperties())
    {
        ASSERT_NE(prop, nullptr);
        if (prop->name == "emissiveColor")
            ++emissiveColorCount;
        if (prop->name == "emissiveStrength")
            ++emissiveStrengthCount;
    }

    EXPECT_EQ(emissiveColorCount, 1u);
    EXPECT_EQ(emissiveStrengthCount, 1u);

    World world;
    Entity entity = world.CreateEntity();
    auto& material = world.AddComponent<MaterialComponent>(entity);
    material.emissiveColor = Vec3(0.3f, 0.4f, 0.5f);
    material.emissiveStrength = 2.0f;

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "dot_scene_reregister_reflection_test.dotscene";
    SceneSerializer serializer;
    ASSERT_TRUE(serializer.Save(world, scenePath.string())) << serializer.GetLastError();
    std::filesystem::remove(scenePath);
}

TEST(SceneSerializationTests, MaterialEmissiveRoundTripsThroughSceneAndPrefab)
{
    if (!TypeRegistry::Get().HasType("MaterialComponent"))
        RegisterSceneComponents();

    World world;
    Entity entity = world.CreateEntity();
    world.AddComponent<NameComponent>(entity).name = "Emitter";
    auto& transform = world.AddComponent<TransformComponent>(entity);
    transform.position = Vec3(1.0f, 2.0f, 3.0f);
    auto& material = world.AddComponent<MaterialComponent>(entity);
    material.materialPath = "Materials/TestTexture.dotmat";
    material.useMaterialFile = true;
    material.baseColor = Vec3(0.2f, 0.3f, 0.4f);
    material.metallic = 0.35f;
    material.roughness = 0.65f;
    material.emissiveColor = Vec3(0.8f, 0.1f, 0.4f);
    material.emissiveStrength = 3.5f;

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "dot_material_emissive_scene_test.dotscene";
    SceneSerializer serializer;
    ASSERT_TRUE(serializer.Save(world, scenePath.string())) << serializer.GetLastError();

    World loadedWorld;
    ASSERT_TRUE(serializer.Load(loadedWorld, scenePath.string())) << serializer.GetLastError();

    MaterialComponent* loadedMaterial = nullptr;
    loadedWorld.EachEntity(
        [&](Entity loadedEntity)
        {
            if (loadedMaterial == nullptr)
                loadedMaterial = loadedWorld.GetComponent<MaterialComponent>(loadedEntity);
        });

    ASSERT_NE(loadedMaterial, nullptr);
    EXPECT_EQ(loadedMaterial->materialPath, material.materialPath);
    EXPECT_EQ(loadedMaterial->useMaterialFile, material.useMaterialFile);
    EXPECT_FLOAT_EQ(loadedMaterial->baseColor.x, material.baseColor.x);
    EXPECT_FLOAT_EQ(loadedMaterial->baseColor.y, material.baseColor.y);
    EXPECT_FLOAT_EQ(loadedMaterial->baseColor.z, material.baseColor.z);
    EXPECT_FLOAT_EQ(loadedMaterial->emissiveColor.x, material.emissiveColor.x);
    EXPECT_FLOAT_EQ(loadedMaterial->emissiveColor.y, material.emissiveColor.y);
    EXPECT_FLOAT_EQ(loadedMaterial->emissiveColor.z, material.emissiveColor.z);
    EXPECT_FLOAT_EQ(loadedMaterial->emissiveStrength, material.emissiveStrength);

    const Prefab prefab = PrefabSystem::CreateFromEntity(world, entity, "EmitterPrefab");
    World prefabWorld;
    const Entity prefabRoot = PrefabSystem::Instantiate(prefabWorld, prefab);
    ASSERT_TRUE(prefabRoot.IsValid());

    const MaterialComponent* prefabMaterial = prefabWorld.GetComponent<MaterialComponent>(prefabRoot);
    ASSERT_NE(prefabMaterial, nullptr);
    EXPECT_EQ(prefabMaterial->materialPath, material.materialPath);
    EXPECT_EQ(prefabMaterial->useMaterialFile, material.useMaterialFile);
    EXPECT_FLOAT_EQ(prefabMaterial->baseColor.x, material.baseColor.x);
    EXPECT_FLOAT_EQ(prefabMaterial->baseColor.y, material.baseColor.y);
    EXPECT_FLOAT_EQ(prefabMaterial->baseColor.z, material.baseColor.z);
    EXPECT_FLOAT_EQ(prefabMaterial->emissiveColor.x, material.emissiveColor.x);
    EXPECT_FLOAT_EQ(prefabMaterial->emissiveColor.y, material.emissiveColor.y);
    EXPECT_FLOAT_EQ(prefabMaterial->emissiveColor.z, material.emissiveColor.z);
    EXPECT_FLOAT_EQ(prefabMaterial->emissiveStrength, material.emissiveStrength);

    std::filesystem::remove(scenePath);
}

TEST(SceneSerializationTests, StaticLightingAndBakeModeRoundTripThroughSceneAndPrefab)
{
    if (!TypeRegistry::Get().HasType("StaticLightingComponent"))
        RegisterSceneComponents();

    World world;
    Entity meshEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(meshEntity).name = "BakedMesh";
    auto& transform = world.AddComponent<TransformComponent>(meshEntity);
    transform.position = Vec3(4.0f, 5.0f, 6.0f);
    world.AddComponent<MeshComponent>(meshEntity).meshPath = "Meshes/Test.obj";
    auto& staticLighting = world.AddComponent<StaticLightingComponent>(meshEntity);
    staticLighting.participateInBake = true;
    staticLighting.receiveBakedLighting = true;
    staticLighting.castBakedShadows = false;
    staticLighting.resolutionScale = 2.0f;
    staticLighting.bakeValid = true;
    staticLighting.useBakedLighting = true;
    staticLighting.lightmapIntensity = 1.75f;
    staticLighting.lightmapTexturePath = "Assets/Lightmaps/Test/lightmap_0.bmp";
    staticLighting.lightmapSidecarPath = "Assets/Lightmaps/Test/BakedMesh.lightmesh.txt";
    staticLighting.bakeSignature = "sig-123";
    staticLighting.lightmapScaleU = 0.5f;
    staticLighting.lightmapScaleV = 0.25f;
    staticLighting.lightmapOffsetU = 0.125f;
    staticLighting.lightmapOffsetV = 0.375f;

    Entity lightEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(lightEntity).name = "BakeSun";
    world.AddComponent<TransformComponent>(lightEntity);
    auto& directional = world.AddComponent<DirectionalLightComponent>(lightEntity);
    directional.intensity = 3.0f;
    directional.castShadows = true;
    directional.lightingMode = LightingMode::Baked;

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "dot_static_lighting_scene_test.dotscene";
    SceneSerializer serializer;
    ASSERT_TRUE(serializer.Save(world, scenePath.string())) << serializer.GetLastError();

    World loadedWorld;
    ASSERT_TRUE(serializer.Load(loadedWorld, scenePath.string())) << serializer.GetLastError();

    StaticLightingComponent* loadedStaticLighting = nullptr;
    DirectionalLightComponent* loadedDirectional = nullptr;
    loadedWorld.EachEntity(
        [&](Entity loadedEntity)
        {
            if (!loadedStaticLighting)
                loadedStaticLighting = loadedWorld.GetComponent<StaticLightingComponent>(loadedEntity);
            if (!loadedDirectional)
                loadedDirectional = loadedWorld.GetComponent<DirectionalLightComponent>(loadedEntity);
        });

    ASSERT_NE(loadedStaticLighting, nullptr);
    EXPECT_TRUE(loadedStaticLighting->participateInBake);
    EXPECT_TRUE(loadedStaticLighting->receiveBakedLighting);
    EXPECT_FALSE(loadedStaticLighting->castBakedShadows);
    EXPECT_FLOAT_EQ(loadedStaticLighting->resolutionScale, staticLighting.resolutionScale);
    EXPECT_TRUE(loadedStaticLighting->bakeValid);
    EXPECT_TRUE(loadedStaticLighting->useBakedLighting);
    EXPECT_FLOAT_EQ(loadedStaticLighting->lightmapIntensity, staticLighting.lightmapIntensity);
    EXPECT_EQ(loadedStaticLighting->lightmapTexturePath, staticLighting.lightmapTexturePath);
    EXPECT_EQ(loadedStaticLighting->lightmapSidecarPath, staticLighting.lightmapSidecarPath);
    EXPECT_EQ(loadedStaticLighting->bakeSignature, staticLighting.bakeSignature);
    EXPECT_FLOAT_EQ(loadedStaticLighting->lightmapScaleU, staticLighting.lightmapScaleU);
    EXPECT_FLOAT_EQ(loadedStaticLighting->lightmapScaleV, staticLighting.lightmapScaleV);
    EXPECT_FLOAT_EQ(loadedStaticLighting->lightmapOffsetU, staticLighting.lightmapOffsetU);
    EXPECT_FLOAT_EQ(loadedStaticLighting->lightmapOffsetV, staticLighting.lightmapOffsetV);

    ASSERT_NE(loadedDirectional, nullptr);
    EXPECT_EQ(loadedDirectional->lightingMode, LightingMode::Baked);

    const Prefab prefab = PrefabSystem::CreateFromEntity(world, meshEntity, "BakedMeshPrefab");
    World prefabWorld;
    const Entity prefabRoot = PrefabSystem::Instantiate(prefabWorld, prefab);
    ASSERT_TRUE(prefabRoot.IsValid());

    const StaticLightingComponent* prefabStaticLighting = prefabWorld.GetComponent<StaticLightingComponent>(prefabRoot);
    ASSERT_NE(prefabStaticLighting, nullptr);
    EXPECT_TRUE(prefabStaticLighting->participateInBake);
    EXPECT_TRUE(prefabStaticLighting->receiveBakedLighting);
    EXPECT_FALSE(prefabStaticLighting->castBakedShadows);
    EXPECT_FLOAT_EQ(prefabStaticLighting->resolutionScale, staticLighting.resolutionScale);
    EXPECT_EQ(prefabStaticLighting->lightmapTexturePath, staticLighting.lightmapTexturePath);
    EXPECT_EQ(prefabStaticLighting->lightmapSidecarPath, staticLighting.lightmapSidecarPath);
    EXPECT_EQ(prefabStaticLighting->bakeSignature, staticLighting.bakeSignature);
    EXPECT_FLOAT_EQ(prefabStaticLighting->lightmapScaleU, staticLighting.lightmapScaleU);
    EXPECT_FLOAT_EQ(prefabStaticLighting->lightmapScaleV, staticLighting.lightmapScaleV);
    EXPECT_FLOAT_EQ(prefabStaticLighting->lightmapOffsetU, staticLighting.lightmapOffsetU);
    EXPECT_FLOAT_EQ(prefabStaticLighting->lightmapOffsetV, staticLighting.lightmapOffsetV);

    const Prefab lightPrefab = PrefabSystem::CreateFromEntity(world, lightEntity, "BakedLightPrefab");
    World lightPrefabWorld;
    const Entity lightPrefabRoot = PrefabSystem::Instantiate(lightPrefabWorld, lightPrefab);
    ASSERT_TRUE(lightPrefabRoot.IsValid());

    const DirectionalLightComponent* prefabDirectional =
        lightPrefabWorld.GetComponent<DirectionalLightComponent>(lightPrefabRoot);
    ASSERT_NE(prefabDirectional, nullptr);
    EXPECT_EQ(prefabDirectional->lightingMode, LightingMode::Baked);

    std::filesystem::remove(scenePath);
}

TEST(SceneSerializationTests, AttachmentRenderLayerAndCameraRoundTripThroughSceneAndPrefab)
{
    RegisterSceneComponents();

    World world;

    Entity cameraEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(cameraEntity).name = "PlayerCamera";
    auto& cameraTransform = world.AddComponent<TransformComponent>(cameraEntity);
    cameraTransform.position = Vec3(10.0f, 2.0f, 3.0f);
    auto& camera = world.AddComponent<CameraComponent>(cameraEntity);
    camera.isActive = true;
    camera.renderMask = RenderLayerMask::World;
    camera.enableViewmodelPass = true;
    camera.viewmodelMask = RenderLayerMask::Viewmodel;
    camera.viewmodelFov = 82.0f;
    camera.viewmodelNearPlane = 0.025f;
    auto& cameraHierarchy = world.AddComponent<HierarchyComponent>(cameraEntity);

    Entity socketEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(socketEntity).name = "ViewmodelRoot";
    auto& socketTransform = world.AddComponent<TransformComponent>(socketEntity);
    socketTransform.position = Vec3(0.0f, -0.25f, 0.8f);
    world.AddComponent<AttachmentPointComponent>(socketEntity).socketName = "viewmodel_root";
    auto& socketHierarchy = world.AddComponent<HierarchyComponent>(socketEntity);
    socketHierarchy.parent = cameraEntity;
    cameraHierarchy.children.push_back(socketEntity);

    Entity weaponEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(weaponEntity).name = "Weapon";
    auto& weaponTransform = world.AddComponent<TransformComponent>(weaponEntity);
    weaponTransform.position = Vec3(0.2f, -0.1f, 0.35f);
    auto& binding = world.AddComponent<AttachmentBindingComponent>(weaponEntity);
    binding.targetMode = AttachmentTargetMode::Entity;
    binding.targetEntity = cameraEntity;
    binding.socketName = "viewmodel_root";
    binding.followPosition = true;
    binding.followRotation = true;
    binding.followScale = false;
    binding.positionAxes = AttachmentAxisMask::XYZ;
    binding.rotationAxes = AttachmentAxisMask::XYZ;
    binding.scaleAxes = AttachmentAxisMask::XYZ;
    world.AddComponent<RenderLayerComponent>(weaponEntity).mask = RenderLayerMask::Viewmodel;
    auto& weaponHierarchy = world.AddComponent<HierarchyComponent>(weaponEntity);
    weaponHierarchy.parent = cameraEntity;
    cameraHierarchy.children.push_back(weaponEntity);

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "dot_attachment_viewmodel_scene_test.dotscene";
    SceneSerializer serializer;
    ASSERT_TRUE(serializer.Save(world, scenePath.string())) << serializer.GetLastError();

    World loadedWorld;
    ASSERT_TRUE(serializer.Load(loadedWorld, scenePath.string())) << serializer.GetLastError();

    Entity loadedCameraEntity = kNullEntity;
    Entity loadedWeaponEntity = kNullEntity;
    loadedWorld.EachEntity(
        [&](Entity entity)
        {
            if (NameComponent* name = loadedWorld.GetComponent<NameComponent>(entity))
            {
                if (name->name == "PlayerCamera")
                    loadedCameraEntity = entity;
                else if (name->name == "Weapon")
                    loadedWeaponEntity = entity;
            }
        });

    ASSERT_TRUE(loadedCameraEntity.IsValid());
    ASSERT_TRUE(loadedWeaponEntity.IsValid());

    const CameraComponent* loadedCamera = loadedWorld.GetComponent<CameraComponent>(loadedCameraEntity);
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_EQ(loadedCamera->renderMask, RenderLayerMask::World);
    EXPECT_TRUE(loadedCamera->enableViewmodelPass);
    EXPECT_EQ(loadedCamera->viewmodelMask, RenderLayerMask::Viewmodel);
    EXPECT_FLOAT_EQ(loadedCamera->viewmodelFov, 82.0f);
    EXPECT_FLOAT_EQ(loadedCamera->viewmodelNearPlane, 0.025f);

    const AttachmentBindingComponent* loadedBinding =
        loadedWorld.GetComponent<AttachmentBindingComponent>(loadedWeaponEntity);
    ASSERT_NE(loadedBinding, nullptr);
    EXPECT_EQ(loadedBinding->targetMode, AttachmentTargetMode::Entity);
    EXPECT_TRUE(loadedBinding->targetEntity.IsValid());
    EXPECT_EQ(loadedBinding->socketName, "viewmodel_root");

    const RenderLayerComponent* loadedRenderLayer = loadedWorld.GetComponent<RenderLayerComponent>(loadedWeaponEntity);
    ASSERT_NE(loadedRenderLayer, nullptr);
    EXPECT_EQ(loadedRenderLayer->mask, RenderLayerMask::Viewmodel);

    const Prefab prefab = PrefabSystem::CreateFromEntity(world, cameraEntity, "ViewmodelCameraPrefab");
    World prefabWorld;
    const Entity prefabCamera = PrefabSystem::Instantiate(prefabWorld, prefab);
    ASSERT_TRUE(prefabCamera.IsValid());

    Entity prefabWeapon = kNullEntity;
    const HierarchyComponent* prefabCameraHierarchy = prefabWorld.GetComponent<HierarchyComponent>(prefabCamera);
    ASSERT_NE(prefabCameraHierarchy, nullptr);
    for (Entity child : prefabCameraHierarchy->children)
    {
        if (NameComponent* name = prefabWorld.GetComponent<NameComponent>(child); name && name->name == "Weapon")
        {
            prefabWeapon = child;
            break;
        }
    }

    ASSERT_TRUE(prefabWeapon.IsValid());
    const AttachmentBindingComponent* prefabBinding = prefabWorld.GetComponent<AttachmentBindingComponent>(prefabWeapon);
    ASSERT_NE(prefabBinding, nullptr);
    EXPECT_EQ(prefabBinding->targetEntity, prefabCamera);
    EXPECT_EQ(prefabBinding->socketName, "viewmodel_root");

    const RenderLayerComponent* prefabRenderLayer = prefabWorld.GetComponent<RenderLayerComponent>(prefabWeapon);
    ASSERT_NE(prefabRenderLayer, nullptr);
    EXPECT_EQ(prefabRenderLayer->mask, RenderLayerMask::Viewmodel);

    const CameraComponent* prefabCameraComponent = prefabWorld.GetComponent<CameraComponent>(prefabCamera);
    ASSERT_NE(prefabCameraComponent, nullptr);
    EXPECT_TRUE(prefabCameraComponent->enableViewmodelPass);
    EXPECT_EQ(prefabCameraComponent->viewmodelMask, RenderLayerMask::Viewmodel);
    EXPECT_FLOAT_EQ(prefabCameraComponent->viewmodelFov, 82.0f);
    EXPECT_FLOAT_EQ(prefabCameraComponent->viewmodelNearPlane, 0.025f);

    std::filesystem::remove(scenePath);
}

TEST(SceneSerializationTests, PointAndSpotLightShadowSettingsRoundTripThroughScene)
{
    RegisterSceneComponents();

    World world;

    Entity pointLightEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(pointLightEntity).name = "PointShadowLight";
    world.AddComponent<TransformComponent>(pointLightEntity).position = Vec3(1.0f, 2.0f, 3.0f);
    auto& pointLight = world.AddComponent<PointLightComponent>(pointLightEntity);
    pointLight.range = 18.0f;
    pointLight.intensity = 2.5f;
    pointLight.castShadows = true;
    pointLight.shadowBias = 0.0075f;

    Entity spotLightEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(spotLightEntity).name = "SpotShadowLight";
    world.AddComponent<TransformComponent>(spotLightEntity).position = Vec3(-2.0f, 4.0f, 6.0f);
    auto& spotLight = world.AddComponent<SpotLightComponent>(spotLightEntity);
    spotLight.range = 24.0f;
    spotLight.intensity = 3.0f;
    spotLight.innerConeAngle = 20.0f;
    spotLight.outerConeAngle = 32.0f;
    spotLight.castShadows = true;
    spotLight.shadowBias = 0.006f;

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "dot_light_shadow_settings_scene_test.dotscene";
    SceneSerializer serializer;
    ASSERT_TRUE(serializer.Save(world, scenePath.string())) << serializer.GetLastError();

    World loadedWorld;
    ASSERT_TRUE(serializer.Load(loadedWorld, scenePath.string())) << serializer.GetLastError();

    PointLightComponent* loadedPointLight = nullptr;
    SpotLightComponent* loadedSpotLight = nullptr;
    loadedWorld.EachEntity(
        [&](Entity entity)
        {
            if (NameComponent* name = loadedWorld.GetComponent<NameComponent>(entity))
            {
                if (name->name == "PointShadowLight")
                    loadedPointLight = loadedWorld.GetComponent<PointLightComponent>(entity);
                else if (name->name == "SpotShadowLight")
                    loadedSpotLight = loadedWorld.GetComponent<SpotLightComponent>(entity);
            }
        });

    ASSERT_NE(loadedPointLight, nullptr);
    EXPECT_TRUE(loadedPointLight->castShadows);
    EXPECT_FLOAT_EQ(loadedPointLight->shadowBias, pointLight.shadowBias);
    EXPECT_FLOAT_EQ(loadedPointLight->range, pointLight.range);

    ASSERT_NE(loadedSpotLight, nullptr);
    EXPECT_TRUE(loadedSpotLight->castShadows);
    EXPECT_FLOAT_EQ(loadedSpotLight->shadowBias, spotLight.shadowBias);
    EXPECT_FLOAT_EQ(loadedSpotLight->range, spotLight.range);
    EXPECT_FLOAT_EQ(loadedSpotLight->innerConeAngle, spotLight.innerConeAngle);
    EXPECT_FLOAT_EQ(loadedSpotLight->outerConeAngle, spotLight.outerConeAngle);

    std::filesystem::remove(scenePath);
}

TEST(SceneTransformTests, ResolveSceneTransformsSupportsAttachmentsSocketsAndAxisMasks)
{
    World world;

    Entity cameraEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(cameraEntity).name = "Camera";
    auto& cameraTransform = world.AddComponent<TransformComponent>(cameraEntity);
    cameraTransform.position = Vec3(10.0f, 2.0f, 3.0f);
    auto& cameraComponent = world.AddComponent<CameraComponent>(cameraEntity);
    cameraComponent.isActive = true;
    auto& cameraHierarchy = world.AddComponent<HierarchyComponent>(cameraEntity);

    Entity socketEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(socketEntity).name = "Socket";
    auto& socketTransform = world.AddComponent<TransformComponent>(socketEntity);
    socketTransform.position = Vec3(1.0f, 0.0f, 0.0f);
    world.AddComponent<AttachmentPointComponent>(socketEntity).socketName = "viewmodel_root";
    auto& socketHierarchy = world.AddComponent<HierarchyComponent>(socketEntity);
    socketHierarchy.parent = cameraEntity;
    cameraHierarchy.children.push_back(socketEntity);

    Entity weaponEntity = world.CreateEntity();
    world.AddComponent<NameComponent>(weaponEntity).name = "Weapon";
    auto& weaponTransform = world.AddComponent<TransformComponent>(weaponEntity);
    weaponTransform.position = Vec3(0.5f, 1.0f, 0.25f);
    auto& weaponBinding = world.AddComponent<AttachmentBindingComponent>(weaponEntity);
    weaponBinding.targetMode = AttachmentTargetMode::ActiveCamera;
    weaponBinding.socketName = "viewmodel_root";

    Entity fallbackTarget = world.CreateEntity();
    world.AddComponent<NameComponent>(fallbackTarget).name = "FallbackTarget";
    auto& fallbackTargetTransform = world.AddComponent<TransformComponent>(fallbackTarget);
    fallbackTargetTransform.position = Vec3(2.0f, 0.0f, 0.0f);

    Entity fallbackFollower = world.CreateEntity();
    auto& fallbackFollowerTransform = world.AddComponent<TransformComponent>(fallbackFollower);
    fallbackFollowerTransform.position = Vec3(1.0f, 0.0f, 0.0f);
    auto& fallbackBinding = world.AddComponent<AttachmentBindingComponent>(fallbackFollower);
    fallbackBinding.targetMode = AttachmentTargetMode::Entity;
    fallbackBinding.targetEntity = fallbackTarget;
    fallbackBinding.socketName = "missing_socket";

    Entity axisTarget = world.CreateEntity();
    auto& axisTargetTransform = world.AddComponent<TransformComponent>(axisTarget);
    axisTargetTransform.position = Vec3(4.0f, 5.0f, 6.0f);

    Entity axisFollower = world.CreateEntity();
    auto& axisFollowerTransform = world.AddComponent<TransformComponent>(axisFollower);
    axisFollowerTransform.position = Vec3(1.0f, 1.0f, 1.0f);
    auto& axisBinding = world.AddComponent<AttachmentBindingComponent>(axisFollower);
    axisBinding.targetMode = AttachmentTargetMode::Entity;
    axisBinding.targetEntity = axisTarget;
    axisBinding.followPosition = true;
    axisBinding.followRotation = false;
    axisBinding.followScale = false;
    axisBinding.positionAxes = AttachmentAxisMask::X | AttachmentAxisMask::Z;

    ResolveSceneTransforms(world, cameraEntity);

    const Vec3 weaponWorldPosition = world.GetComponent<TransformComponent>(weaponEntity)->worldMatrix.GetTranslation();
    EXPECT_NEAR(weaponWorldPosition.x, 11.5f, 1e-4f);
    EXPECT_NEAR(weaponWorldPosition.y, 3.0f, 1e-4f);
    EXPECT_NEAR(weaponWorldPosition.z, 3.25f, 1e-4f);

    const Vec3 fallbackWorldPosition =
        world.GetComponent<TransformComponent>(fallbackFollower)->worldMatrix.GetTranslation();
    EXPECT_NEAR(fallbackWorldPosition.x, 3.0f, 1e-4f);
    EXPECT_NEAR(fallbackWorldPosition.y, 0.0f, 1e-4f);
    EXPECT_NEAR(fallbackWorldPosition.z, 0.0f, 1e-4f);

    const Vec3 axisWorldPosition = world.GetComponent<TransformComponent>(axisFollower)->worldMatrix.GetTranslation();
    EXPECT_NEAR(axisWorldPosition.x, 5.0f, 1e-4f);
    EXPECT_NEAR(axisWorldPosition.y, 1.0f, 1e-4f);
    EXPECT_NEAR(axisWorldPosition.z, 7.0f, 1e-4f);

    EXPECT_EQ(FindAttachmentSocket(world, cameraEntity, "viewmodel_root"), socketEntity);
}
