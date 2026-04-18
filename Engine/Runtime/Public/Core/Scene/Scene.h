// =============================================================================
// Dot Engine - Scene
// =============================================================================
// Scene container managing nodes and transform hierarchy.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/World.h"
#include "Core/Scene/Node.h"

#include <string>
#include <vector>


namespace Dot
{

/// Scene - container for hierarchical node tree
class DOT_CORE_API Scene
{
public:
    Scene();
    ~Scene();

    // Node creation
    Node CreateNode(const std::string& name = "Node");
    Node CreateChildNode(Node parent, const std::string& name = "Node");

    // Node destruction
    void DestroyNode(Node node);

    // Root access
    Node GetRoot() const { return m_Root; }

    // World access (for advanced ECS usage)
    World& GetWorld() { return m_World; }
    const World& GetWorld() const { return m_World; }

    // Transform system
    void UpdateTransforms();

    // Scene management
    void Clear();
    usize GetNodeCount() const { return m_World.GetEntityCount(); }

private:
    void UpdateNodeTransform(Node node, const Mat4& parentWorld);

    World m_World;
    Node m_Root;
};

} // namespace Dot
