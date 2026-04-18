// =============================================================================
// Dot Engine - Scene Components
// =============================================================================
// ECS components for scene graph functionality.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/Math/Mat4.h"
#include "Core/Math/Quat.h"
#include "Core/Math/Vec3.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/UI/UIComponent.h"
#include "Core/UI/UIComponent.h"

#include <string>
#include <vector>

namespace Dot
{

/// Transform component - position, rotation (euler angles in degrees), scale
struct TransformComponent
{
    Vec3 position{0, 0, 0};
    Vec3 rotation{0, 0, 0}; // Euler angles in degrees (pitch, yaw, roll)
    Vec3 scale{1, 1, 1};

    // Cached world transform (updated by scene system)
    Mat4 localMatrix = Mat4::Identity();
    Mat4 worldMatrix = Mat4::Identity();
    bool dirty = true;

    Mat4 GetLocalMatrix() const
    {
        const float deg2rad = 3.14159265f / 180.0f;
        Mat4 t = Mat4::Translation(position);
        Mat4 rx = Mat4::RotationX(rotation.x * deg2rad);
        Mat4 ry = Mat4::RotationY(rotation.y * deg2rad);
        Mat4 rz = Mat4::RotationZ(rotation.z * deg2rad);
        Mat4 r = rz * ry * rx; // ZYX order (common convention)
        Mat4 s = Mat4::Scale(scale);
        return t * r * s;
    }

    void SetPosition(const Vec3& pos)
    {
        position = pos;
        dirty = true;
    }
    void SetRotation(const Vec3& rot)
    {
        rotation = rot;
        dirty = true;
    }
    void SetScale(const Vec3& s)
    {
        scale = s;
        dirty = true;
    }
};

/// Hierarchy component - parent/child relationships
struct HierarchyComponent
{
    Entity parent = kNullEntity;
    std::vector<Entity> children;

    void AddChild(Entity child) { children.push_back(child); }

    void RemoveChild(Entity child)
    {
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end())
        {
            children.erase(it);
        }
    }

    bool HasChildren() const { return !children.empty(); }
    usize GetChildCount() const { return children.size(); }
};

/// Name component - human-readable identifier
struct NameComponent
{
    std::string name;

    NameComponent() = default;
    explicit NameComponent(const std::string& n) : name(n) {}
    explicit NameComponent(std::string&& n) : name(std::move(n)) {}
};

/// Active component - whether node is enabled
struct ActiveComponent
{
    bool active = true;
    bool visibleInTree = true; // Computed: self && all parents active
};

enum class AttachmentTargetMode : uint8
{
    Entity = 0,
    ActiveCamera = 1,
};

namespace AttachmentAxisMask
{
constexpr uint8 None = 0;
constexpr uint8 X = 1 << 0;
constexpr uint8 Y = 1 << 1;
constexpr uint8 Z = 1 << 2;
constexpr uint8 XYZ = X | Y | Z;
} // namespace AttachmentAxisMask

struct AttachmentBindingComponent
{
    bool enabled = true;
    AttachmentTargetMode targetMode = AttachmentTargetMode::Entity;
    Entity targetEntity = kNullEntity;
    std::string socketName;
    bool followPosition = true;
    bool followRotation = true;
    bool followScale = true;
    uint8 positionAxes = AttachmentAxisMask::XYZ;
    uint8 rotationAxes = AttachmentAxisMask::XYZ;
    uint8 scaleAxes = AttachmentAxisMask::XYZ;
};

struct AttachmentPointComponent
{
    std::string socketName;
};

struct RenderLayerComponent
{
    uint32 mask = RenderLayerMask::World;
};

// =============================================================================
// Primitive Types
// =============================================================================

/// Primitive shape types - add more as needed
enum class PrimitiveType : uint8
{
    Cube = 0,
    Sphere,   // Future
    Cylinder, // Future
    Plane,    // Future
    Cone,     // Future
    Capsule,  // Future
    Count
};

/// Get display name for primitive type
inline const char* GetPrimitiveTypeName(PrimitiveType type)
{
    switch (type)
    {
        case PrimitiveType::Cube:
            return "Cube";
        case PrimitiveType::Sphere:
            return "Sphere";
        case PrimitiveType::Cylinder:
            return "Cylinder";
        case PrimitiveType::Plane:
            return "Plane";
        case PrimitiveType::Cone:
            return "Cone";
        case PrimitiveType::Capsule:
            return "Capsule";
        default:
            return "Unknown";
    }
}

inline void GetPrimitiveDefaultLodScreenHeightThresholds(PrimitiveType type, float& lod1Threshold, float& lod2Threshold)
{
    switch (type)
    {
        case PrimitiveType::Cube:
            lod1Threshold = 0.10f;
            lod2Threshold = 0.04f;
            break;
        case PrimitiveType::Sphere:
            lod1Threshold = 0.18f;
            lod2Threshold = 0.07f;
            break;
        case PrimitiveType::Cylinder:
            lod1Threshold = 0.17f;
            lod2Threshold = 0.065f;
            break;
        case PrimitiveType::Plane:
            lod1Threshold = 0.12f;
            lod2Threshold = 0.05f;
            break;
        case PrimitiveType::Cone:
            lod1Threshold = 0.16f;
            lod2Threshold = 0.06f;
            break;
        case PrimitiveType::Capsule:
            lod1Threshold = 0.18f;
            lod2Threshold = 0.07f;
            break;
        default:
            lod1Threshold = 0.22f;
            lod2Threshold = 0.08f;
            break;
    }
}

/// Primitive component - defines the mesh/shape of an entity
struct PrimitiveComponent
{
    PrimitiveType type = PrimitiveType::Cube;
    bool overrideLodThresholds = false;
    float lod1ScreenHeight = 0.10f;
    float lod2ScreenHeight = 0.04f;

    PrimitiveComponent() = default;
    explicit PrimitiveComponent(PrimitiveType t) : type(t)
    {
        GetPrimitiveDefaultLodScreenHeightThresholds(t, lod1ScreenHeight, lod2ScreenHeight);
    }
};

inline void GetPrimitiveLodScreenHeightThresholds(const PrimitiveComponent& primitive, float& lod1Threshold,
                                                  float& lod2Threshold)
{
    if (primitive.overrideLodThresholds)
    {
        lod1Threshold = std::max(primitive.lod1ScreenHeight, 0.0f);
        lod2Threshold = std::clamp(primitive.lod2ScreenHeight, 0.0f, lod1Threshold);
        return;
    }

    GetPrimitiveDefaultLodScreenHeightThresholds(primitive.type, lod1Threshold, lod2Threshold);
}

} // namespace Dot
