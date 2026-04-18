#include "Core/Physics/PhysicsSystem.h"

#include "Core/ECS/World.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Math/Vec3.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/CollisionMath.h"
#include "Core/Physics/PhysicsSettings.h" // Already included, but ensuring its presence
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Scene/Components.h"

#include <cmath>
#include <vector>

namespace Dot
{

// Helper: Build OBB from transform and box collider
static OBB BuildOBB(const TransformComponent& t, const BoxColliderComponent& box)
{
    OBB obb;
    obb.center = Vec3(t.position.x + box.center.x, t.position.y + box.center.y, t.position.z + box.center.z);
    obb.halfExtents = Vec3(box.size.x * 0.5f * t.scale.x, box.size.y * 0.5f * t.scale.y, box.size.z * 0.5f * t.scale.z);
    obb.orientation = Mat3::FromEulerDegrees(t.rotation.x, t.rotation.y, t.rotation.z);
    return obb;
}

// Helper: Build Sphere from transform and sphere collider
static Sphere BuildSphere(const TransformComponent& t, const SphereColliderComponent& sphereComp)
{
    float avgScale = (t.scale.x + t.scale.y + t.scale.z) / 3.0f;
    Sphere result;
    result.center = Vec3(t.position.x + sphereComp.center.x, t.position.y + sphereComp.center.y,
                         t.position.z + sphereComp.center.z);
    result.radius = sphereComp.radius * avgScale;
    return result;
}

// Data for a collision detection job
struct CollisionJobData
{
    struct PhysicsEntity
    {
        Entity entity;
        TransformComponent* transform;
        RigidBodyComponent* rb;
        BoxColliderComponent* box;
        SphereColliderComponent* sphere;
    };

