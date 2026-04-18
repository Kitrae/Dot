// =============================================================================
// Dot Engine - Reflection Unit Tests
// =============================================================================

#include "Core/Reflect/Registry.h"
#include "Core/Reflect/Serialization.h"

#include <gtest/gtest.h>

using namespace Dot;

// =============================================================================
// Test Types
// =============================================================================

struct TestComponent
{
    float x = 0;
    float y = 0;
    float z = 0;
    int32 health = 100;
    bool active = true;
};

struct DerivedComponent : TestComponent
{
    float extra = 42.0f;
};

struct SmallUnsignedComponent
{
    uint8 layer = 0;
    uint8 sentinel = 99;
};

// =============================================================================
// Property Tests
// =============================================================================

TEST(PropertyTests, CreateProperty)
{
    Property prop = Property::Create("x", &TestComponent::x, PropertyType::Float);

    EXPECT_EQ(prop.name, "x");
    EXPECT_EQ(prop.type, PropertyType::Float);
    EXPECT_EQ(prop.size, sizeof(float));
}

TEST(PropertyTests, GetterSetter)
{
    Property prop = Property::Create("health", &TestComponent::health, PropertyType::Int32);

    TestComponent obj;
    obj.health = 50;

    // Test getter
    int32* value = static_cast<int32*>(prop.getter(&obj));
    EXPECT_EQ(*value, 50);

    // Test setter
    int32 newValue = 75;
    prop.setter(&obj, &newValue);
    EXPECT_EQ(obj.health, 75);
}

TEST(PropertyTests, Flags)
{
    PropertyFlags flags = PropertyFlags::ReadOnly | PropertyFlags::Hidden;

    EXPECT_TRUE(HasFlag(flags, PropertyFlags::ReadOnly));
    EXPECT_TRUE(HasFlag(flags, PropertyFlags::Hidden));
    EXPECT_FALSE(HasFlag(flags, PropertyFlags::Transient));
}

// =============================================================================
// TypeInfo Tests
// =============================================================================

TEST(TypeInfoTests, Registration)
{
    // Each test registers type fresh since tests run in separate processes
    TypeRegistry::Get()
        .Register<TestComponent>("TestComponent")
        .Property("x", &TestComponent::x, PropertyType::Float)
        .Property("y", &TestComponent::y, PropertyType::Float)
        .Property("z", &TestComponent::z, PropertyType::Float)
        .Property("health", &TestComponent::health, PropertyType::Int32)
        .Property("active", &TestComponent::active, PropertyType::Bool);

    const TypeInfo* info = TypeRegistry::Get().GetType("TestComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "TestComponent");
    EXPECT_EQ(info->size, sizeof(TestComponent));
    EXPECT_EQ(info->properties.size(), 5);
}

TEST(TypeInfoTests, GetProperty)
{
    // Register type for this test
    TypeRegistry::Get()
        .Register<TestComponent>("TestComponent")
        .Property("health", &TestComponent::health, PropertyType::Int32);

    const TypeInfo* info = TypeRegistry::Get().GetType("TestComponent");
    ASSERT_NE(info, nullptr);

    const Property* prop = info->GetProperty("health");
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->type, PropertyType::Int32);

    EXPECT_EQ(info->GetProperty("nonexistent"), nullptr);
}

TEST(TypeInfoTests, CreateInstance)
{
    // Register type for this test
    TypeRegistry::Get().Register<TestComponent>("TestComponent");

    void* instance = TypeRegistry::Get().CreateInstance("TestComponent");
    ASSERT_NE(instance, nullptr);

    TestComponent* obj = static_cast<TestComponent*>(instance);
    EXPECT_EQ(obj->health, 100); // Default value

    TypeRegistry::Get().DestroyInstance("TestComponent", instance);
}

// =============================================================================
// Registry Tests
// =============================================================================

TEST(RegistryTests, HasType)
{
    // Register type for this test
    TypeRegistry::Get().Register<TestComponent>("TestComponent");

    EXPECT_TRUE(TypeRegistry::Get().HasType("TestComponent"));
    EXPECT_FALSE(TypeRegistry::Get().HasType("NonexistentType"));
}

TEST(RegistryTests, Inheritance)
{
    // Register parent type
    TypeRegistry::Get()
        .Register<TestComponent>("TestComponent")
        .Property("health", &TestComponent::health, PropertyType::Int32);

    const TypeInfo* parentInfo = TypeRegistry::Get().GetType("TestComponent");

    // Register derived type
    TypeRegistry::Get()
        .Register<DerivedComponent>("DerivedComponent")
        .Parent(parentInfo)
        .Property("extra", &DerivedComponent::extra, PropertyType::Float);

    const TypeInfo* derivedInfo = TypeRegistry::Get().GetType("DerivedComponent");
    ASSERT_NE(derivedInfo, nullptr);
    EXPECT_EQ(derivedInfo->parent, parentInfo);

    // Can access parent properties
    EXPECT_NE(derivedInfo->GetProperty("health"), nullptr);
    EXPECT_NE(derivedInfo->GetProperty("extra"), nullptr);
}

TEST(SerializationTests, UInt32PropertySupportsUint8Storage)
{
    TypeRegistry::Get()
        .Register<SmallUnsignedComponent>("SmallUnsignedComponent")
        .Property("layer", &SmallUnsignedComponent::layer, PropertyType::UInt32)
        .Property("sentinel", &SmallUnsignedComponent::sentinel, PropertyType::UInt32);

    const TypeInfo* info = TypeRegistry::Get().GetType("SmallUnsignedComponent");
    ASSERT_NE(info, nullptr);

    SmallUnsignedComponent obj;
    obj.layer = 0;
    obj.sentinel = 99;

    const Property* layerProp = info->GetProperty("layer");
    ASSERT_NE(layerProp, nullptr);
    EXPECT_TRUE(DeserializePropertyFromJson(*layerProp, &obj, "4"));
    EXPECT_EQ(obj.layer, 4);
    EXPECT_EQ(obj.sentinel, 99);

    std::ostringstream out;
    SerializePropertyToJson(out, *layerProp, &obj);
    EXPECT_EQ(out.str(), "4");
}
