#include "../../Editor/Source/Commands/EntityClipboard.h"
#include "../../Editor/Source/Commands/CreateEntityCommands.h"
#include "Core/ECS/World.h"
#include "Core/Scene/Components.h"

#include <gtest/gtest.h>

using namespace Dot;

TEST(EntityCommandTests, PasteEntitySnapshotCreatesRootLevelCopyAndSupportsUndo)
{
    World world;

    Entity parent = world.CreateEntity();
    world.AddComponent<HierarchyComponent>(parent);

    Entity source = world.CreateEntity();
    world.AddComponent<NameComponent>(source).name = "Source";
    auto& transform = world.AddComponent<TransformComponent>(source);
    transform.position = {1.0f, 2.0f, 3.0f};
    auto& hierarchy = world.AddComponent<HierarchyComponent>(source);
    hierarchy.parent = parent;
    hierarchy.children.push_back(Entity(123, 1));

    Entity pasted = kNullEntity;
    PasteEntitySnapshotCommand cmd(&world, CaptureEntitySnapshot(world, source), &pasted, true);
    cmd.Execute();

    ASSERT_TRUE(pasted.IsValid());
    ASSERT_TRUE(world.IsAlive(pasted));

    const NameComponent* pastedName = world.GetComponent<NameComponent>(pasted);
    ASSERT_NE(pastedName, nullptr);
    EXPECT_EQ(pastedName->name, "Source (Copy)");

    const TransformComponent* pastedTransform = world.GetComponent<TransformComponent>(pasted);
    ASSERT_NE(pastedTransform, nullptr);
    EXPECT_FLOAT_EQ(pastedTransform->position.x, 1.0f);
    EXPECT_FLOAT_EQ(pastedTransform->position.y, 2.0f);
    EXPECT_FLOAT_EQ(pastedTransform->position.z, 3.0f);

    const HierarchyComponent* pastedHierarchy = world.GetComponent<HierarchyComponent>(pasted);
    ASSERT_NE(pastedHierarchy, nullptr);
    EXPECT_EQ(pastedHierarchy->parent, kNullEntity);
    EXPECT_TRUE(pastedHierarchy->children.empty());

    cmd.Undo();
    EXPECT_FALSE(world.IsAlive(pasted));
}

TEST(EntityCommandTests, PasteEntitySnapshotPreservesNameForCutPaste)
{
    World world;

    Entity source = world.CreateEntity();
    world.AddComponent<NameComponent>(source).name = "Moved Entity";

    Entity pasted = kNullEntity;
    PasteEntitySnapshotCommand cmd(&world, CaptureEntitySnapshot(world, source), &pasted, false);
    cmd.Execute();

    ASSERT_TRUE(pasted.IsValid());
    const NameComponent* pastedName = world.GetComponent<NameComponent>(pasted);
    ASSERT_NE(pastedName, nullptr);
    EXPECT_EQ(pastedName->name, "Moved Entity");
}

TEST(EntityCommandTests, ClipboardEntriesPreserveSelectedHierarchyRelationships)
{
    World world;

    Entity parent = world.CreateEntity();
    world.AddComponent<NameComponent>(parent).name = "Parent";
    world.AddComponent<HierarchyComponent>(parent);

    Entity child = world.CreateEntity();
    world.AddComponent<NameComponent>(child).name = "Child";
    auto& childHierarchy = world.AddComponent<HierarchyComponent>(child);
    childHierarchy.parent = parent;
    world.GetComponent<HierarchyComponent>(parent)->children.push_back(child);

    const std::vector<EntityClipboardEntry> entries = CreateClipboardEntries(world, {parent, child});
    ASSERT_EQ(entries.size(), 2u);

    const HierarchyComponent* clipboardParentHierarchy = entries[0].snapshot.hierarchy ? &*entries[0].snapshot.hierarchy : nullptr;
    const HierarchyComponent* clipboardChildHierarchy = entries[1].snapshot.hierarchy ? &*entries[1].snapshot.hierarchy : nullptr;
    ASSERT_NE(clipboardParentHierarchy, nullptr);
    ASSERT_NE(clipboardChildHierarchy, nullptr);

    EXPECT_EQ(clipboardParentHierarchy->children.size(), 1u);
    EXPECT_EQ(clipboardParentHierarchy->children[0], child);
    EXPECT_EQ(clipboardChildHierarchy->parent, parent);
}

