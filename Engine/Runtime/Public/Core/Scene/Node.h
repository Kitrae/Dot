// =============================================================================
// Dot Engine - Node
// =============================================================================
// User-friendly scene graph node wrapping ECS Entity.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/World.h"
#include "Core/Scene/Components.h"

#include <string>
#include <vector>

namespace Dot
{

class Scene; // Forward declaration

/// Node - user-friendly wrapper around ECS Entity
/// Provides familiar scene graph API (transform, children, etc.)
class DOT_CORE_API Node
{
public:
    Node() = default;
    Node(Entity entity, World* world);

    // Validity
    bool IsValid() const { return m_Entity.IsValid() && m_World != nullptr; }
    Entity GetEntity() const { return m_Entity; }

    // Name
    const std::string& GetName() const;
    void SetName(const std::string& name);

    // Transform
    Vec3 GetPosition() const;
    void SetPosition(const Vec3& pos);
    void SetPosition(float x, float y, float z);

    Vec3 GetRotation() const;
    void SetRotation(const Vec3& rot);

    Vec3 GetScale() const;
    void SetScale(const Vec3& scale);
    void SetScale(float uniform);

    // Hierarchy
    Node GetParent() const;
    void SetParent(Node parent);
    void AddChild(Node child);
    void RemoveChild(Node child);
    std::vector<Node> GetChildren() const;
    usize GetChildCount() const;

    // Active state
    bool IsActive() const;
    void SetActive(bool active);

    // Component access (passthrough to ECS)
    template <typename T> T& AddComponent() { return m_World->AddComponent<T>(m_Entity); }

    template <typename T> T* GetComponent() { return m_World->GetComponent<T>(m_Entity); }

    template <typename T> bool HasComponent() const { return m_World->HasComponent<T>(m_Entity); }

    // Comparison
    bool operator==(const Node& other) const { return m_Entity == other.m_Entity; }
    bool operator!=(const Node& other) const { return m_Entity != other.m_Entity; }

private:
    Entity m_Entity = kNullEntity;
    World* m_World = nullptr;
};

} // namespace Dot
