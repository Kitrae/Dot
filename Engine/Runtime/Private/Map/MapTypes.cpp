#include "Core/Map/MapTypes.h"

#include <cmath>

namespace Dot
{

namespace
{

bool PointInsideBrushPlane(const Vec3& point, const Vec3& facePoint, const Vec3& faceNormal)
{
    return Vec3::Dot(point - facePoint, faceNormal) <= 0.0005f;
}

Vec3 ComputeFaceBasisReference(const Vec3& normal)
{
    if (std::abs(normal.y) < 0.95f)
        return Vec3::UnitY();
    return Vec3::UnitX();
}

} // namespace

MapBrush CreateBoxBrush(uint32 brushId, const Vec3& center, const Vec3& halfExtents, const std::string& defaultMaterialPath)
{
    MapBrush brush;
    brush.brushId = brushId;
    brush.name = "Brush " + std::to_string(brushId);

    const Vec3 min = center - halfExtents;
    const Vec3 max = center + halfExtents;
    brush.vertices = {
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {max.x, max.y, min.z},
        {min.x, max.y, min.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {max.x, max.y, max.z},
        {min.x, max.y, max.z},
    };

    brush.faces = {
        {{0, 3, 2, 1}, defaultMaterialPath, {}},
        {{4, 5, 6, 7}, defaultMaterialPath, {}},
        {{0, 1, 5, 4}, defaultMaterialPath, {}},
        {{3, 7, 6, 2}, defaultMaterialPath, {}},
        {{0, 4, 7, 3}, defaultMaterialPath, {}},
        {{1, 2, 6, 5}, defaultMaterialPath, {}},
    };

    return brush;
}

Vec3 ComputeMapFaceNormal(const MapBrush& brush, const MapFace& face)
{
    if (face.vertexIndices.size() < 3)
        return Vec3::Up();

    const Vec3& a = brush.vertices[face.vertexIndices[0]];
    const Vec3& b = brush.vertices[face.vertexIndices[1]];
    const Vec3& c = brush.vertices[face.vertexIndices[2]];
    Vec3 normal = Vec3::Cross(b - a, c - a).Normalized();
    if (normal.LengthSquared() <= 1e-8f)
        return Vec3::Up();

    Vec3 brushCenter = Vec3::Zero();
    for (const Vec3& vertex : brush.vertices)
        brushCenter = brushCenter + vertex;
    brushCenter = brushCenter * (1.0f / static_cast<float>(brush.vertices.size()));

    if (Vec3::Dot(brushCenter - a, normal) > 0.0f)
        normal = normal * -1.0f;

    return normal;
}

bool ValidateMapBrushConvex(const MapBrush& brush)
{
    if (brush.vertices.size() < 4 || brush.faces.size() < 4)
        return false;

    for (const MapFace& face : brush.faces)
    {
        if (face.vertexIndices.size() < 3)
            return false;

        const Vec3 faceNormal = ComputeMapFaceNormal(brush, face);
        const Vec3 facePoint = brush.vertices[face.vertexIndices.front()];
        for (uint32 vertexIndex = 0; vertexIndex < brush.vertices.size(); ++vertexIndex)
        {
            bool isOnFace = false;
            for (uint32 faceVertexIndex : face.vertexIndices)
            {
                if (faceVertexIndex == vertexIndex)
                {
                    isOnFace = true;
                    break;
                }
            }

            if (isOnFace)
                continue;

            if (!PointInsideBrushPlane(brush.vertices[vertexIndex], facePoint, faceNormal))
                return false;
        }
    }

    return true;
}

MapFaceLightmapBasis ComputeMapFaceLightmapBasis(const MapBrush& brush, const MapFace& face)
{
    MapFaceLightmapBasis basis;
    if (face.vertexIndices.empty())
        return basis;

    const Vec3 faceNormal = ComputeMapFaceNormal(brush, face);
    const Vec3 reference = ComputeFaceBasisReference(faceNormal);
    basis.axisU = Vec3::Cross(reference, faceNormal).Normalized();
    if (basis.axisU.LengthSquared() <= 1e-8f)
        basis.axisU = Vec3::UnitX();
    basis.axisV = Vec3::Cross(faceNormal, basis.axisU).Normalized();
    if (basis.axisV.LengthSquared() <= 1e-8f)
        basis.axisV = Vec3::UnitY();

    const Vec3& firstVertex = brush.vertices[face.vertexIndices.front()];
    basis.minU = Vec3::Dot(firstVertex, basis.axisU);
    float maxU = basis.minU;
    basis.minV = Vec3::Dot(firstVertex, basis.axisV);
    float maxV = basis.minV;

    for (uint32 vertexIndex : face.vertexIndices)
    {
        const Vec3& position = brush.vertices[vertexIndex];
        const float projectedU = Vec3::Dot(position, basis.axisU);
        const float projectedV = Vec3::Dot(position, basis.axisV);
        basis.minU = std::min(basis.minU, projectedU);
        maxU = std::max(maxU, projectedU);
        basis.minV = std::min(basis.minV, projectedV);
        maxV = std::max(maxV, projectedV);
    }

    basis.rangeU = std::max(maxU - basis.minU, 0.001f);
    basis.rangeV = std::max(maxV - basis.minV, 0.001f);
    return basis;
}

Vec2 ComputeMapFaceLightmapUV(const MapFaceLightmapBasis& basis, const Vec3& position)
{
    const float projectedU = Vec3::Dot(position, basis.axisU);
    const float projectedV = Vec3::Dot(position, basis.axisV);
    return Vec2((projectedU - basis.minU) / basis.rangeU, (projectedV - basis.minV) / basis.rangeV);
}

} // namespace Dot
