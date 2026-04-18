// =============================================================================
// Dot Engine - Attachment Resolver
// =============================================================================

#include "Core/Scene/AttachmentResolver.h"

#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Dot
{

namespace
{

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265f;

enum class ResolveState : uint8
{
    Unvisited = 0,
    Resolving,
    Resolved,
};

Mat4 ComposeTransform(const Vec3& position, const Vec3& rotationDegrees, const Vec3& scale)
{
    return Mat4::Translation(position) * Mat4::RotationZ(rotationDegrees.z * kDegToRad) *
           Mat4::RotationY(rotationDegrees.y * kDegToRad) * Mat4::RotationX(rotationDegrees.x * kDegToRad) *
           Mat4::Scale(scale);
}

Vec3 ExtractEulerDegrees(const Mat4& matrix)
{
    Vec3 scale = matrix.GetScale();
    const float invScaleX = scale.x > 1e-6f ? 1.0f / scale.x : 0.0f;
    const float invScaleY = scale.y > 1e-6f ? 1.0f / scale.y : 0.0f;
    const float invScaleZ = scale.z > 1e-6f ? 1.0f / scale.z : 0.0f;

    const float m00 = matrix.columns[0].x * invScaleX;
    const float m10 = matrix.columns[0].y * invScaleX;
    const float m20 = matrix.columns[0].z * invScaleX;
    const float m21 = matrix.columns[1].z * invScaleY;
    const float m22 = matrix.columns[2].z * invScaleZ;
    const float m11 = matrix.columns[1].y * invScaleY;
    const float m12 = matrix.columns[2].y * invScaleZ;

    Vec3 result = Vec3::Zero();
    if (std::abs(m20) < 0.9999f)
    {
        result.x = std::atan2(m21, m22) * kRadToDeg;
        result.y = std::asin(-m20) * kRadToDeg;
        result.z = std::atan2(m10, m00) * kRadToDeg;
    }
    else
    {
        result.x = std::atan2(-m12, m11) * kRadToDeg;
        result.y = (m20 <= -0.9999f ? 90.0f : -90.0f);
        result.z = 0.0f;
    }

    return result;
}

Vec3 ApplyAxisMask(const Vec3& value, uint8 mask, const Vec3& fallback)
{
    return Vec3((mask & AttachmentAxisMask::X) != 0 ? value.x : fallback.x,
                (mask & AttachmentAxisMask::Y) != 0 ? value.y : fallback.y,
                (mask & AttachmentAxisMask::Z) != 0 ? value.z : fallback.z);
}

Mat4 BuildAttachmentBaseTransform(const Mat4& targetWorld, const AttachmentBindingComponent& binding)
{
    const Vec3 targetPosition = targetWorld.GetTranslation();
    const Vec3 targetRotation = ExtractEulerDegrees(targetWorld);
    const Vec3 targetScale = targetWorld.GetScale();

    const Vec3 position =
        binding.followPosition ? ApplyAxisMask(targetPosition, binding.positionAxes, Vec3::Zero()) : Vec3::Zero();
    const Vec3 rotation =
        binding.followRotation ? ApplyAxisMask(targetRotation, binding.rotationAxes, Vec3::Zero()) : Vec3::Zero();
    const Vec3 scale = binding.followScale ? ApplyAxisMask(targetScale, binding.scaleAxes, Vec3::One()) : Vec3::One();
    return ComposeTransform(position, rotation, scale);
}

struct Resolver
{
    World& world;
    Entity activeCamera = kNullEntity;
    std::unordered_map<uint32, ResolveState> states;
    std::unordered_map<uint32, Mat4> cache;

    Entity FindSocketRecursive(Entity entity, const std::string& socketName, std::unordered_set<uint32>& visited)
    {
        if (!entity.IsValid() || !world.IsAlive(entity) || !visited.insert(entity.id).second)
            return kNullEntity;

        if (AttachmentPointComponent* socket = world.GetComponent<AttachmentPointComponent>(entity))
        {
            if (socket->socketName == socketName)
                return entity;
        }

        HierarchyComponent* hierarchy = world.GetComponent<HierarchyComponent>(entity);
        if (!hierarchy)
            return kNullEntity;

        for (Entity child : hierarchy->children)
        {
            Entity found = FindSocketRecursive(child, socketName, visited);
            if (found.IsValid())
                return found;
        }

        return kNullEntity;
    }

    Entity ResolveSocket(Entity root, const std::string& socketName)
    {
        if (!root.IsValid() || socketName.empty())
            return root;

        std::unordered_set<uint32> visited;
        Entity socket = FindSocketRecursive(root, socketName, visited);
        return socket.IsValid() ? socket : root;
    }

    Mat4 ResolveEntity(Entity entity)
    {
        if (!entity.IsValid() || !world.IsAlive(entity))
            return Mat4::Identity();

        const auto stateIt = states.find(entity.id);
        if (stateIt != states.end())
        {
            if (stateIt->second == ResolveState::Resolved)
                return cache[entity.id];
            if (stateIt->second == ResolveState::Resolving)
            {
                if (TransformComponent* transform = world.GetComponent<TransformComponent>(entity))
                    return transform->localMatrix;
                return Mat4::Identity();
            }
        }

        states[entity.id] = ResolveState::Resolving;

        TransformComponent* transform = world.GetComponent<TransformComponent>(entity);
        const Mat4 local = transform ? transform->GetLocalMatrix() : Mat4::Identity();
        if (transform)
            transform->localMatrix = local;

        Mat4 worldMatrix = local;
        bool usedAttachment = false;
        if (AttachmentBindingComponent* binding = world.GetComponent<AttachmentBindingComponent>(entity))
        {
            if (binding->enabled)
            {
                Entity target = binding->targetMode == AttachmentTargetMode::ActiveCamera ? activeCamera : binding->targetEntity;
                usedAttachment = true;

                if (target.IsValid() && world.IsAlive(target) && target != entity)
                {
                    const Entity resolvedTarget = ResolveSocket(target, binding->socketName);
                    const Mat4 targetWorld = ResolveEntity(resolvedTarget);
                    worldMatrix = BuildAttachmentBaseTransform(targetWorld, *binding) * local;
                }
                else
                {
                    worldMatrix = local;
                }
            }
        }

        if (!usedAttachment)
        {
            if (HierarchyComponent* hierarchy = world.GetComponent<HierarchyComponent>(entity))
            {
                if (hierarchy->parent.IsValid() && world.IsAlive(hierarchy->parent))
                    worldMatrix = ResolveEntity(hierarchy->parent) * local;
            }
        }

        if (transform)
            transform->worldMatrix = worldMatrix;

        states[entity.id] = ResolveState::Resolved;
        cache[entity.id] = worldMatrix;
        return worldMatrix;
    }
};

} // namespace

Entity FindActiveCameraEntity(World& world)
{
    Entity activeCamera = kNullEntity;
    world.Each<CameraComponent>(
        [&](Entity entity, CameraComponent& camera)
        {
            if (!activeCamera.IsValid() && camera.isActive)
                activeCamera = entity;
        });
    return activeCamera;
}

Entity FindAttachmentSocket(World& world, Entity root, const std::string& socketName)
{
    Resolver resolver{world};
    return resolver.ResolveSocket(root, socketName);
}

void ResolveSceneTransforms(World& world, Entity activeCameraEntity)
{
    Resolver resolver{world};
    resolver.activeCamera = activeCameraEntity.IsValid() ? activeCameraEntity : FindActiveCameraEntity(world);
    world.EachEntity([&](Entity entity) { resolver.ResolveEntity(entity); });
}

} // namespace Dot
