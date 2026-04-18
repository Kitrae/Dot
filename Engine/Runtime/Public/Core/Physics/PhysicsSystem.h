#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/Math/Vec3.h"
#include "Core/Physics/CollisionMath.h"

#include <vector>

namespace Dot
{

class World;

struct CollisionPair
{
    Entity entityA;
    Entity entityB;
    ContactManifold manifold;
};

/// Physics System - handles rigid body dynamics and collision detection
/// Uses OBB collision with SAT algorithm and contact-based torque
class DOT_CORE_API PhysicsSystem
{
public:
    PhysicsSystem();
    ~PhysicsSystem();

    /// Initialize physics system
    void Initialize();

    /// Shutdown physics system
    void Shutdown();

    /// Update physics simulation
    /// @param world The ECS world containing physics components
    /// @param dt Delta time in seconds
    void Update(World& world, float dt);

    /// Access the resolved collision contacts from the most recent physics step.
    const std::vector<CollisionPair>& GetCollisionPairs() const { return m_CollisionPairs; }

private:
    void Integrate(World& world, float dt);
    void DetectCollisions(World& world);
    void ResolveCollisions(World& world, float dt);

    std::vector<CollisionPair> m_CollisionPairs;
    float m_Accumulator = 0.0f; // For fixed timestep physics
};

} // namespace Dot
