#include "Core/ECS/World.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Map/MapCompiler.h"
#include "Core/Map/MapTypes.h"
#include "Core/Map/StaticWorldGeometry.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Navigation/NavigationSystem.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scripting/ScriptSystem.h"

#include "../../Editor/Source/Settings/EditorSettings.h"
#include "../../Editor/Source/Settings/ProjectSettingsStorage.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace Dot;

namespace
{

class NavigationTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!JobSystem::Get().IsInitialized())
            JobSystem::Get().Initialize(4);
    }
};

MapCompiledData CompileGroundPlane()
{
    MapAsset asset;
    asset.nextBrushId = 2;
    asset.brushes.push_back(CreateBoxBrush(1, Vec3(0.0f, -0.5f, 0.0f), Vec3(5.0f, 0.5f, 5.0f), "Assets/Materials/Ground.dotmat"));
    return MapCompiler::Compile(asset, 1);
}

MapCompiledData CompileSeparatedPlatforms()
{
    MapAsset asset;
    asset.nextBrushId = 3;
    asset.brushes.push_back(CreateBoxBrush(1, Vec3(-4.0f, -0.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f), "Assets/Materials/A.dotmat"));
    asset.brushes.push_back(CreateBoxBrush(2, Vec3(4.0f, -0.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f), "Assets/Materials/B.dotmat"));
    return MapCompiler::Compile(asset, 2);
}

Entity CreateBoxEntity(World& world, const std::string& name, const Vec3& position, const Vec3& size, bool isTrigger = false)
{
    Entity entity = world.CreateEntity();
    world.AddComponent<NameComponent>(entity).name = name;
    auto& transform = world.AddComponent<TransformComponent>(entity);
    transform.position = position;
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& collider = world.AddComponent<BoxColliderComponent>(entity);
    collider.size = size;
    collider.center = Vec3::Zero();
    collider.isTrigger = isTrigger;
    collider.collisionMask = CollisionLayers::kAllLayersMask;
    return entity;
}

bool WaitForPathRequest(NavigationSystem& navigationSystem, uint64 requestId, int maxIterations = 500)
{
    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
        if (navigationSystem.IsPathRequestDone(requestId))
            return true;
        std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

Entity CreateControllerEntity(World& world, const Vec3& position)
{
    Entity entity = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(entity);
    transform.position = position;
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);
    world.AddComponent<NameComponent>(entity).name = "Navigator";

    auto& collider = world.AddComponent<BoxColliderComponent>(entity);
    collider.size = Vec3(1.0f, 2.0f, 1.0f);
    collider.center = Vec3::Zero();

    auto& controller = world.AddComponent<CharacterControllerComponent>(entity);
    controller.moveSpeed = 3.0f;
    controller.useGravity = true;
    controller.slideOnSteepSlopes = false;
    controller.collisionMask = CollisionLayers::kAllLayersMask;
    return entity;
}

void TickNavigation(World& world, CharacterControllerSystem& controllerSystem, NavigationSystem& navigationSystem, float dt)
{
    controllerSystem.Update(world, dt);
    navigationSystem.Update(world, controllerSystem, dt);
    controllerSystem.RefreshTriggerOverlaps(world);
}

} // namespace

TEST_F(NavigationTestFixture, NavMeshBuildsAndFindsAsyncPath)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    ASSERT_TRUE(navigationSystem.IsAvailable());

    const uint64 requestId = navigationSystem.RequestPathAsync(Vec3(0.0f, 1.0f, 0.0f), Vec3(3.0f, 1.0f, 3.0f));
    const NavigationRequestStatus initialStatus = navigationSystem.GetPathRequestStatus(requestId);
    EXPECT_NE(initialStatus, NavigationRequestStatus::Failed);

    ASSERT_TRUE(WaitForPathRequest(navigationSystem, requestId));
    EXPECT_TRUE(navigationSystem.DidPathRequestSucceed(requestId));
    EXPECT_FALSE(navigationSystem.GetPathRequestPath(requestId).empty());
}

