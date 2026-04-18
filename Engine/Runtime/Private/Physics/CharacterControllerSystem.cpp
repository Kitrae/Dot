// =============================================================================
// Dot Engine - Character Controller System Implementation
// =============================================================================

#include "Core/Physics/CharacterControllerSystem.h"

#include "Core/ECS/World.h"
#include "Core/Map/StaticWorldGeometry.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/CollisionMath.h"
#include "Core/Physics/PhysicsSettings.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Dot
{

namespace
{

bool ShouldControllerCollide(const CharacterControllerComponent& controller, const BoxColliderComponent& collider)
{
    return CollisionLayers::Get().ShouldCollide(controller.collisionLayer, controller.collisionMask, collider.collisionLayer,
                                                collider.collisionMask);
}

bool ShouldControllerCollide(const CharacterControllerComponent& controller, const SphereColliderComponent& collider)
{
    return CollisionLayers::Get().ShouldCollide(controller.collisionLayer, controller.collisionMask, collider.collisionLayer,
                                                collider.collisionMask);
}

float Clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

struct StaticCollisionProbe
{
    Vec3 center = Vec3::Zero();
    float radius = 0.5f;
    bool valid = false;
};

struct DynamicControllerCollisionHit
{
    bool hit = false;
    Vec3 normal = Vec3::Zero();
    float depth = 0.0f;
    Entity otherEntity = kNullEntity;
};

Vec3 ClampHorizontalDisplacement(const Vec3& desiredMove, const Vec3& actualMove)
{
    const Vec3 desiredHorizontal(desiredMove.x, 0.0f, desiredMove.z);
    const Vec3 actualHorizontal(actualMove.x, 0.0f, actualMove.z);
    const float desiredHorizontalLengthSq = desiredHorizontal.LengthSquared();
    const float actualHorizontalLengthSq = actualHorizontal.LengthSquared();
    if (desiredHorizontalLengthSq <= 1e-8f || actualHorizontalLengthSq <= (desiredHorizontalLengthSq + 1e-6f))
        return actualMove;

    const float desiredHorizontalLength = std::sqrt(desiredHorizontalLengthSq);
    const float actualHorizontalLength = std::sqrt(actualHorizontalLengthSq);
    if (actualHorizontalLength <= 1e-6f)
        return actualMove;

    const float scale = desiredHorizontalLength / actualHorizontalLength;
    return Vec3(actualMove.x * scale, actualMove.y, actualMove.z * scale);
}

OBB BuildControllerOBBAtPosition(const TransformComponent& transform, const BoxColliderComponent& box, const Vec3& position)
{
    OBB obb;
    obb.center = position + box.center;
    obb.halfExtents =
        Vec3(box.size.x * 0.5f * transform.scale.x, box.size.y * 0.5f * transform.scale.y, box.size.z * 0.5f * transform.scale.z);
    obb.orientation = Mat3::FromEulerDegrees(transform.rotation.x, transform.rotation.y, transform.rotation.z);
    return obb;
}

Sphere BuildControllerSphereAtPosition(const TransformComponent& transform, const SphereColliderComponent& sphere, const Vec3& position)
{
    const float avgScale = (transform.scale.x + transform.scale.y + transform.scale.z) / 3.0f;
    Sphere result;
    result.center = position + sphere.center;
    result.radius = sphere.radius * avgScale;
    return result;
}

bool ShouldIgnoreControllerWallCollision(const Vec3& normal)
{
    return normal.y > 0.7f;
}

void ConsiderDynamicControllerCollision(const ContactManifold& manifold, const Vec3& controllerCenter, const Vec3& otherCenter,
                                        Entity otherEntity, DynamicControllerCollisionHit& ioHit)
{
    for (int contactIndex = 0; contactIndex < manifold.numContacts; ++contactIndex)
    {
        Vec3 normal = manifold.contacts[contactIndex].normal;
        if (normal.LengthSquared() <= 1e-8f)
            continue;

        if (Vec3::Dot(normal, controllerCenter - otherCenter) < 0.0f)
            normal = -normal;
        if (ShouldIgnoreControllerWallCollision(normal))
            continue;

        const float depth = manifold.contacts[contactIndex].depth;
        if (!ioHit.hit || depth > ioHit.depth)
        {
            ioHit.hit = true;
            ioHit.normal = normal;
            ioHit.depth = depth;
            ioHit.otherEntity = otherEntity;
        }
    }
}

bool FindDynamicControllerCollisionAtPosition(World& world, Entity entity, const TransformComponent& transform,
                                              const CharacterControllerComponent& controller, const BoxColliderComponent* myBox,
                                              const SphereColliderComponent* mySphere, const Vec3& position,
                                              DynamicControllerCollisionHit& outHit)
{
    outHit = {};
    if (!myBox && !mySphere)
        return false;

    if (myBox)
    {
        const OBB myOBB = BuildControllerOBBAtPosition(transform, *myBox, position);

        world.Each<TransformComponent, BoxColliderComponent>(
            [&](Entity otherEntity, TransformComponent& otherTransform, BoxColliderComponent& otherBox)
            {
                if (otherEntity == entity || otherBox.isTrigger || !ShouldControllerCollide(controller, otherBox))
                    return;

                ContactManifold manifold;
                if (!OBBvsOBB(myOBB, BuildControllerOBBAtPosition(otherTransform, otherBox, otherTransform.position), manifold) ||
                    manifold.numContacts <= 0)
                {
                    return;
                }

                ConsiderDynamicControllerCollision(manifold, myOBB.center, otherTransform.position + otherBox.center, otherEntity,
                                                   outHit);
            });

        world.Each<TransformComponent, SphereColliderComponent>(
            [&](Entity otherEntity, TransformComponent& otherTransform, SphereColliderComponent& otherSphere)
            {
                if (otherEntity == entity || otherSphere.isTrigger || !ShouldControllerCollide(controller, otherSphere))
                    return;

                ContactManifold manifold;
                if (!OBBvsSphere(myOBB, BuildControllerSphereAtPosition(otherTransform, otherSphere, otherTransform.position), manifold) ||
                    manifold.numContacts <= 0)
                {
                    return;
                }

                ConsiderDynamicControllerCollision(manifold, myOBB.center, otherTransform.position + otherSphere.center, otherEntity,
                                                   outHit);
            });
    }
    else
    {
        const Sphere myControllerSphere = BuildControllerSphereAtPosition(transform, *mySphere, position);

        world.Each<TransformComponent, BoxColliderComponent>(
            [&](Entity otherEntity, TransformComponent& otherTransform, BoxColliderComponent& otherBox)
            {
                if (otherEntity == entity || otherBox.isTrigger || !ShouldControllerCollide(controller, otherBox))
                    return;

                ContactManifold manifold;
                if (!OBBvsSphere(BuildControllerOBBAtPosition(otherTransform, otherBox, otherTransform.position), myControllerSphere,
                                 manifold) ||
                    manifold.numContacts <= 0)
                {
                    return;
                }

                ConsiderDynamicControllerCollision(manifold, myControllerSphere.center, otherTransform.position + otherBox.center,
                                                   otherEntity, outHit);
            });

        world.Each<TransformComponent, SphereColliderComponent>(
            [&](Entity otherEntity, TransformComponent& otherTransform, SphereColliderComponent& otherSphere)
            {
                if (otherEntity == entity || otherSphere.isTrigger || !ShouldControllerCollide(controller, otherSphere))
                    return;

                ContactManifold manifold;
                if (!SphereVsSphere(myControllerSphere, BuildControllerSphereAtPosition(otherTransform, otherSphere, otherTransform.position),
                                    manifold) ||
                    manifold.numContacts <= 0)
                {
                    return;
                }

                ConsiderDynamicControllerCollision(manifold, myControllerSphere.center, otherTransform.position + otherSphere.center,
                                                   otherEntity, outHit);
            });
    }

    return outHit.hit;
}

bool ResolveDynamicControllerPenetration(World& world, Entity entity, const TransformComponent& transform,
                                         const CharacterControllerComponent& controller, const BoxColliderComponent* myBox,
                                         const SphereColliderComponent* mySphere, Vec3& inOutPosition,
                                         DynamicControllerCollisionHit* outHit = nullptr)
{
    bool resolved = false;
    DynamicControllerCollisionHit lastHit;
    const float separationEpsilon = std::max(0.001f, controller.skinWidth * 0.5f);

    for (int iteration = 0; iteration < 4; ++iteration)
    {
        DynamicControllerCollisionHit hit;
        if (!FindDynamicControllerCollisionAtPosition(world, entity, transform, controller, myBox, mySphere, inOutPosition, hit))
            break;

        inOutPosition += hit.normal * (hit.depth + separationEpsilon);
        lastHit = hit;
        resolved = true;
    }

    if (resolved && outHit)
        *outHit = lastHit;
    return resolved;
}

StaticCollisionProbe BuildStaticCollisionProbe(const TransformComponent& transform, const BoxColliderComponent* box,
                                               const SphereColliderComponent* sphere)
{
    StaticCollisionProbe probe;

    if (sphere)
    {
        const float avgScale = (transform.scale.x + transform.scale.y + transform.scale.z) / 3.0f;
        probe.center =
            transform.position +
            Vec3(sphere->center.x * transform.scale.x, sphere->center.y * transform.scale.y, sphere->center.z * transform.scale.z);
        probe.radius = std::max(0.05f, sphere->radius * avgScale);
        probe.valid = true;
        return probe;
    }

    if (box)
    {
        const Vec3 halfExtents(box->size.x * 0.5f * transform.scale.x, box->size.y * 0.5f * transform.scale.y,
                               box->size.z * 0.5f * transform.scale.z);
        probe.radius = std::max(0.05f, std::min(halfExtents.x, halfExtents.z));
        probe.center =
            transform.position +
            Vec3(box->center.x * transform.scale.x, box->center.y * transform.scale.y, box->center.z * transform.scale.z);
        probe.center.y += -halfExtents.y + probe.radius;
        probe.valid = true;
    }

    return probe;
}

bool TryStaticWorldStepMove(const StaticWorldGeometry& staticWorld, const StaticCollisionProbe& probe,
                            const CharacterControllerComponent& controller, const Vec3& desiredMove, Vec3& outMove)
{
    if (!controller.enableStepping || controller.stepHeight <= 0.0f)
        return false;

    const Vec3 horizontalMove(desiredMove.x, 0.0f, desiredMove.z);
    if (horizontalMove.LengthSquared() <= 1e-6f)
        return false;

    const float collisionRadius = probe.radius + controller.skinWidth;
    const Vec3 stepUp(0.0f, controller.stepHeight, 0.0f);
    const Vec3 liftedMove = staticWorld.MoveSphereWithSliding(probe.center, collisionRadius, stepUp, controller.maxSlideIterations);
    if (liftedMove.y < controller.stepHeight * 0.5f)
        return false;

    const Vec3 liftedCenter = probe.center + liftedMove;
    Vec3 stepAdvance = horizontalMove;
    if (horizontalMove.LengthSquared() > 1e-6f)
        stepAdvance = stepAdvance + (horizontalMove.Normalized() * collisionRadius);

    const Vec3 steppedHorizontal =
        staticWorld.MoveSphereWithSliding(liftedCenter, collisionRadius, stepAdvance, controller.maxSlideIterations);
    if (Vec3(steppedHorizontal.x, 0.0f, steppedHorizontal.z).LengthSquared() <= 1e-6f)
        return false;

    Ray downRay;
    downRay.origin = liftedCenter + steppedHorizontal;
    downRay.direction = Vec3(0.0f, -1.0f, 0.0f);

    StaticWorldHit groundHit;
    const float maxDrop = controller.stepHeight + collisionRadius + controller.groundCheckDistance;
    if (!staticWorld.Raycast(downRay, maxDrop, groundHit) || groundHit.normal.y <= 0.1f)
        return false;

    const float downDistance = std::max(0.0f, groundHit.distance - collisionRadius);
    if (downDistance > controller.stepHeight + controller.groundCheckDistance)
        return false;

    outMove = liftedMove + steppedHorizontal - Vec3(0.0f, downDistance, 0.0f);
    return true;
}

} // namespace

CharacterControllerSystem::CharacterControllerSystem() {}
CharacterControllerSystem::~CharacterControllerSystem() {}

void CharacterControllerSystem::Update(World& world, float dt)
{
    m_TriggerPairs.clear();

    // Update all character controllers (ground state, gravity, etc.)
    world.Each<TransformComponent, CharacterControllerComponent>(
        [&](Entity entity, TransformComponent&, CharacterControllerComponent& cc)
        {
            UpdateGroundState(world, entity, dt);

            if (cc.useGravity && !cc.isGrounded)
            {
                ApplyGravity(world, entity, dt);
            }

            // Update slope sliding
            if (cc.isSliding && cc.slideOnSteepSlopes)
            {
                HandleSlopeSliding(world, entity, dt);
            }

            // Update coyote time
            if (!cc.isGrounded)
            {
                cc.timeSinceGrounded += dt;
            }

            // Update jump buffer
            if (cc.jumpBufferTimer > 0.0f)
            {
                cc.jumpBufferTimer -= dt;
            }
        });
}

void CharacterControllerSystem::RefreshTriggerOverlaps(World& world)
{
    m_TriggerPairs.clear();

    world.Each<TransformComponent, CharacterControllerComponent>(
        [&](Entity entity, TransformComponent& transform, CharacterControllerComponent& controller)
        {
            const auto* box = world.GetComponent<BoxColliderComponent>(entity);
            const auto* sphere = world.GetComponent<SphereColliderComponent>(entity);
            if (!box && !sphere)
                return;

            if (box)
            {
                OBB controllerObb;
                controllerObb.center = transform.position;
                controllerObb.halfExtents =
                    Vec3(box->size.x * 0.5f * transform.scale.x, box->size.y * 0.5f * transform.scale.y,
                         box->size.z * 0.5f * transform.scale.z);
                controllerObb.orientation =
                    Mat3::FromEulerDegrees(transform.rotation.x, transform.rotation.y, transform.rotation.z);

                world.Each<TransformComponent, BoxColliderComponent>(
                    [&](Entity otherEntity, TransformComponent& otherTransform, BoxColliderComponent& otherBox)
                    {
                        if (otherEntity == entity || !otherBox.isTrigger || !ShouldControllerCollide(controller, otherBox))
                            return;

                        OBB triggerObb;
                        triggerObb.center = otherTransform.position;
                        triggerObb.halfExtents =
                            Vec3(otherBox.size.x * 0.5f * otherTransform.scale.x, otherBox.size.y * 0.5f * otherTransform.scale.y,
                                 otherBox.size.z * 0.5f * otherTransform.scale.z);
                        triggerObb.orientation =
                            Mat3::FromEulerDegrees(otherTransform.rotation.x, otherTransform.rotation.y, otherTransform.rotation.z);

                        ContactManifold manifold;
                        if (OBBvsOBB(controllerObb, triggerObb, manifold) && manifold.numContacts > 0)
                        {
                            m_TriggerPairs.push_back({entity, otherEntity, manifold});
                        }
                    });

                world.Each<TransformComponent, SphereColliderComponent>(
                    [&](Entity otherEntity, TransformComponent& otherTransform, SphereColliderComponent& otherSphere)
                    {
                        if (otherEntity == entity || !otherSphere.isTrigger ||
                            !ShouldControllerCollide(controller, otherSphere))
                        {
                            return;
                        }

                        const float avgScale =
                            (otherTransform.scale.x + otherTransform.scale.y + otherTransform.scale.z) / 3.0f;
                        Sphere triggerSphere;
                        triggerSphere.center = otherTransform.position + otherSphere.center;
                        triggerSphere.radius = otherSphere.radius * avgScale;

                        ContactManifold manifold;
                        if (OBBvsSphere(controllerObb, triggerSphere, manifold) && manifold.numContacts > 0)
                        {
                            m_TriggerPairs.push_back({entity, otherEntity, manifold});
                        }
                    });

                return;
            }

            const float avgScale = (transform.scale.x + transform.scale.y + transform.scale.z) / 3.0f;
            Sphere controllerSphere;
            controllerSphere.center = transform.position + sphere->center;
            controllerSphere.radius = sphere->radius * avgScale;

            world.Each<TransformComponent, BoxColliderComponent>(
                [&](Entity otherEntity, TransformComponent& otherTransform, BoxColliderComponent& otherBox)
                {
                    if (otherEntity == entity || !otherBox.isTrigger || !ShouldControllerCollide(controller, otherBox))
                        return;

                    OBB triggerObb;
                    triggerObb.center = otherTransform.position;
                    triggerObb.halfExtents =
                        Vec3(otherBox.size.x * 0.5f * otherTransform.scale.x, otherBox.size.y * 0.5f * otherTransform.scale.y,
                             otherBox.size.z * 0.5f * otherTransform.scale.z);
                    triggerObb.orientation =
                        Mat3::FromEulerDegrees(otherTransform.rotation.x, otherTransform.rotation.y, otherTransform.rotation.z);

                    ContactManifold manifold;
                    if (OBBvsSphere(triggerObb, controllerSphere, manifold) && manifold.numContacts > 0)
                    {
                        m_TriggerPairs.push_back({entity, otherEntity, manifold});
                    }
                });

            world.Each<TransformComponent, SphereColliderComponent>(
                [&](Entity otherEntity, TransformComponent& otherTransform, SphereColliderComponent& otherSphere)
                {
                    if (otherEntity == entity || !otherSphere.isTrigger ||
                        !ShouldControllerCollide(controller, otherSphere))
                    {
                        return;
                    }

                    const float otherAvgScale =
                        (otherTransform.scale.x + otherTransform.scale.y + otherTransform.scale.z) / 3.0f;
                    Sphere triggerSphere;
                    triggerSphere.center = otherTransform.position + otherSphere.center;
                    triggerSphere.radius = otherSphere.radius * otherAvgScale;

                    ContactManifold manifold;
                    if (SphereVsSphere(controllerSphere, triggerSphere, manifold) && manifold.numContacts > 0)
                    {
                        m_TriggerPairs.push_back({entity, otherEntity, manifold});
                    }
                });
        });
}

void CharacterControllerSystem::Move(World& world, Entity entity, const Vec3& inputDirection, bool sprint, bool jump,
                                     float dt)
{
    auto* transform = world.GetComponent<TransformComponent>(entity);
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);

    if (!transform || !cc)
        return;

    cc->isSprinting = sprint;

    // Handle jumping
    if (jump)
    {
        cc->jumpBufferTimer = cc->jumpBufferTime;
    }
    ProcessJump(world, entity, cc->jumpBufferTimer > 0.0f);

    // Calculate movement speed
    float speed = cc->moveSpeed;
    if (sprint)
        speed *= cc->sprintMultiplier;

    // Simple direct movement - just set horizontal velocity from input
    if (inputDirection.LengthSquared() > 0.001f)
    {
        Vec3 horizontalVelocity(inputDirection.x * speed, 0.0f, inputDirection.z * speed);

        // On walkable slopes, reduce the horizontal component so the resulting
        // movement along the surface stays near the configured move speed.
        if (cc->isGrounded && cc->maintainVelocityOnSlopes && !cc->isSliding && cc->groundNormal.y > 0.001f &&
            cc->groundNormal.y < 0.999f)
        {
            Vec3 slopeDirection = horizontalVelocity - (cc->groundNormal * Vec3::Dot(horizontalVelocity, cc->groundNormal));
            if (slopeDirection.LengthSquared() > 1e-6f)
            {
                slopeDirection = slopeDirection.Normalized() * speed;
                horizontalVelocity.x = slopeDirection.x;
                horizontalVelocity.z = slopeDirection.z;
            }
        }

        cc->velocity.x = horizontalVelocity.x;
        cc->velocity.z = horizontalVelocity.z;
    }
    else if (cc->isGrounded)
    {
        // No input on ground - stop immediately (friction)
        cc->velocity.x = 0.0f;
        cc->velocity.z = 0.0f;
    }
    // In air with no input - keep current velocity (momentum)

    // Calculate desired movement
    Vec3 desiredMove = cc->velocity * dt;

    // Collision sliding
    Vec3 actualMove = SlideMove(world, entity, desiredMove, dt);
    actualMove = ClampHorizontalDisplacement(desiredMove, actualMove);

    if (dt > 1e-6f)
    {
        cc->velocity.x = actualMove.x / dt;
        cc->velocity.z = actualMove.z / dt;
    }

    // Apply movement
    transform->position = transform->position + actualMove;
}

