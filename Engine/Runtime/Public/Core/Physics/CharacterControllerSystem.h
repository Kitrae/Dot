#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/Math/Vec3.h"
#include "Core/Physics/CollisionMath.h"

#include <vector>

namespace Dot
{

class World;
class StaticWorldGeometry;

struct CharacterControllerTriggerPair
{
    Entity controllerEntity;
    Entity triggerEntity;
    ContactManifold manifold;
};

/// Character Controller System - handles kinematic character movement
class DOT_CORE_API CharacterControllerSystem
{
public:
    CharacterControllerSystem();
    ~CharacterControllerSystem();

    /// Update all character controllers
    void Update(World& world, float dt);

    /// Refresh trigger overlaps for character controllers after movement has been applied.
    void RefreshTriggerOverlaps(World& world);

    /// Move a specific character controller
    /// @param world The ECS world
    /// @param entity Entity with CharacterControllerComponent
    /// @param inputDirection Desired movement direction (normalized)
    /// @param sprint Whether sprinting
    /// @param jump Whether jump was pressed this frame
    void Move(World& world, Entity entity, const Vec3& inputDirection, bool sprint, bool jump, float dt);

    /// Check if character is grounded (convenience)
    bool IsGrounded(World& world, Entity entity);

    /// Get character velocity
    Vec3 GetVelocity(World& world, Entity entity);

    /// Set character velocity directly
    void SetVelocity(World& world, Entity entity, const Vec3& velocity);

    /// Teleport character (ignores collision)
    void Teleport(World& world, Entity entity, const Vec3& position);

    void SetStaticWorldGeometry(const StaticWorldGeometry* staticWorldGeometry)
    {
        m_StaticWorldGeometry = staticWorldGeometry;
    }

private:
    /// Perform ground detection
    void UpdateGroundState(World& world, Entity entity, float dt);

    /// Apply gravity to character
    void ApplyGravity(World& world, Entity entity, float dt);

    /// Handle jumping logic
    void ProcessJump(World& world, Entity entity, bool jumpPressed);

    /// Resolve collisions and slide along surfaces
    Vec3 SlideMove(World& world, Entity entity, const Vec3& desiredMove, float dt);

    /// Check and climb steps
    bool TryStepUp(World& world, Entity entity, const Vec3& moveDir, float dt);

    /// Handle steep slope sliding
    void HandleSlopeSliding(World& world, Entity entity, float dt);

    /// Push rigidbodies on contact
    void PushRigidbodies(World& world, Entity entity, const Vec3& hitNormal, Entity hitEntity);

public:
    const std::vector<CharacterControllerTriggerPair>& GetTriggerPairs() const { return m_TriggerPairs; }

private:
    std::vector<CharacterControllerTriggerPair> m_TriggerPairs;
    const StaticWorldGeometry* m_StaticWorldGeometry = nullptr;
};

} // namespace Dot