TEST_F(NavigationTestFixture, NavMeshBuildsAndFindsSyncPath)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    std::vector<Vec3> path;
    ASSERT_TRUE(navigationSystem.FindPath(Vec3(0.0f, 1.0f, 0.0f), Vec3(3.0f, 1.0f, 3.0f), path));
    EXPECT_FALSE(path.empty());
}

TEST_F(NavigationTestFixture, UnreachableDestinationFails)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileSeparatedPlatforms());

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const uint64 requestId = navigationSystem.RequestPathAsync(Vec3(-4.0f, 1.0f, 0.0f), Vec3(4.0f, 1.0f, 0.0f));
    ASSERT_TRUE(WaitForPathRequest(navigationSystem, requestId));
    EXPECT_EQ(navigationSystem.GetPathRequestStatus(requestId), NavigationRequestStatus::Failed);
}

TEST_F(NavigationTestFixture, MoveToAdvancesCharacterControllerAndCompletes)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity entity = CreateControllerEntity(world, Vec3(0.0f, 1.05f, 0.0f));
    const uint64 moveId = navigationSystem.StartMove(world, entity, Vec3(3.0f, 1.0f, 0.0f),
                                                     NavigationMoveOptions{0.0f, 0.15f, Vec3(2.0f, 4.0f, 2.0f)});

    for (int iteration = 0; iteration < 240 && !navigationSystem.IsMoveDone(moveId); ++iteration)
        TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);

    ASSERT_TRUE(navigationSystem.DidMoveSucceed(moveId));
    const TransformComponent* transform = world.GetComponent<TransformComponent>(entity);
    ASSERT_NE(transform, nullptr);
    EXPECT_GT(transform->position.x, 2.5f);
}

TEST_F(NavigationTestFixture, MoveToAdvancesTransformMoverAndSamplesNavHeight)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity entity = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(entity);
    transform.position = Vec3(0.0f, 1.0f, 0.0f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    const uint64 moveId = navigationSystem.StartMove(world, entity, Vec3(3.0f, 1.0f, 0.0f),
                                                     NavigationMoveOptions{2.0f, 0.1f, Vec3(2.0f, 4.0f, 2.0f)});

    for (int iteration = 0; iteration < 240 && !navigationSystem.IsMoveDone(moveId); ++iteration)
        TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);

    ASSERT_TRUE(navigationSystem.DidMoveSucceed(moveId));
    EXPECT_GT(transform.position.x, 2.5f);
    EXPECT_NEAR(transform.position.y, 0.2f, 0.05f);
}

TEST_F(NavigationTestFixture, MoveToUsesNavAgentDefaultsWhenOptionsAreOmitted)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity entity = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(entity);
    transform.position = Vec3(0.0f, 1.0f, 0.0f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& navAgent = world.AddComponent<NavAgentComponent>(entity);
    navAgent.moveSpeed = 1.0f;
    navAgent.stoppingDistance = 1.0f;
    navAgent.projectionExtent = Vec3(1.5f, 5.0f, 1.5f);

    const uint64 moveId = navigationSystem.StartMove(world, entity, Vec3(3.0f, 1.0f, 0.0f));

    for (int iteration = 0; iteration < 300 && !navigationSystem.IsMoveDone(moveId); ++iteration)
        TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);

    ASSERT_TRUE(navigationSystem.DidMoveSucceed(moveId));
    const TransformComponent* finalTransform = world.GetComponent<TransformComponent>(entity);
    ASSERT_NE(finalTransform, nullptr);
    EXPECT_LT(finalTransform->position.x, 2.5f);
    EXPECT_GT(finalTransform->position.x, 1.5f);
    EXPECT_NEAR(finalTransform->position.y, 0.2f, 0.05f);
}

