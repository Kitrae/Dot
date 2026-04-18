// =============================================================================
// Dot Engine - Scene Implementation
// =============================================================================

#include "Core/Scene/Scene.h"

namespace Dot
{

Scene::Scene()
{
    // Create root node - just entity with basic transform, no hierarchy
    Entity rootEntity = m_World.CreateEntity();
    m_Root = Node(rootEntity, &m_World);

    // Add transform component
    m_World.AddComponent<TransformComponent>(rootEntity);

    // Add name component
    auto& name = m_World.AddComponent<NameComponent>(rootEntity);
    name.name = "Root";

    // Add hierarchy component (no parent, empty children)
    m_World.AddComponent<HierarchyComponent>(rootEntity);
}

Scene::~Scene()
{
    // Don't call Clear() in destructor as World will clean up entities
}

Node Scene::CreateNode(const std::string& name)
{
    return CreateChildNode(m_Root, name);
}

Node Scene::CreateChildNode(Node parent, const std::string& name)
{
    Entity entity = m_World.CreateEntity();

    // Add all components first before setting up hierarchy
    auto& transform = m_World.AddComponent<TransformComponent>(entity);
    transform.position = Vec3::Zero();

    auto& nameComp = m_World.AddComponent<NameComponent>(entity);
    nameComp.name = name;

    auto& hierarchy = m_World.AddComponent<HierarchyComponent>(entity);
    hierarchy.parent = parent.GetEntity();

    // Add to parent's children list
    if (parent.IsValid())
    {
        auto* parentHierarchy = m_World.GetComponent<HierarchyComponent>(parent.GetEntity());
        if (parentHierarchy)
        {
            parentHierarchy->children.push_back(entity);
        }
    }

    return Node(entity, &m_World);
}

void Scene::DestroyNode(Node node)
{
    if (!node.IsValid())
        return;
    if (node == m_Root)
        return; // Can't destroy root

    // Recursively destroy children first
    auto* hierarchy = m_World.GetComponent<HierarchyComponent>(node.GetEntity());
    if (hierarchy)
    {
        // Copy children list since we're modifying it
        auto children = hierarchy->children;
        for (Entity childEntity : children)
        {
            DestroyNode(Node(childEntity, &m_World));
        }

        // Remove from parent
        if (hierarchy->parent.IsValid())
        {
            auto* parentHierarchy = m_World.GetComponent<HierarchyComponent>(hierarchy->parent);
            if (parentHierarchy)
            {
                auto& siblings = parentHierarchy->children;
                auto it = std::find(siblings.begin(), siblings.end(), node.GetEntity());
                if (it != siblings.end())
                {
                    siblings.erase(it);
                }
            }
        }
    }

    // Destroy entity
    m_World.DestroyEntity(node.GetEntity());
}

void Scene::UpdateTransforms()
{
    UpdateNodeTransform(m_Root, Mat4::Identity());
}

void Scene::UpdateNodeTransform(Node node, const Mat4& parentWorld)
{
    if (!node.IsValid())
        return;

    auto* transform = m_World.GetComponent<TransformComponent>(node.GetEntity());
    Mat4 currentWorld = parentWorld;

    if (transform)
    {
        transform->localMatrix = transform->GetLocalMatrix();
        transform->worldMatrix = parentWorld * transform->localMatrix;
        transform->dirty = false;
        currentWorld = transform->worldMatrix;
    }

    // Update children
    auto* hierarchy = m_World.GetComponent<HierarchyComponent>(node.GetEntity());
    if (hierarchy)
    {
        for (Entity childEntity : hierarchy->children)
        {
            UpdateNodeTransform(Node(childEntity, &m_World), currentWorld);
        }
    }
}

void Scene::Clear()
{
    auto* rootHierarchy = m_World.GetComponent<HierarchyComponent>(m_Root.GetEntity());
    if (rootHierarchy)
    {
        auto children = rootHierarchy->children;
        for (Entity childEntity : children)
        {
            DestroyNode(Node(childEntity, &m_World));
        }
    }
}

} // namespace Dot
