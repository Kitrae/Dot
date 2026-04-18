// =============================================================================
// Dot Engine - Node Unit Tests
// =============================================================================

#include "Core/ECS/World.h"
#include "Core/Math/Math.h"
#include "Core/Scene/Node.h"

#include <gtest/gtest.h>

using namespace Dot;

// =============================================================================
// Node Tests
// =============================================================================

class NodeTests : public ::testing::Test
{
protected:
    void SetUp() override { m_World = std::make_unique<World>(); }

    void TearDown() override { m_World.reset(); }

    Node CreateNode(const std::string& name = "")
    {
        Entity entity = m_World->CreateEntity();
        Node node(entity, m_World.get());
        if (!name.empty())
        {
            node.SetName(name);
        }
        return node;
    }

    std::unique_ptr<World> m_World;
};

TEST_F(NodeTests, Creation)
{
    Node node = CreateNode("TestNode");

    EXPECT_TRUE(node.IsValid());
    EXPECT_EQ(node.GetName(), "TestNode");
}

TEST_F(NodeTests, InvalidNode)
{
    Node invalidNode;

    EXPECT_FALSE(invalidNode.IsValid());
    EXPECT_EQ(invalidNode.GetName(), "");
}

TEST_F(NodeTests, Position)
{
    Node node = CreateNode();

    node.SetPosition(1.0f, 2.0f, 3.0f);
    Vec3 pos = node.GetPosition();

    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

TEST_F(NodeTests, Scale)
{
    Node node = CreateNode();

    node.SetScale(2.0f);
    Vec3 scale = node.GetScale();

    EXPECT_FLOAT_EQ(scale.x, 2.0f);
    EXPECT_FLOAT_EQ(scale.y, 2.0f);
    EXPECT_FLOAT_EQ(scale.z, 2.0f);
}

TEST_F(NodeTests, Rotation)
{
    Node node = CreateNode();

    Vec3 rot(0.0f, kHalfPi, 0.0f);
    node.SetRotation(rot);

    Vec3 result = node.GetRotation();
    EXPECT_FLOAT_EQ(result.x, rot.x);
    EXPECT_FLOAT_EQ(result.y, rot.y);
    EXPECT_FLOAT_EQ(result.z, rot.z);
}

TEST_F(NodeTests, ParentChild)
{
    Node parent = CreateNode("Parent");
    Node child = CreateNode("Child");

    child.SetParent(parent);

    EXPECT_EQ(parent.GetChildCount(), 1);
    EXPECT_TRUE(child.GetParent() == parent);
}

TEST_F(NodeTests, MultipleChildren)
{
    Node parent = CreateNode("Parent");
    Node child1 = CreateNode("Child1");
    Node child2 = CreateNode("Child2");
    Node child3 = CreateNode("Child3");

    // Use AddChild directly instead of SetParent to avoid archetype migration complexity
    parent.AddChild(child1);
    parent.AddChild(child2);
    parent.AddChild(child3);

    EXPECT_EQ(parent.GetChildCount(), 3);
}

TEST_F(NodeTests, RemoveChild)
{
    Node parent = CreateNode("Parent");
    Node child = CreateNode("Child");

    child.SetParent(parent);
    EXPECT_EQ(parent.GetChildCount(), 1);

    parent.RemoveChild(child);
    EXPECT_EQ(parent.GetChildCount(), 0);
}

TEST_F(NodeTests, ActiveState)
{
    Node node = CreateNode();

    EXPECT_TRUE(node.IsActive()); // Default active

    node.SetActive(false);
    EXPECT_FALSE(node.IsActive());

    node.SetActive(true);
    EXPECT_TRUE(node.IsActive());
}

TEST_F(NodeTests, ReparentChild)
{
    Node parent1 = CreateNode("Parent1");
    Node parent2 = CreateNode("Parent2");
    Node child = CreateNode("Child");

    child.SetParent(parent1);
    EXPECT_EQ(parent1.GetChildCount(), 1);
    EXPECT_EQ(parent2.GetChildCount(), 0);

    child.SetParent(parent2);
    EXPECT_EQ(parent1.GetChildCount(), 0);
    EXPECT_EQ(parent2.GetChildCount(), 1);
}