TEST_F(NavigationTestFixture, ReplacingMoveCancelsPreviousRequestAndDestroyedEntityCancelsActiveMove)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity entity = CreateControllerEntity(world, Vec3(0.0f, 1.05f, 0.0f));
    const uint64 firstMove = navigationSystem.StartMove(world, entity, Vec3(4.0f, 1.0f, 0.0f));
    const uint64 secondMove = navigationSystem.StartMove(world, entity, Vec3(-4.0f, 1.0f, 0.0f));

    EXPECT_EQ(navigationSystem.GetMoveStatus(firstMove), NavigationMoveStatus::Cancelled);

    for (int iteration = 0; iteration < 300 && !navigationSystem.IsMoveDone(secondMove); ++iteration)
        TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);

    ASSERT_TRUE(navigationSystem.DidMoveSucceed(secondMove));

    const uint64 thirdMove = navigationSystem.StartMove(world, entity, Vec3(4.0f, 1.0f, 0.0f));
    world.DestroyEntity(entity);
    TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);
    EXPECT_EQ(navigationSystem.GetMoveStatus(thirdMove), NavigationMoveStatus::Cancelled);
}

TEST_F(NavigationTestFixture, ScriptSystemCanPollPathAndMoveHandles)
{
    const std::filesystem::path scriptPath =
        std::filesystem::temp_directory_path() / "dot_navigation_script_test.lua";
    {
        std::ofstream scriptFile(scriptPath);
        scriptFile << "pathReq = nil\n";
        scriptFile << "moveReq = nil\n";
        scriptFile << "function OnStart()\n";
        scriptFile << "    pathReq = Navigation.FindPathAsync(self.Position, Vec3(3, 1, 0))\n";
        scriptFile << "    moveReq = self:MoveTo(Vec3(3, 1, 0), { speed = 2.5, stoppingDistance = 0.15 })\n";
        scriptFile << "end\n";
        scriptFile << "function OnUpdate(dt)\n";
        scriptFile << "    if moveReq and moveReq.Succeeded then\n";
        scriptFile << "        self.Name = 'MoveDone'\n";
        scriptFile << "    elseif pathReq and pathReq.Succeeded then\n";
        scriptFile << "        self.Name = 'PathReady'\n";
        scriptFile << "    end\n";
        scriptFile << "end\n";
    }

    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity entity = CreateControllerEntity(world, Vec3(0.0f, 1.05f, 0.0f));
    world.AddComponent<ScriptComponent>(entity).scriptPath = scriptPath.string();

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));
    scriptSystem.SetScriptBasePath("");
    scriptSystem.SetCharacterControllerSystem(&controllerSystem);
    scriptSystem.SetStaticWorldGeometry(&staticWorld);
    scriptSystem.SetNavigationSystem(&navigationSystem);
    scriptSystem.Start();

    for (int iteration = 0; iteration < 360; ++iteration)
    {
        TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);
        scriptSystem.Update(1.0f / 60.0f);

        NameComponent* name = world.GetComponent<NameComponent>(entity);
        if (name && name->name == "MoveDone")
            break;

        std::this_thread::yield();
    }

    NameComponent* name = world.GetComponent<NameComponent>(entity);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->name, "MoveDone");

    scriptSystem.Stop();
    std::filesystem::remove(scriptPath);
}

