// =============================================================================
// Dot Engine - Scene Object
// =============================================================================
// Basic selectable object in the scene.
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

/// Basic 3D transform
struct Transform
{
    float positionX = 0, positionY = 0, positionZ = 0;
    float rotationX = 0, rotationY = 0, rotationZ = 0; // Euler angles in degrees
    float scaleX = 1, scaleY = 1, scaleZ = 1;
};

/// A selectable object in the scene
class SceneObject
{
public:
    SceneObject(const std::string& name = "Object") : m_Name(name) {}

    // Name
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    // Transform accessors
    Transform& GetTransform() { return m_Transform; }
    const Transform& GetTransform() const { return m_Transform; }

    void SetPosition(float x, float y, float z)
    {
        m_Transform.positionX = x;
        m_Transform.positionY = y;
        m_Transform.positionZ = z;
    }

    void GetPosition(float& x, float& y, float& z) const
    {
        x = m_Transform.positionX;
        y = m_Transform.positionY;
        z = m_Transform.positionZ;
    }

    // Bounding box for picking (centered on position)
    float GetBoundsSize() const { return m_BoundsSize; }
    void SetBoundsSize(float size) { m_BoundsSize = size; }

private:
    std::string m_Name;
    Transform m_Transform;
    float m_BoundsSize = 1.0f; // Cube bounding size for picking
};

} // namespace Dot
