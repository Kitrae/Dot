// =============================================================================
// Dot Engine - Node Implementation
// =============================================================================

#include "Core/Scene/Node.h"

namespace Dot
{

static const std::string s_EmptyName;

Node::Node(Entity entity, World* world) : m_Entity(entity), m_World(world) {}

// =============================================================================
// Name
// =============================================================================

const std::string& Node::GetName() const
{
    if (!IsValid())
        return s_EmptyName;

    auto* name = m_World->GetComponent<NameComponent>(m_Entity);
    return name ? name->name : s_EmptyName;
}

void Node::SetName(const std::string& name)
{
    if (!IsValid())
        return;

    if (!m_World->HasComponent<NameComponent>(m_Entity))
    {
        m_World->AddComponent<NameComponent>(m_Entity);
    }
    m_World->GetComponent<NameComponent>(m_Entity)->name = name;
}

// =============================================================================
// Transform
// =============================================================================

Vec3 Node::GetPosition() const
{
    if (!IsValid())
        return Vec3::Zero();

    auto* transform = m_World->GetComponent<TransformComponent>(m_Entity);
    return transform ? transform->position : Vec3::Zero();
}

void Node::SetPosition(const Vec3& pos)
{
    if (!IsValid())
        return;

    if (!m_World->HasComponent<TransformComponent>(m_Entity))
    {
        m_World->AddComponent<TransformComponent>(m_Entity);
    }
    m_World->GetComponent<TransformComponent>(m_Entity)->SetPosition(pos);
}

void Node::SetPosition(float x, float y, float z)
{
    SetPosition(Vec3(x, y, z));
}

Vec3 Node::GetRotation() const
{
    if (!IsValid())
        return Vec3::Zero();

    auto* transform = m_World->GetComponent<TransformComponent>(m_Entity);
    return transform ? transform->rotation : Vec3::Zero();
}

void Node::SetRotation(const Vec3& rot)
{
    if (!IsValid())
        return;

    if (!m_World->HasComponent<TransformComponent>(m_Entity))
    {
        m_World->AddComponent<TransformComponent>(m_Entity);
    }
    m_World->GetComponent<TransformComponent>(m_Entity)->SetRotation(rot);
}

Vec3 Node::GetScale() const
{
    if (!IsValid())
        return Vec3::One();

    auto* transform = m_World->GetComponent<TransformComponent>(m_Entity);
    return transform ? transform->scale : Vec3::One();
}

void Node::SetScale(const Vec3& scale)
{
    if (!IsValid())
        return;

    if (!m_World->HasComponent<TransformComponent>(m_Entity))
    {
        m_World->AddComponent<TransformComponent>(m_Entity);
    }
    m_World->GetComponent<TransformComponent>(m_Entity)->SetScale(scale);
}

void Node::SetScale(float uniform)
{
    SetScale(Vec3(uniform, uniform, uniform));
}

// =============================================================================
// Hierarchy
// =============================================================================

Node Node::GetParent() const
{
    if (!IsValid())
        return Node();

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(m_Entity);
    if (hierarchy && hierarchy->parent.IsValid())
    {
        return Node(hierarchy->parent, m_World);
    }
    return Node();
}

void Node::SetParent(Node parent)
{
    if (!IsValid())
        return;

    // Remove from old parent
    Node oldParent = GetParent();
    if (oldParent.IsValid())
    {
        oldParent.RemoveChild(*this);
    }

    // Ensure we have hierarchy component
    if (!m_World->HasComponent<HierarchyComponent>(m_Entity))
    {
        m_World->AddComponent<HierarchyComponent>(m_Entity);
    }

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(m_Entity);
    hierarchy->parent = parent.GetEntity();

    // Add to new parent's children
    if (parent.IsValid())
    {
        parent.AddChild(*this);
    }
}

void Node::AddChild(Node child)
{
    if (!IsValid() || !child.IsValid())
        return;

    if (!m_World->HasComponent<HierarchyComponent>(m_Entity))
    {
        m_World->AddComponent<HierarchyComponent>(m_Entity);
    }

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(m_Entity);

    // Avoid duplicates
    auto& children = hierarchy->children;
    if (std::find(children.begin(), children.end(), child.GetEntity()) == children.end())
    {
        children.push_back(child.GetEntity());
    }
}

void Node::RemoveChild(Node child)
{
    if (!IsValid() || !child.IsValid())
        return;

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(m_Entity);
    if (hierarchy)
    {
        hierarchy->RemoveChild(child.GetEntity());
    }
}

std::vector<Node> Node::GetChildren() const
{
    std::vector<Node> result;
    if (!IsValid())
        return result;

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(m_Entity);
    if (hierarchy)
    {
        result.reserve(hierarchy->children.size());
        for (Entity child : hierarchy->children)
        {
            result.emplace_back(child, m_World);
        }
    }
    return result;
}

usize Node::GetChildCount() const
{
    if (!IsValid())
        return 0;

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(m_Entity);
    return hierarchy ? hierarchy->GetChildCount() : 0;
}

// =============================================================================
// Active State
// =============================================================================

bool Node::IsActive() const
{
    if (!IsValid())
        return false;

    auto* active = m_World->GetComponent<ActiveComponent>(m_Entity);
    return active ? active->active : true; // Default to active
}

void Node::SetActive(bool active)
{
    if (!IsValid())
        return;

    if (!m_World->HasComponent<ActiveComponent>(m_Entity))
    {
        m_World->AddComponent<ActiveComponent>(m_Entity);
    }
    m_World->GetComponent<ActiveComponent>(m_Entity)->active = active;
}

} // namespace Dot