TEST_F(NavigationTestFixture, ScriptSystemSupportsSyncPathsMoveStatusAndMoveFinishedCallbacks)
{
    const std::filesystem::path scriptPath =
        std::filesystem::temp_directory_path() / "dot_navigation_script_callback_test.lua";
    {
        std::ofstream scriptFile(scriptPath);
        scriptFile << "syncPathReady = false\n";
        scriptFile << "callbackSeen = false\n";
        scriptFile << "eventSeen = false\n";
        scriptFile << "movingSeen = false\n";
        scriptFile << "callbackHandle = nil\n";
        scriptFile << "eventHandle = nil\n";
        scriptFile << "function OnStart()\n";
        scriptFile << "    local path = Navigation.FindPath(self.Position, Vec3(3, 1, 0))\n";
        scriptFile << "    syncPathReady = path ~= nil and #path > 0\n";
        scriptFile << "    self:MoveTo(Vec3(3, 1, 0), { speed = 2.5, stoppingDistance = 0.15 }, function(status, handle)\n";
        scriptFile << "        callbackHandle = handle\n";
        scriptFile << "        callbackSeen = (status == 'succeeded')\n";
        scriptFile << "    end)\n";
        scriptFile << "end\n";
        scriptFile << "function OnMoveFinished(status, handle)\n";
        scriptFile << "    eventHandle = handle\n";
        scriptFile << "    eventSeen = (status == 'succeeded')\n";
        scriptFile << "end\n";
        scriptFile << "function OnUpdate(dt)\n";
        scriptFile << "    if self:IsMoving() and self:GetMoveStatus() == 'moving' then\n";
        scriptFile << "        movingSeen = true\n";
        scriptFile << "    end\n";
        scriptFile << "    local callbackHandleReady = callbackHandle ~= nil and callbackHandle.Succeeded\n";
        scriptFile << "    local eventHandleReady = eventHandle ~= nil and eventHandle.Succeeded\n";
        scriptFile << "    if syncPathReady and callbackSeen and eventSeen and movingSeen and callbackHandleReady and eventHandleReady then\n";
        scriptFile << "        self.Name = 'NavApiDone'\n";
        scriptFile << "    end\n";
        scriptFile << "end\n";
    }

    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity entity = CreateControllerEntity(world, Vec3(0.0f, 1.05f, 0.0f));
    world.AddComponent<ScriptComponent>(entity).scriptPath = scriptPath.string();

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));
    scriptSystem.SetScriptBasePath("");
    scriptSystem.SetCharacterControllerSystem(&controllerSystem);
    scriptSystem.SetStaticWorldGeometry(&staticWorld);
    scriptSystem.SetNavigationSystem(&navigationSystem);
    scriptSystem.Start();

    for (int iteration = 0; iteration < 360; ++iteration)
    {
        TickNavigation(world, controllerSystem, navigationSystem, 1.0f / 60.0f);
        scriptSystem.Update(1.0f / 60.0f);

        NameComponent* name = world.GetComponent<NameComponent>(entity);
        if (name && name->name == "NavApiDone")
            break;

        std::this_thread::yield();
    }

    NameComponent* name = world.GetComponent<NameComponent>(entity);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->name, "NavApiDone");

    scriptSystem.Stop();
    std::filesystem::remove(scriptPath);
}