bool CharacterControllerSystem::IsGrounded(World& world, Entity entity)
{
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    return cc ? cc->isGrounded : false;
}

Vec3 CharacterControllerSystem::GetVelocity(World& world, Entity entity)
{
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    return cc ? cc->velocity : Vec3::Zero();
}

void CharacterControllerSystem::SetVelocity(World& world, Entity entity, const Vec3& velocity)
{
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    if (cc)
        cc->velocity = velocity;
}

void CharacterControllerSystem::Teleport(World& world, Entity entity, const Vec3& position)
{
    auto* transform = world.GetComponent<TransformComponent>(entity);
    if (transform)
        transform->position = position;
}

void CharacterControllerSystem::UpdateGroundState(World& world, Entity entity, float)
{
    auto* transform = world.GetComponent<TransformComponent>(entity);
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    auto* box = world.GetComponent<BoxColliderComponent>(entity);
    auto* sphere = world.GetComponent<SphereColliderComponent>(entity);

    if (!transform || !cc)
        return;

    cc->wasGrounded = cc->isGrounded;
    cc->isGrounded = false;
    cc->groundNormal = Vec3::Up();
    cc->isSliding = false;

    // Calculate raycast origin (bottom of collider)
    Vec3 rayOrigin = transform->position;
    float colliderBottomOffset = 0.0f;

    if (box)
    {
        colliderBottomOffset = box->size.y * 0.5f * transform->scale.y;
    }
    else if (sphere)
    {
        float avgScale = (transform->scale.x + transform->scale.y + transform->scale.z) / 3.0f;
        colliderBottomOffset = sphere->radius * avgScale;
    }

    rayOrigin.y -= colliderBottomOffset;

    Ray groundRay;
    groundRay.origin = rayOrigin;
    groundRay.direction = Vec3(0.0f, -1.0f, 0.0f);

    float checkDist = cc->groundCheckDistance + cc->skinWidth;
    RaycastHit hit;
    bool foundGround = false;
    float bestGroundDistance = std::numeric_limits<float>::max();

    // Check against all box colliders
    world.Each<TransformComponent, BoxColliderComponent>(
        [&](Entity otherEntity, TransformComponent& otherTransform, BoxColliderComponent& otherBox)
        {
            if (otherEntity == entity)
                return;
            if (otherBox.isTrigger)
                return;
            if (!ShouldControllerCollide(*cc, otherBox))
                return;

            OBB obb;
            obb.center = otherTransform.position;
            obb.halfExtents =
                Vec3(otherBox.size.x * 0.5f * otherTransform.scale.x, otherBox.size.y * 0.5f * otherTransform.scale.y,
                     otherBox.size.z * 0.5f * otherTransform.scale.z);
            obb.orientation =
                Mat3::FromEulerDegrees(otherTransform.rotation.x, otherTransform.rotation.y, otherTransform.rotation.z);

            RaycastHit thisHit;
            if (RaycastVsOBB(groundRay, obb, checkDist, thisHit))
            {
                if (!foundGround || thisHit.distance < hit.distance)
                {
                    hit = thisHit;
                    foundGround = true;
                    bestGroundDistance = thisHit.distance;
                }
            }
        });

    if (foundGround)
    {
        cc->isGrounded = true;
        cc->groundNormal = hit.normal;
        cc->timeSinceGrounded = 0.0f;
        cc->jumpsRemaining = cc->maxJumps;

        // Check if slope is too steep
        float slopeAngle = std::acos(std::clamp(Vec3::Dot(hit.normal, Vec3::Up()), -1.0f, 1.0f)) * 57.2957795f;
        if (slopeAngle > cc->slopeLimit)
        {
            cc->isSliding = true;
            cc->isGrounded = false; // Can't stand on steep slopes
        }

        // Snap to ground if falling slowly
        if (cc->velocity.y <= 0.0f && hit.distance > 0.001f)
        {
            auto* t = world.GetComponent<TransformComponent>(entity);
            if (t)
            {
                t->position.y -= hit.distance - 0.001f;
            }
        }
        if (cc->velocity.y <= 0.0f)
            cc->velocity.y = 0.0f;
    }

    if (m_StaticWorldGeometry && m_StaticWorldGeometry->HasGeometry())
    {
        const StaticCollisionProbe probe = BuildStaticCollisionProbe(*transform, box, sphere);
        if (probe.valid)
        {
            StaticWorldHit worldHit;
            float worldGroundDistance = 0.0f;
            bool foundStaticGround = false;

            StaticWorldHit overlapHit;
            if (m_StaticWorldGeometry->OverlapSphere(probe.center, probe.radius + cc->skinWidth, overlapHit) && overlapHit.normal.y > 0.1f)
            {
                worldHit = overlapHit;
                worldGroundDistance = 0.0f;
                foundStaticGround = true;
            }
            else
            {
                Ray staticGroundRay;
                staticGroundRay.origin = probe.center;
                staticGroundRay.direction = Vec3(0.0f, -1.0f, 0.0f);

                const float staticCheckDist = probe.radius + cc->groundCheckDistance + cc->skinWidth;
                StaticWorldHit rayHit;
                if (m_StaticWorldGeometry->Raycast(staticGroundRay, staticCheckDist, rayHit))
                {
                    worldHit = rayHit;
                    worldGroundDistance = std::max(0.0f, rayHit.distance - probe.radius);
                    foundStaticGround = true;
                }
            }

            if (foundStaticGround && worldGroundDistance < bestGroundDistance)
            {
                cc->isGrounded = true;
                cc->groundNormal = worldHit.normal;
                cc->timeSinceGrounded = 0.0f;
                cc->jumpsRemaining = cc->maxJumps;

                const float slopeAngle =
                    std::acos(std::clamp(Vec3::Dot(worldHit.normal, Vec3::Up()), -1.0f, 1.0f)) * 57.2957795f;
                if (slopeAngle > cc->slopeLimit)
                {
                    cc->isSliding = true;
                    cc->isGrounded = false;
                }

                if (cc->velocity.y <= 0.0f && worldGroundDistance > 0.001f)
                {
                    transform->position.y -= worldGroundDistance - 0.001f;
                }
                if (cc->velocity.y <= 0.0f)
                    cc->velocity.y = 0.0f;
            }
        }
    }
}

