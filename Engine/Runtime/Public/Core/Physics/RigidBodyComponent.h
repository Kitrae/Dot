#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec3.h"

namespace Dot
{

struct RigidBodyComponent
{
    // Basic properties
    float mass = 1.0f;
    float drag = 0.05f;        // Linear drag (air resistance)
    float angularDrag = 0.05f; // Rotational drag
    bool useGravity = true;
    bool isKinematic = false; // If true, not affected by physics forces

    // Physics material properties
    float friction = 0.5f;   // 0 = ice, 1 = sticky (how much it resists sliding)
    float bounciness = 0.3f; // 0 = no bounce, 1 = perfect bounce (restitution)

    // Constraints
    bool freezeRotation = false; // If true, rotation is not affected by physics

    // Runtime state (not serialized)
    Vec3 velocity = {0.0f, 0.0f, 0.0f};
    Vec3 angularVelocity = {0.0f, 0.0f, 0.0f};
    Vec3 forceAccumulator = {0.0f, 0.0f, 0.0f};
    Vec3 torqueAccumulator = {0.0f, 0.0f, 0.0f};
};

} // namespace Dot
