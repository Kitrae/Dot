// =============================================================================
// Dot Engine - ECS Unit Tests
// =============================================================================

#include "Core/ECS/Archetype.h"
#include "Core/ECS/ComponentType.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/World.h"

#include <gtest/gtest.h>
#include <string>


using namespace Dot;

// =============================================================================
// Test Components
// =============================================================================

struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Velocity
{
    float vx = 0, vy = 0, vz = 0;
};

struct Health
{
    int current = 100;
    int max = 100;
};

struct Label
{
    std::string text;
};

// =============================================================================
// Entity Tests
// =============================================================================

TEST(EntityTests, Construction)
{
    Entity e(42, 3);
    EXPECT_EQ(e.GetIndex(), 42);
    EXPECT_EQ(e.GetGeneration(), 3);
}

TEST(EntityTests, Invalid)
{
    Entity e = Entity::Invalid();
    EXPECT_FALSE(e.IsValid());
    EXPECT_EQ(e, kNullEntity);
}

TEST(EntityTests, Comparison)
{
    Entity a(1, 1);
    Entity b(1, 1);
    Entity c(2, 1);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// =============================================================================
// ComponentType Tests
// =============================================================================

TEST(ComponentTypeTests, UniqueIds)
{
    ComponentTypeId posId = GetComponentTypeId<Position>();
    ComponentTypeId velId = GetComponentTypeId<Velocity>();
    ComponentTypeId healthId = GetComponentTypeId<Health>();

    EXPECT_NE(posId, velId);
    EXPECT_NE(posId, healthId);
    EXPECT_NE(velId, healthId);
}

TEST(ComponentTypeTests, ConsistentIds)
{
    ComponentTypeId id1 = GetComponentTypeId<Position>();
    ComponentTypeId id2 = GetComponentTypeId<Position>();

    EXPECT_EQ(id1, id2);
}

// =============================================================================
// ArchetypeSignature Tests
// =============================================================================

TEST(ArchetypeSignatureTests, AddHas)
{
    ArchetypeSignature sig;
    sig.Add(GetComponentTypeId<Position>());

    EXPECT_TRUE(sig.Has(GetComponentTypeId<Position>()));
    EXPECT_FALSE(sig.Has(GetComponentTypeId<Velocity>()));
}

TEST(ArchetypeSignatureTests, Contains)
{
    ArchetypeSignature full;
    full.Add(GetComponentTypeId<Position>());
    full.Add(GetComponentTypeId<Velocity>());

    ArchetypeSignature partial;
    partial.Add(GetComponentTypeId<Position>());

    EXPECT_TRUE(full.Contains(partial));
    EXPECT_FALSE(partial.Contains(full));
}

// =============================================================================
// World Tests
// =============================================================================

TEST(WorldTests, CreateEntity)
{
    World world;

    Entity e1 = world.CreateEntity();
    Entity e2 = world.CreateEntity();

    EXPECT_TRUE(e1.IsValid());
    EXPECT_TRUE(e2.IsValid());
    EXPECT_NE(e1, e2);
    EXPECT_EQ(world.GetEntityCount(), 2);
}

TEST(WorldTests, DestroyEntity)
{
    World world;

    Entity e = world.CreateEntity();
    EXPECT_TRUE(world.IsAlive(e));

    world.DestroyEntity(e);
    EXPECT_FALSE(world.IsAlive(e));
    EXPECT_EQ(world.GetEntityCount(), 0);
}

TEST(WorldTests, EntityReuse)
{
    World world;

    Entity e1 = world.CreateEntity();
    uint32 oldIndex = e1.GetIndex();
    world.DestroyEntity(e1);

    Entity e2 = world.CreateEntity();
    EXPECT_EQ(e2.GetIndex(), oldIndex); // Reused index
    EXPECT_NE(e1, e2);                  // Different generation
}

TEST(WorldTests, AddComponent)
{
    World world;
    Entity e = world.CreateEntity();

    Position& pos = world.AddComponent<Position>(e);
    pos.x = 10;
    pos.y = 20;

    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_FALSE(world.HasComponent<Velocity>(e));

    Position* p = world.GetComponent<Position>(e);
    EXPECT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 10);
    EXPECT_FLOAT_EQ(p->y, 20);
}

TEST(WorldTests, MultipleComponents)
{
    World world;
    Entity e = world.CreateEntity();

    Position& pos = world.AddComponent<Position>(e);
    pos.x = 5;

    Velocity& vel = world.AddComponent<Velocity>(e);
    vel.vx = 1;

    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_TRUE(world.HasComponent<Velocity>(e));
}

TEST(WorldTests, EachQuery)
{
    World world;

    // Create entities with Position
    for (int i = 0; i < 5; ++i)
    {
        Entity e = world.CreateEntity();
        Position& pos = world.AddComponent<Position>(e);
        pos.x = static_cast<float>(i);
    }

    int count = 0;
    world.Each<Position>(
        [&](Entity e, Position& pos)
        {
            (void)e;
            (void)pos;
            count++;
        });

    EXPECT_EQ(count, 5);
}

TEST(WorldTests, RemoveComponentPreservesRemainingData)
{
    World world;
    Entity e = world.CreateEntity();

    Position& pos = world.AddComponent<Position>(e);
    pos.x = 12.5f;
    pos.y = -3.0f;
    pos.z = 99.0f;

    Velocity& vel = world.AddComponent<Velocity>(e);
    vel.vx = 7.0f;
    vel.vy = 8.0f;
    vel.vz = 9.0f;

    Health& hp = world.AddComponent<Health>(e);
    hp.current = 42;
    hp.max = 1337;

    world.RemoveComponent<Velocity>(e);

    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_TRUE(world.HasComponent<Health>(e));
    EXPECT_FALSE(world.HasComponent<Velocity>(e));

    Position* outPos = world.GetComponent<Position>(e);
    ASSERT_NE(outPos, nullptr);
    EXPECT_FLOAT_EQ(outPos->x, 12.5f);
    EXPECT_FLOAT_EQ(outPos->y, -3.0f);
    EXPECT_FLOAT_EQ(outPos->z, 99.0f);

    Health* outHp = world.GetComponent<Health>(e);
    ASSERT_NE(outHp, nullptr);
    EXPECT_EQ(outHp->current, 42);
    EXPECT_EQ(outHp->max, 1337);
}

TEST(WorldTests, Clear)
{
    World world;

    for (int i = 0; i < 10; ++i)
    {
        Entity e = world.CreateEntity();
        world.AddComponent<Position>(e);
    }

    EXPECT_EQ(world.GetEntityCount(), 10);

    world.Clear();

    EXPECT_EQ(world.GetEntityCount(), 0);
}

TEST(WorldTests, StringComponentsSurviveArchetypeGrowth)
{
    World world;
    std::vector<Entity> entities;
    entities.reserve(128);

    for (int i = 0; i < 128; ++i)
    {
        Entity entity = world.CreateEntity();
        Label& label = world.AddComponent<Label>(entity);
        label.text = "EntityLabel_" + std::to_string(i) + "_with_payload";
        entities.push_back(entity);
    }

    for (int i = 0; i < static_cast<int>(entities.size()); ++i)
    {
        Label* label = world.GetComponent<Label>(entities[i]);
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->text, "EntityLabel_" + std::to_string(i) + "_with_payload");
    }
}