void CharacterControllerSystem::ApplyGravity(World& world, Entity entity, float dt)
{
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    if (!cc)
        return;

    auto& settings = PhysicsSettings::Get();
    cc->velocity.y += settings.gravity.y * cc->gravityMultiplier * dt;
}

void CharacterControllerSystem::ProcessJump(World& world, Entity entity, bool jumpPressed)
{
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    if (!cc)
        return;

    bool canJump = cc->jumpsRemaining > 0 &&
                   (cc->isGrounded || cc->timeSinceGrounded < cc->coyoteTime || cc->jumpsRemaining < cc->maxJumps);

    if (jumpPressed && canJump)
    {
        // Calculate jump velocity from height: v = sqrt(2 * g * h)
        auto& settings = PhysicsSettings::Get();
        float gravity = std::abs(settings.gravity.y) * cc->gravityMultiplier;
        float jumpVel = std::sqrt(2.0f * gravity * cc->jumpHeight);

        cc->velocity.y = jumpVel;
        cc->jumpsRemaining--;
        cc->isGrounded = false;
        cc->jumpBufferTimer = 0.0f; // Consume buffer
    }
}

Vec3 CharacterControllerSystem::SlideMove(World& world, Entity entity, const Vec3& desiredMove, float)
{
    auto* transform = world.GetComponent<TransformComponent>(entity);
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    auto* myBox = world.GetComponent<BoxColliderComponent>(entity);
    auto* mySphere = world.GetComponent<SphereColliderComponent>(entity);

    if (!transform || !cc)
        return desiredMove;

    if (!cc->slideAlongWalls)
        return desiredMove;

    Vec3 remainingMove = desiredMove;
    Vec3 totalMove = Vec3::Zero();

    if (myBox || mySphere)
    {
        for (int iter = 0; iter < cc->maxSlideIterations && remainingMove.LengthSquared() > 0.0001f; ++iter)
        {
            Vec3 currentPosition = transform->position + totalMove;
            ResolveDynamicControllerPenetration(world, entity, *transform, *cc, myBox, mySphere, currentPosition);
            totalMove = currentPosition - transform->position;

            DynamicControllerCollisionHit collisionHit;
            if (!FindDynamicControllerCollisionAtPosition(world, entity, *transform, *cc, myBox, mySphere,
                                                          currentPosition + remainingMove, collisionHit))
            {
                totalMove += remainingMove;
                break;
            }

            float minT = 0.0f;
            float maxT = 1.0f;
            for (int binaryStep = 0; binaryStep < 10; ++binaryStep)
            {
                const float t = (minT + maxT) * 0.5f;
                DynamicControllerCollisionHit overlapAtT;
                if (FindDynamicControllerCollisionAtPosition(world, entity, *transform, *cc, myBox, mySphere,
                                                             currentPosition + (remainingMove * t), overlapAtT))
                {
                    maxT = t;
                    collisionHit = overlapAtT;
                }
                else
                {
                    minT = t;
                }
            }

            const float safeT = Clamp01(minT - 0.001f);
            if (safeT > 1e-6f)
            {
                totalMove += remainingMove * safeT;
                currentPosition = transform->position + totalMove;
                ResolveDynamicControllerPenetration(world, entity, *transform, *cc, myBox, mySphere, currentPosition);
                totalMove = currentPosition - transform->position;
            }

            if (cc->pushRigidbodies && collisionHit.otherEntity.IsValid())
                PushRigidbodies(world, entity, collisionHit.normal, collisionHit.otherEntity);

            const Vec3 unresolved = remainingMove * (1.0f - safeT);
            const float intoSurface = Vec3::Dot(unresolved, collisionHit.normal);
            Vec3 nextRemaining = intoSurface < 0.0f ? (unresolved - (collisionHit.normal * intoSurface)) : unresolved;

            if (safeT <= 1e-6f && nextRemaining.LengthSquared() >= remainingMove.LengthSquared() - 1e-6f)
                break;

            remainingMove = nextRemaining;
        }
    }

    if (m_StaticWorldGeometry && m_StaticWorldGeometry->HasGeometry())
    {
        const StaticCollisionProbe probe = BuildStaticCollisionProbe(*transform, myBox, mySphere);
        if (probe.valid)
        {
            StaticWorldHit worldHit;
            Vec3 staticMove = m_StaticWorldGeometry->MoveSphereWithSliding(probe.center, probe.radius + cc->skinWidth, totalMove,
                                                                           cc->maxSlideIterations, &worldHit);

            const Vec3 desiredHorizontal(totalMove.x, 0.0f, totalMove.z);
            const Vec3 actualHorizontal(staticMove.x, 0.0f, staticMove.z);
            if (cc->enableStepping && worldHit.hit && worldHit.normal.y < 0.5f && desiredHorizontal.LengthSquared() > 1e-6f &&
                actualHorizontal.LengthSquared() + 1e-4f < desiredHorizontal.LengthSquared() &&
                std::abs(staticMove.y) < (cc->stepHeight * 0.35f))
            {
                Vec3 steppedMove;
                if (TryStaticWorldStepMove(*m_StaticWorldGeometry, probe, *cc, totalMove, steppedMove))
                {
                    const Vec3 steppedHorizontal(steppedMove.x, 0.0f, steppedMove.z);
                    if (steppedHorizontal.LengthSquared() > actualHorizontal.LengthSquared())
                        staticMove = steppedMove;
                }
            }

            totalMove = staticMove;
        }
    }

    return totalMove;
}