TEST(EntityCommandTests, PasteEntitiesCommandRemapsHierarchyWithinSelection)
{
    World world;

    Entity parent = world.CreateEntity();
    world.AddComponent<NameComponent>(parent).name = "Parent";
    world.AddComponent<HierarchyComponent>(parent);

    Entity child = world.CreateEntity();
    world.AddComponent<NameComponent>(child).name = "Child";
    auto& childHierarchy = world.AddComponent<HierarchyComponent>(child);
    childHierarchy.parent = parent;
    world.GetComponent<HierarchyComponent>(parent)->children.push_back(child);

    std::vector<Entity> pastedEntities;
    Entity pastedPrimary = kNullEntity;
    PasteEntitiesCommand cmd(&world, CreateClipboardEntries(world, {parent, child}), &pastedEntities, &pastedPrimary, true);
    cmd.Execute();

    ASSERT_EQ(pastedEntities.size(), 2u);
    ASSERT_TRUE(pastedPrimary.IsValid());

    const HierarchyComponent* pastedParentHierarchy = world.GetComponent<HierarchyComponent>(pastedEntities[0]);
    const HierarchyComponent* pastedChildHierarchy = world.GetComponent<HierarchyComponent>(pastedEntities[1]);
    ASSERT_NE(pastedParentHierarchy, nullptr);
    ASSERT_NE(pastedChildHierarchy, nullptr);
    ASSERT_EQ(pastedParentHierarchy->children.size(), 1u);

    EXPECT_EQ(pastedParentHierarchy->children[0], pastedEntities[1]);
    EXPECT_EQ(pastedChildHierarchy->parent, pastedEntities[0]);
}

TEST(EntityCommandTests, DeleteEntitiesCommandUndoRestoresHierarchyRelationships)
{
    World world;

    Entity parent = world.CreateEntity();
    world.AddComponent<NameComponent>(parent).name = "Parent";
    world.AddComponent<HierarchyComponent>(parent);

    Entity child = world.CreateEntity();
    world.AddComponent<NameComponent>(child).name = "Child";
    auto& childHierarchy = world.AddComponent<HierarchyComponent>(child);
    childHierarchy.parent = parent;
    world.GetComponent<HierarchyComponent>(parent)->children.push_back(child);

    Entity selectedEntity = child;
    DeleteEntitiesCommand cmd(&world, {parent, child}, &selectedEntity);
    cmd.Execute();

    EXPECT_FALSE(world.IsAlive(parent));
    EXPECT_FALSE(world.IsAlive(child));

    cmd.Undo();

    ASSERT_TRUE(selectedEntity.IsValid());
    ASSERT_TRUE(world.IsAlive(selectedEntity));

    Entity recreatedParent = kNullEntity;
    Entity recreatedChild = kNullEntity;
    world.EachEntity(
        [&](Entity entity)
        {
            const NameComponent* name = world.GetComponent<NameComponent>(entity);
            if (!name)
                return;

            if (name->name == "Parent")
                recreatedParent = entity;
            else if (name->name == "Child")
                recreatedChild = entity;
        });

    ASSERT_TRUE(recreatedParent.IsValid());
    ASSERT_TRUE(recreatedChild.IsValid());

    const HierarchyComponent* recreatedParentHierarchy = world.GetComponent<HierarchyComponent>(recreatedParent);
    const HierarchyComponent* recreatedChildHierarchy = world.GetComponent<HierarchyComponent>(recreatedChild);
    ASSERT_NE(recreatedParentHierarchy, nullptr);
    ASSERT_NE(recreatedChildHierarchy, nullptr);
    ASSERT_EQ(recreatedParentHierarchy->children.size(), 1u);

    EXPECT_EQ(recreatedParentHierarchy->children[0], recreatedChild);
    EXPECT_EQ(recreatedChildHierarchy->parent, recreatedParent);
}

TEST(EntityCommandTests, MultiTransformCommandAppliesAndUndoesAllEntities)
{
    World world;

    Entity first = world.CreateEntity();
    world.AddComponent<TransformComponent>(first).position = Vec3(0.0f, 0.0f, 0.0f);

    Entity second = world.CreateEntity();
    world.AddComponent<TransformComponent>(second).position = Vec3(2.0f, 0.0f, 0.0f);

    std::vector<EntityTransformState> before = {
        {first, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::One()},
        {second, Vec3(2.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::One()},
    };
    std::vector<EntityTransformState> after = {
        {first, Vec3(1.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::One()},
        {second, Vec3(3.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::One()},
    };

    MultiTransformCommand cmd(&world, before, after);
    cmd.Execute();

    EXPECT_EQ(world.GetComponent<TransformComponent>(first)->position, Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(world.GetComponent<TransformComponent>(second)->position, Vec3(3.0f, 0.0f, 0.0f));

    cmd.Undo();

    EXPECT_EQ(world.GetComponent<TransformComponent>(first)->position, Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(world.GetComponent<TransformComponent>(second)->position, Vec3(2.0f, 0.0f, 0.0f));
}
