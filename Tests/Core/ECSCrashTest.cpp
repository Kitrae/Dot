
#include "Core/ECS/World.h"
#include "Core/Scene/Components.h"

#include <gtest/gtest.h>


using namespace Dot;

TEST(ECSCrashTest, TestWorldDestruction)
{
    // Replicates NewScene -> ResetWorld logic
    {
        auto world = std::make_unique<World>();

        // Add typical default entities
        Entity cube = world->CreateEntity();

        // Add NameComponent (std::string) - non-trivial destructor
        auto& name = world->AddComponent<NameComponent>(cube);
        name.name = "Cube";

        // Add TransformComponent
        auto& transform = world->AddComponent<TransformComponent>(cube);
        transform.position = Vec3(0.0f, 0.0f, 0.0f);
        transform.rotation = Vec3(0.0f, 0.0f, 0.0f);
        transform.scale = Vec3(1.0f, 1.0f, 1.0f);

        // Ensure another entity exists
        Entity empty = world->CreateEntity();
        world->AddComponent<NameComponent>(empty).name = "New Entity";
        world->AddComponent<TransformComponent>(empty);

        // World destructor called here -> calls World::Clear()
    }

    // If we survived, success
    SUCCEED();
}