bool CharacterControllerSystem::TryStepUp(World&, Entity, const Vec3&, float)
{
    // Step climbing - TODO: implement
    return false;
}

void CharacterControllerSystem::HandleSlopeSliding(World& world, Entity entity, float dt)
{
    auto* cc = world.GetComponent<CharacterControllerComponent>(entity);
    if (!cc || !cc->slideOnSteepSlopes)
        return;

    // Calculate slide direction (down the slope)
    Vec3 slideDir = cc->groundNormal;
    slideDir.y = 0.0f;
    if (slideDir.LengthSquared() > 0.001f)
    {
        slideDir = slideDir.Normalized();
        cc->velocity = cc->velocity + slideDir * cc->slideSpeed * dt;
    }
}

void CharacterControllerSystem::PushRigidbodies(World& world, Entity, const Vec3& hitNormal, Entity hitEntity)
{
    auto* rb = world.GetComponent<RigidBodyComponent>(hitEntity);
    auto* cc = world.GetComponent<CharacterControllerComponent>(hitEntity);

    // Only push dynamic rigidbodies (not kinematic, not other controllers)
    if (rb && !rb->isKinematic && !cc)
    {
        auto* pusherCc = world.GetComponent<CharacterControllerComponent>(hitEntity);
        float force = pusherCc ? pusherCc->pushForce : 2.0f;

        rb->velocity = rb->velocity - hitNormal * force;
    }
}

} // namespace Dot
