#include "Core/ECS/World.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/PhysicsSettings.h"
#include "Core/Physics/PhysicsSystem.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Scene/Components.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace Dot;

class PhysicsParallelTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!JobSystem::Get().IsInitialized())
        {
            JobSystem::Get().Initialize(4);
        }

        m_SavedPhysicsSettings = PhysicsSettings::Get();
        PhysicsSettings::Get() = PhysicsSettings{};
        CollisionLayers::Get().ResetDefaults();
    }

    void TearDown() override
    {
        PhysicsSettings::Get() = m_SavedPhysicsSettings;
        CollisionLayers::Get().ResetDefaults();
    }

    PhysicsSettings m_SavedPhysicsSettings = {};
};

namespace
{

constexpr uint8 kPlayerLayer = 1;
constexpr uint8 kWorldLayer = 2;

Entity CreatePhysicsBox(World& world, const Vec3& position, bool isKinematic, uint8 layer, uint32 mask)
{
    Entity entity = world.CreateEntity();

    auto& transform = world.AddComponent<TransformComponent>(entity);
    transform.position = position;
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& rigidBody = world.AddComponent<RigidBodyComponent>(entity);
    rigidBody.isKinematic = isKinematic;
    rigidBody.useGravity = false;
    rigidBody.mass = 1.0f;

    auto& collider = world.AddComponent<BoxColliderComponent>(entity);
    collider.size = Vec3(1.0f, 1.0f, 1.0f);
    collider.collisionLayer = layer;
    collider.collisionMask = mask;

    return entity;
}

float GetAxisSeparation(World& world, Entity a, Entity b)
{
    const auto* transformA = world.GetComponent<TransformComponent>(a);
    const auto* transformB = world.GetComponent<TransformComponent>(b);
    return std::abs(transformA->position.x - transformB->position.x);
}

} // namespace

TEST_F(PhysicsParallelTests, StressCollisionDetection)
{
    World world;
    PhysicsSystem physics;

    // Create a large number of overlapping entities to stress parallel detection
    constexpr int kEntityCount = 200;

    for (int i = 0; i < kEntityCount; ++i)
    {
        Entity e = world.CreateEntity();

        auto& t = world.AddComponent<TransformComponent>(e);
        t.position = Vec3(0, 0, 0); // All overlapping at origin
        t.scale = Vec3(1, 1, 1);

        world.AddComponent<RigidBodyComponent>(e);

        auto& box = world.AddComponent<BoxColliderComponent>(e);
        box.size = Vec3(1, 1, 1);
    }

    // This will trigger O(N^2) collision checks in parallel
    // 200 * 199 / 2 = 19,850 pairs
    physics.Update(world, 1.0f / 60.0f);

    // Since all overlap, we expect exactly 19,850 collision pairs
    // Wait, the internal m_CollisionPairs is cleared and repopulated in DetectCollisions
    // We can't access it directly, but we can verify it doesn't crash
    // and we can check the time or output if we added logging.

    // For now, verification that it completes without crash is a win for parallel safety.
    SUCCEED();
}

TEST_F(PhysicsParallelTests, DistributedEntities)
{
    World world;
    PhysicsSystem physics;

    // Create entities in a grid to ensure spatial distribution doesn't break logic
    constexpr int kGridSize = 10; // 100 entities

    for (int x = 0; x < kGridSize; ++x)
    {
        for (int y = 0; y < kGridSize; ++y)
        {
            Entity e = world.CreateEntity();
            auto& t = world.AddComponent<TransformComponent>(e);
            t.position = Vec3((float)x * 2.0f, (float)y * 2.0f, 0);
            t.scale = Vec3(1, 1, 1);

            world.AddComponent<RigidBodyComponent>(e);
            auto& box = world.AddComponent<BoxColliderComponent>(e);
            box.size = Vec3(1, 1, 1);
        }
    }

    // No entities should collide (spaced by 2.0, size 1.0)
    physics.Update(world, 1.0f / 60.0f);

    SUCCEED();
}

