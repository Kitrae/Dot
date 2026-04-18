#pragma once

#include "Core/Map/MapTypes.h"
#include "Core/Physics/CollisionMath.h"

#include <vector>

namespace Dot
{

struct StaticWorldHit
{
    bool hit = false;
    float distance = 0.0f;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
    uint32 brushId = 0;
    uint32 faceIndex = 0;
};

class DOT_CORE_API StaticWorldGeometry
{
public:
    void Clear();
    void Build(const MapCompiledData& compiledData);

    bool HasGeometry() const { return !m_Triangles.empty(); }
    const std::vector<MapCompiledTriangle>& GetTriangles() const { return m_Triangles; }
    uint64 GetRevision() const { return m_Revision; }

    bool Raycast(const Ray& ray, float maxDistance, StaticWorldHit& outHit) const;
    bool OverlapSphere(const Vec3& center, float radius, StaticWorldHit& outHit) const;
    Vec3 MoveSphereWithSliding(const Vec3& start, float radius, const Vec3& desiredMove, int maxIterations,
                               StaticWorldHit* outHit = nullptr) const;

private:
    std::vector<MapCompiledTriangle> m_Triangles;
    uint64 m_Revision = 0;
};

} // namespace Dot