TEST_F(NavigationTestFixture, ScriptSystemSupportsPerceptionHelpers)
{
    const std::filesystem::path scriptPath =
        std::filesystem::temp_directory_path() / "dot_perception_script_test.lua";
    {
        std::ofstream scriptFile(scriptPath);
        scriptFile << "function HasNamedEntity(list, name)\n";
        scriptFile << "    for _, entity in ipairs(list) do\n";
        scriptFile << "        if entity.Name == name then\n";
        scriptFile << "            return true\n";
        scriptFile << "        end\n";
        scriptFile << "    end\n";
        scriptFile << "    return false\n";
        scriptFile << "end\n";
        scriptFile << "function OnStart()\n";
        scriptFile << "    local blocked = World.FindByName('BlockedTarget')\n";
        scriptFile << "    local visible = World.FindByName('VisibleTarget')\n";
        scriptFile << "    local behind = World.FindByName('BehindTarget')\n";
        scriptFile << "    local near = World.FindByName('NearTarget')\n";
        scriptFile << "    local blockedLos = not self:HasLineOfSight(blocked)\n";
        scriptFile << "    local blockedGlobalLos = not Perception.HasLineOfSight(self, blocked)\n";
        scriptFile << "    local visibleSeen = self:CanSee(visible, { fieldOfView = 120, maxDistance = 10 })\n";
        scriptFile << "    local visibleSeenGlobal = Perception.CanSee(self, visible, { fieldOfView = 120, maxDistance = 10 })\n";
        scriptFile << "    local behindHidden = not self:CanSee(behind, { fieldOfView = 120, maxDistance = 10 })\n";
        scriptFile << "    local nearList = self:GetEntitiesInRadius(2.5)\n";
        scriptFile << "    local globalNearList = Perception.GetEntitiesInRadius(self.Position, 2.5)\n";
        scriptFile << "    local nearFound = HasNamedEntity(nearList, 'NearTarget')\n";
        scriptFile << "    local globalNearFound = HasNamedEntity(globalNearList, 'NearTarget')\n";
        scriptFile << "    if not blockedLos then self.Name = 'FailBlockedLos'; return end\n";
        scriptFile << "    if not blockedGlobalLos then self.Name = 'FailBlockedGlobalLos'; return end\n";
        scriptFile << "    if not visibleSeen then self.Name = 'FailVisibleSeen'; return end\n";
        scriptFile << "    if not visibleSeenGlobal then self.Name = 'FailVisibleSeenGlobal'; return end\n";
        scriptFile << "    if not behindHidden then self.Name = 'FailBehindHidden'; return end\n";
        scriptFile << "    if not nearFound then self.Name = 'FailNearFound'; return end\n";
        scriptFile << "    if not globalNearFound then self.Name = 'FailGlobalNearFound'; return end\n";
        scriptFile << "    if not near.IsValid then self.Name = 'FailNearValid'; return end\n";
        scriptFile << "    self.Name = 'PerceptionOk'\n";
        scriptFile << "end\n";
    }

    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    World world;
    CharacterControllerSystem controllerSystem;
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    NavigationSystem navigationSystem;
    navigationSystem.SetStaticWorldGeometry(&staticWorld);

    const Entity observer = CreateControllerEntity(world, Vec3(0.0f, 1.05f, 0.0f));
    world.GetComponent<NameComponent>(observer)->name = "Observer";
    world.AddComponent<ScriptComponent>(observer).scriptPath = scriptPath.string();

    CreateBoxEntity(world, "BlockedTarget", Vec3(0.0f, 1.0f, 6.0f), Vec3(0.5f, 1.8f, 0.5f));
    CreateBoxEntity(world, "VisibleTarget", Vec3(3.0f, 1.0f, 3.0f), Vec3(0.5f, 1.8f, 0.5f));
    CreateBoxEntity(world, "BehindTarget", Vec3(0.0f, 1.0f, -3.0f), Vec3(0.5f, 1.8f, 0.5f));
    CreateBoxEntity(world, "NearTarget", Vec3(1.5f, 1.0f, 0.0f), Vec3(0.5f, 1.8f, 0.5f));
    CreateBoxEntity(world, "Wall", Vec3(0.0f, 1.0f, 3.0f), Vec3(2.0f, 2.5f, 0.5f));

    ScriptSystem scriptSystem;
    ASSERT_TRUE(scriptSystem.Initialize(&world));
    scriptSystem.SetScriptBasePath("");
    scriptSystem.SetCharacterControllerSystem(&controllerSystem);
    scriptSystem.SetStaticWorldGeometry(&staticWorld);
    scriptSystem.SetNavigationSystem(&navigationSystem);
    scriptSystem.Start();
    scriptSystem.Update(1.0f / 60.0f);

    NameComponent* observerName = world.GetComponent<NameComponent>(observer);
    ASSERT_NE(observerName, nullptr);
    EXPECT_EQ(observerName->name, "PerceptionOk");

    scriptSystem.Stop();
    std::filesystem::remove(scriptPath);
}

TEST(ProjectSettingsStorageTests, SaveAndLoadRoundTripsNavMeshGizmoSetting)
{
    namespace fs = std::filesystem;

    const fs::path previousCwd = fs::current_path();
    const fs::path tempRoot = fs::temp_directory_path() / "dot_project_settings_navmesh_roundtrip";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "Config");

    auto& editorSettings = EditorSettings::Get();
    const bool originalShowNavMesh = editorSettings.showNavMeshGizmo;

    fs::current_path(tempRoot);
    editorSettings.showNavMeshGizmo = true;
    ASSERT_TRUE(ProjectSettingsStorage::Save());

    editorSettings.showNavMeshGizmo = false;
    ASSERT_TRUE(ProjectSettingsStorage::Load());
    EXPECT_TRUE(editorSettings.showNavMeshGizmo);

    editorSettings.showNavMeshGizmo = originalShowNavMesh;
    fs::current_path(previousCwd);
    fs::remove_all(tempRoot);
}