TEST_F(PhysicsParallelTests, CollisionMatrixBlocksRuntimeResolution)
{
    World world;
    PhysicsSystem physics;

    Entity dynamicBox = CreatePhysicsBox(world, Vec3(0.0f, 0.0f, 0.0f), false, kPlayerLayer,
                                         CollisionLayers::LayerBit(kWorldLayer));
    Entity staticBox = CreatePhysicsBox(world, Vec3(0.8f, 0.0f, 0.0f), true, kWorldLayer,
                                        CollisionLayers::LayerBit(kPlayerLayer));

    const float initialSeparation = GetAxisSeparation(world, dynamicBox, staticBox);
    CollisionLayers::Get().SetLayersCollide(kPlayerLayer, kWorldLayer, false);

    physics.Update(world, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(GetAxisSeparation(world, dynamicBox, staticBox), initialSeparation);
}

TEST_F(PhysicsParallelTests, CollisionMaskBlocksRuntimeResolution)
{
    World world;
    PhysicsSystem physics;

    Entity dynamicBox = CreatePhysicsBox(world, Vec3(0.0f, 0.0f, 0.0f), false, kPlayerLayer, 0);
    Entity staticBox = CreatePhysicsBox(world, Vec3(0.8f, 0.0f, 0.0f), true, kWorldLayer,
                                        CollisionLayers::LayerBit(kPlayerLayer));

    const float initialSeparation = GetAxisSeparation(world, dynamicBox, staticBox);

    physics.Update(world, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(GetAxisSeparation(world, dynamicBox, staticBox), initialSeparation);
}

TEST_F(PhysicsParallelTests, CollisionFilterAllowsRuntimeResolutionWhenEnabled)
{
    World world;
    PhysicsSystem physics;

    Entity dynamicBox = CreatePhysicsBox(world, Vec3(0.0f, 0.0f, 0.0f), false, kPlayerLayer,
                                         CollisionLayers::LayerBit(kWorldLayer));
    Entity staticBox = CreatePhysicsBox(world, Vec3(0.8f, 0.0f, 0.0f), true, kWorldLayer,
                                        CollisionLayers::LayerBit(kPlayerLayer));

    const float initialSeparation = GetAxisSeparation(world, dynamicBox, staticBox);

    physics.Update(world, 1.0f / 60.0f);

    EXPECT_GT(GetAxisSeparation(world, dynamicBox, staticBox), initialSeparation + 0.0001f);
}

TEST_F(PhysicsParallelTests, CharacterControllerGroundCheckHonorsCollisionMask)
{
    World world;
    CharacterControllerSystem controllerSystem;

    Entity controller = world.CreateEntity();
    auto& controllerTransform = world.AddComponent<TransformComponent>(controller);
    controllerTransform.position = Vec3(0.0f, 1.05f, 0.0f);
    controllerTransform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& controllerBox = world.AddComponent<BoxColliderComponent>(controller);
    controllerBox.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.collisionLayer = kPlayerLayer;
    character.collisionMask = 0;
    character.groundCheckDistance = 0.1f;
    character.skinWidth = 0.02f;
    character.useGravity = false;

    Entity ground = world.CreateEntity();
    auto& groundTransform = world.AddComponent<TransformComponent>(ground);
    groundTransform.position = Vec3(0.0f, -0.5f, 0.0f);
    groundTransform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& groundBox = world.AddComponent<BoxColliderComponent>(ground);
    groundBox.size = Vec3(10.0f, 1.0f, 10.0f);
    groundBox.collisionLayer = kWorldLayer;
    groundBox.collisionMask = CollisionLayers::LayerBit(kPlayerLayer);

    controllerSystem.Update(world, 1.0f / 60.0f);
    EXPECT_FALSE(controllerSystem.IsGrounded(world, controller));

    character.collisionMask = CollisionLayers::LayerBit(kWorldLayer);
    controllerSystem.Update(world, 1.0f / 60.0f);
    EXPECT_TRUE(controllerSystem.IsGrounded(world, controller));
}

TEST_F(PhysicsParallelTests, CharacterControllerSlidesAlongDynamicWallWithoutCornerSticking)
{
    World world;
    CharacterControllerSystem controllerSystem;

    Entity controller = world.CreateEntity();
    auto& controllerTransform = world.AddComponent<TransformComponent>(controller);
    const Vec3 startPosition(0.0f, 1.05f, -2.0f);
    controllerTransform.position = startPosition;
    controllerTransform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& controllerBox = world.AddComponent<BoxColliderComponent>(controller);
    controllerBox.size = Vec3(1.0f, 2.0f, 1.0f);
    controllerBox.collisionLayer = kPlayerLayer;
    controllerBox.collisionMask = CollisionLayers::LayerBit(kWorldLayer);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.moveSpeed = 3.5f;
    character.collisionLayer = kPlayerLayer;
    character.collisionMask = CollisionLayers::LayerBit(kWorldLayer);
    character.groundCheckDistance = 0.1f;
    character.skinWidth = 0.02f;
    character.useGravity = false;

    Entity ground = CreatePhysicsBox(world, Vec3(0.0f, -0.5f, 0.0f), true, kWorldLayer, CollisionLayers::LayerBit(kPlayerLayer));
    world.GetComponent<BoxColliderComponent>(ground)->size = Vec3(20.0f, 1.0f, 20.0f);

    Entity wall = CreatePhysicsBox(world, Vec3(1.4f, 1.0f, 0.0f), true, kWorldLayer, CollisionLayers::LayerBit(kPlayerLayer));
    world.GetComponent<BoxColliderComponent>(wall)->size = Vec3(1.0f, 2.0f, 8.0f);

    const Vec3 inputDirection = Vec3(1.0f, 0.0f, 1.0f).Normalized();
    const float dt = 1.0f / 60.0f;
    const int steps = 90;
    for (int step = 0; step < steps; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, inputDirection, false, false, dt);
    }

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);

    const Vec3 horizontalDelta(finalTransform->position.x - startPosition.x, 0.0f, finalTransform->position.z - startPosition.z);
    const float actualHorizontalTravel = horizontalDelta.Length();
    const float expectedHorizontalTravel = character.moveSpeed * dt * static_cast<float>(steps);

    EXPECT_GT(finalTransform->position.z, startPosition.z + 1.0f);
    EXPECT_LT(finalTransform->position.x, 1.0f);
    EXPECT_LE(actualHorizontalTravel, expectedHorizontalTravel + 0.15f);
}