    const std::vector<PhysicsEntity>* entities;
    size_t startI;
    size_t endI;
    std::vector<CollisionPair> results;
};

static uint8 GetCollisionLayer(const CollisionJobData::PhysicsEntity& entity)
{
    if (entity.box)
        return entity.box->collisionLayer;
    if (entity.sphere)
        return entity.sphere->collisionLayer;
    return 0;
}

static uint32 GetCollisionMask(const CollisionJobData::PhysicsEntity& entity)
{
    if (entity.box)
        return entity.box->collisionMask;
    if (entity.sphere)
        return entity.sphere->collisionMask;
    return CollisionLayers::kAllLayersMask;
}

// Job function for collision detection
static void DetectCollisionsJob(void* userData)
{
    auto* data = static_cast<CollisionJobData*>(userData);
    const auto& entities = *data->entities;
    auto& collisionLayers = CollisionLayers::Get();

    for (size_t i = data->startI; i < data->endI; ++i)
    {
        for (size_t j = i + 1; j < entities.size(); ++j)
        {
            auto& a = entities[i];
            auto& b = entities[j];

            if (a.rb->isKinematic && b.rb->isKinematic)
                continue;

            if (!collisionLayers.ShouldCollide(GetCollisionLayer(a), GetCollisionMask(a), GetCollisionLayer(b),
                                               GetCollisionMask(b)))
            {
                continue;
            }

            ContactManifold manifold;
            bool collision = false;

            if (a.box && b.box)
            {
                collision = OBBvsOBB(BuildOBB(*a.transform, *a.box), BuildOBB(*b.transform, *b.box), manifold);
            }
            else if (a.sphere && b.sphere)
            {
                collision = SphereVsSphere(BuildSphere(*a.transform, *a.sphere), BuildSphere(*b.transform, *b.sphere),
                                           manifold);
            }
            else if (a.box && b.sphere)
            {
                collision = OBBvsSphere(BuildOBB(*a.transform, *a.box), BuildSphere(*b.transform, *b.sphere), manifold);
                for (int c = 0; c < manifold.numContacts; c++)
                    manifold.contacts[c].normal = -manifold.contacts[c].normal;
            }
            else if (a.sphere && b.box)
            {
                collision = OBBvsSphere(BuildOBB(*b.transform, *b.box), BuildSphere(*a.transform, *a.sphere), manifold);
                for (int c = 0; c < manifold.numContacts; c++)
                    manifold.contacts[c].normal = -manifold.contacts[c].normal;
            }

            if (collision && manifold.numContacts > 0)
            {
                CollisionPair pair;
                pair.entityA = a.entity;
                pair.entityB = b.entity;
                pair.manifold = manifold;
                data->results.push_back(pair);
            }
        }
    }
}

// Inertia tensor for a box
static Vec3 GetBoxInertiaTensor(float mass, const Vec3& halfExtents)
{
    float w = halfExtents.x * 2.0f;
    float h = halfExtents.y * 2.0f;
    float d = halfExtents.z * 2.0f;
    return Vec3((1.0f / 12.0f) * mass * (h * h + d * d), (1.0f / 12.0f) * mass * (w * w + d * d),
                (1.0f / 12.0f) * mass * (w * w + h * h));
}

// Inertia for a sphere (same in all directions)
static float GetSphereInertia(float mass, float radius)
{
    return (2.0f / 5.0f) * mass * radius * radius;
}

PhysicsSystem::PhysicsSystem() {}
PhysicsSystem::~PhysicsSystem() {}

void PhysicsSystem::Initialize() {}
void PhysicsSystem::Shutdown()
{
    m_CollisionPairs.clear();
}

void PhysicsSystem::Update(World& world, float /*dt*/)
{
    auto& settings = PhysicsSettings::Get();
    const float fixedDt = settings.fixedTimestep;
    const int maxSubSteps = settings.maxSubSteps;

    m_Accumulator += 1.0f / 60.0f;

    int substeps = 0;
    while (m_Accumulator >= fixedDt && substeps < maxSubSteps)
    {
        m_Accumulator -= fixedDt;
        substeps++;
        Integrate(world, fixedDt);
        DetectCollisions(world);
        ResolveCollisions(world, fixedDt);
    }
}

void PhysicsSystem::Integrate(World& world, float dt)
{
    auto& settings = PhysicsSettings::Get();
    Vec3 gravity = settings.gravity;

    // Sleep threshold - velocities below this are zeroed for stability
    const float linearSleepThreshold = 0.01f;
    const float angularSleepThreshold = 0.01f;

    world.Each<TransformComponent, RigidBodyComponent>(
        [&](Entity, TransformComponent& transform, RigidBodyComponent& rb)
        {
            if (rb.isKinematic)
                return;

            if (rb.useGravity)
                rb.velocity = rb.velocity + gravity * dt;

            float dragFactor = 1.0f - (rb.drag * dt);
            if (dragFactor < 0.0f)
                dragFactor = 0.0f;
            rb.velocity = rb.velocity * dragFactor;

            // Clamp tiny velocities to zero for stability
            if (rb.velocity.LengthSquared() < linearSleepThreshold * linearSleepThreshold)
                rb.velocity = Vec3::Zero();

            transform.position = transform.position + rb.velocity * dt;

            if (!rb.freezeRotation)
            {
                float angDragFactor = 1.0f - (rb.angularDrag * dt);
                if (angDragFactor < 0.0f)
                    angDragFactor = 0.0f;
                rb.angularVelocity = rb.angularVelocity * angDragFactor;

                // Clamp tiny angular velocities
                if (rb.angularVelocity.LengthSquared() < angularSleepThreshold * angularSleepThreshold)
                    rb.angularVelocity = Vec3::Zero();

                const float radToDeg = 57.2957795f;
                transform.rotation = transform.rotation + rb.angularVelocity * dt * radToDeg;

                auto wrapAngle = [](float& angle)
                {
                    while (angle > 360.0f)
                        angle -= 360.0f;
                    while (angle < 0.0f)
                        angle += 360.0f;
                };
                wrapAngle(transform.rotation.x);
                wrapAngle(transform.rotation.y);
                wrapAngle(transform.rotation.z);
            }
        });
}

void PhysicsSystem::DetectCollisions(World& world)
{
    m_CollisionPairs.clear();

    std::vector<CollisionJobData::PhysicsEntity> entities;

    world.Each<TransformComponent, RigidBodyComponent>(
        [&](Entity entity, TransformComponent& transform, RigidBodyComponent& rb)
        {
            CollisionJobData::PhysicsEntity pe;
            pe.entity = entity;
            pe.transform = &transform;
            pe.rb = &rb;
            pe.box = world.GetComponent<BoxColliderComponent>(entity);
            pe.sphere = world.GetComponent<SphereColliderComponent>(entity);
            if (pe.box || pe.sphere)
                entities.push_back(pe);
        });

    if (entities.empty())
        return;

    // Parallel collision detection
    uint32 numWorkers = JobSystem::Get().GetWorkerCount();
    if (numWorkers == 0)
        numWorkers = 1;

    // We split entities.size() into chunks
    size_t totalWork = entities.size();
    size_t chunkSize = (totalWork + numWorkers - 1) / numWorkers;

    std::vector<CollisionJobData> jobData(numWorkers);
    std::vector<Job> jobs;
    JobCounter counter;

    for (uint32 i = 0; i < numWorkers; ++i)
    {
        jobData[i].entities = &entities;
        jobData[i].startI = i * chunkSize;
        jobData[i].endI = std::min(jobData[i].startI + chunkSize, totalWork);
        jobData[i].results.reserve(entities.size() / numWorkers); // Heuristic

        if (jobData[i].startI < jobData[i].endI)
        {
            counter.Increment();
            jobs.push_back(Job::Create(DetectCollisionsJob, &jobData[i], &counter));
        }
    }

    if (!jobs.empty())
    {
        JobSystem::Get().ScheduleBatch(jobs.data(), jobs.size());
        JobSystem::Get().WaitForCounter(&counter);

        // Merge results
        for (uint32 i = 0; i < numWorkers; ++i)
        {
            m_CollisionPairs.insert(m_CollisionPairs.end(), jobData[i].results.begin(), jobData[i].results.end());
        }
    }
}

void PhysicsSystem::ResolveCollisions(World& world, float)
{
    for (auto& pair : m_CollisionPairs)
    {
        TransformComponent* tA = world.GetComponent<TransformComponent>(pair.entityA);
        TransformComponent* tB = world.GetComponent<TransformComponent>(pair.entityB);
        RigidBodyComponent* rbA = world.GetComponent<RigidBodyComponent>(pair.entityA);
        RigidBodyComponent* rbB = world.GetComponent<RigidBodyComponent>(pair.entityB);
        BoxColliderComponent* boxA = world.GetComponent<BoxColliderComponent>(pair.entityA);
        BoxColliderComponent* boxB = world.GetComponent<BoxColliderComponent>(pair.entityB);
        SphereColliderComponent* sphereA = world.GetComponent<SphereColliderComponent>(pair.entityA);
        SphereColliderComponent* sphereB = world.GetComponent<SphereColliderComponent>(pair.entityB);

        const bool isTriggerA = (boxA && boxA->isTrigger) || (sphereA && sphereA->isTrigger);
        const bool isTriggerB = (boxB && boxB->isTrigger) || (sphereB && sphereB->isTrigger);
        if (isTriggerA || isTriggerB)
            continue;

        if (!tA || !tB || !rbA || !rbB)
            continue;

        float invMassA = rbA->isKinematic ? 0.0f : 1.0f / rbA->mass;
        float invMassB = rbB->isKinematic ? 0.0f : 1.0f / rbB->mass;
        float totalInvMass = invMassA + invMassB;
        if (totalInvMass < 0.0001f)
            continue;

        Vec3 invInertiaA = Vec3::Zero();
        Vec3 invInertiaB = Vec3::Zero();

        // Calculate inertia for boxes
        if (!rbA->isKinematic && boxA)
        {
            Vec3 halfExt = Vec3(boxA->size.x * 0.5f * tA->scale.x, boxA->size.y * 0.5f * tA->scale.y,
                                boxA->size.z * 0.5f * tA->scale.z);
            Vec3 inertia = GetBoxInertiaTensor(rbA->mass, halfExt);
            invInertiaA = Vec3(1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z);
        }
        // Calculate inertia for spheres
        else if (!rbA->isKinematic && sphereA)
        {
            float avgScale = (tA->scale.x + tA->scale.y + tA->scale.z) / 3.0f;
            float inertia = GetSphereInertia(rbA->mass, sphereA->radius * avgScale);
            float invI = 1.0f / inertia;
            invInertiaA = Vec3(invI, invI, invI);
        }

        if (!rbB->isKinematic && boxB)
        {
            Vec3 halfExt = Vec3(boxB->size.x * 0.5f * tB->scale.x, boxB->size.y * 0.5f * tB->scale.y,
                                boxB->size.z * 0.5f * tB->scale.z);
            Vec3 inertia = GetBoxInertiaTensor(rbB->mass, halfExt);
            invInertiaB = Vec3(1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z);
        }
        else if (!rbB->isKinematic && sphereB)
        {
            float avgScale = (tB->scale.x + tB->scale.y + tB->scale.z) / 3.0f;
            float inertia = GetSphereInertia(rbB->mass, sphereB->radius * avgScale);
            float invI = 1.0f / inertia;
            invInertiaB = Vec3(invI, invI, invI);
        }

        for (int c = 0; c < pair.manifold.numContacts; c++)
        {
            ContactPoint& contact = pair.manifold.contacts[c];
            Vec3 normal = contact.normal;
            float depth = contact.depth;
            Vec3 contactPoint = contact.point;

            Vec3 rA = contactPoint - tA->position;
            Vec3 rB = contactPoint - tB->position;

            // Positional correction using settings
            auto& settings = PhysicsSettings::Get();
            float correctionMag = std::max(depth - settings.positionCorrectionSlop, 0.0f) *
                                  settings.positionCorrectionPercent / totalInvMass;
            tA->position = tA->position + normal * correctionMag * invMassA;
            tB->position = tB->position - normal * correctionMag * invMassB;

            // Velocity at contact
            Vec3 velA = rbA->velocity + Vec3::Cross(rbA->angularVelocity, rA);
            Vec3 velB = rbB->velocity + Vec3::Cross(rbB->angularVelocity, rB);
            Vec3 relVel = velA - velB;
            float relVelN = Vec3::Dot(relVel, normal);

            if (relVelN > 0)
                continue;

            // Normal impulse
            float restitution = (std::abs(relVelN) < 0.5f) ? 0.0f : (rbA->bounciness + rbB->bounciness) * 0.5f;

            Vec3 rAxN = Vec3::Cross(rA, normal);
            Vec3 rBxN = Vec3::Cross(rB, normal);
            float angularTermA =
                Vec3::Dot(rAxN, Vec3(rAxN.x * invInertiaA.x, rAxN.y * invInertiaA.y, rAxN.z * invInertiaA.z));
            float angularTermB =
                Vec3::Dot(rBxN, Vec3(rBxN.x * invInertiaB.x, rBxN.y * invInertiaB.y, rBxN.z * invInertiaB.z));

            float effectiveMass = totalInvMass + angularTermA + angularTermB;
            if (effectiveMass < 0.0001f)
                continue;

            float j = -(1.0f + restitution) * relVelN / effectiveMass / static_cast<float>(pair.manifold.numContacts);
            Vec3 impulse = normal * j;

            rbA->velocity = rbA->velocity + impulse * invMassA;
            rbB->velocity = rbB->velocity - impulse * invMassB;

            if (!rbA->freezeRotation && !rbA->isKinematic)
            {
                Vec3 torqueA = Vec3::Cross(rA, impulse);
                rbA->angularVelocity = rbA->angularVelocity + Vec3(torqueA.x * invInertiaA.x, torqueA.y * invInertiaA.y,
                                                                   torqueA.z * invInertiaA.z);
            }
            if (!rbB->freezeRotation && !rbB->isKinematic)
            {
                Vec3 torqueB = Vec3::Cross(rB, -impulse);
                rbB->angularVelocity = rbB->angularVelocity + Vec3(torqueB.x * invInertiaB.x, torqueB.y * invInertiaB.y,
                                                                   torqueB.z * invInertiaB.z);
            }

            // Friction
            velA = rbA->velocity + Vec3::Cross(rbA->angularVelocity, rA);
            velB = rbB->velocity + Vec3::Cross(rbB->angularVelocity, rB);
            relVel = velA - velB;
            Vec3 tangentVel = relVel - normal * Vec3::Dot(relVel, normal);
            float tangentSpeed = tangentVel.Length();

            if (tangentSpeed > 0.0001f)
            {
                Vec3 tangent = tangentVel / tangentSpeed;
                Vec3 rAxT = Vec3::Cross(rA, tangent);
                Vec3 rBxT = Vec3::Cross(rB, tangent);
                float angTermTA =
                    Vec3::Dot(rAxT, Vec3(rAxT.x * invInertiaA.x, rAxT.y * invInertiaA.y, rAxT.z * invInertiaA.z));
                float angTermTB =
                    Vec3::Dot(rBxT, Vec3(rBxT.x * invInertiaB.x, rBxT.y * invInertiaB.y, rBxT.z * invInertiaB.z));

                float effMassT = totalInvMass + angTermTA + angTermTB;
                if (effMassT < 0.0001f)
                    continue;

                float jt = -tangentSpeed / effMassT / static_cast<float>(pair.manifold.numContacts);
                float friction = (rbA->friction + rbB->friction) * 0.5f;
                float maxFriction = friction * std::abs(j);
                if (std::abs(jt) > maxFriction)
                    jt = (jt > 0 ? maxFriction : -maxFriction);

                Vec3 frictionImpulse = tangent * jt;
                rbA->velocity = rbA->velocity + frictionImpulse * invMassA;
                rbB->velocity = rbB->velocity - frictionImpulse * invMassB;

                if (!rbA->freezeRotation && !rbA->isKinematic)
                {
                    Vec3 tA2 = Vec3::Cross(rA, frictionImpulse);
                    rbA->angularVelocity = rbA->angularVelocity +
                                           Vec3(tA2.x * invInertiaA.x, tA2.y * invInertiaA.y, tA2.z * invInertiaA.z);
                }
                if (!rbB->freezeRotation && !rbB->isKinematic)
                {
                    Vec3 tB2 = Vec3::Cross(rB, -frictionImpulse);
                    rbB->angularVelocity = rbB->angularVelocity +
                                           Vec3(tB2.x * invInertiaB.x, tB2.y * invInertiaB.y, tB2.z * invInertiaB.z);
                }
            }
        }
    }
}

} // namespace Dot
