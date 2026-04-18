#include "Core/Map/MapCompiler.h"

#include <algorithm>
#include <cmath>

namespace Dot
{

namespace
{

Vec3 SelectProjectionNormal(MapProjectionMode mode, const Vec3& faceNormal)
{
    switch (mode)
    {
        case MapProjectionMode::XY:
            return Vec3::UnitZ();
        case MapProjectionMode::XZ:
            return Vec3::UnitY();
        case MapProjectionMode::YZ:
            return Vec3::UnitX();
        case MapProjectionMode::Auto:
        default:
        {
            const Vec3 absNormal(std::abs(faceNormal.x), std::abs(faceNormal.y), std::abs(faceNormal.z));
            if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z)
                return Vec3::UnitX();
            if (absNormal.y >= absNormal.x && absNormal.y >= absNormal.z)
                return Vec3::UnitY();
            return Vec3::UnitZ();
        }
    }
}

void BuildFaceBasis(const Vec3& axisNormal, Vec3& outAxisU, Vec3& outAxisV)
{
    if (std::abs(axisNormal.x) > 0.5f)
    {
        outAxisU = Vec3::UnitZ();
        outAxisV = Vec3::UnitY();
    }
    else if (std::abs(axisNormal.y) > 0.5f)
    {
        outAxisU = Vec3::UnitX();
        outAxisV = Vec3::UnitZ();
    }
    else
    {
        outAxisU = Vec3::UnitX();
        outAxisV = Vec3::UnitY();
    }
}

void RotateUV(float& u, float& v, float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float rotatedU = (u * c) - (v * s);
    const float rotatedV = (u * s) + (v * c);
    u = rotatedU;
    v = rotatedV;
}

} // namespace

MapCompiledData MapCompiler::Compile(const MapAsset& asset, uint64 revision)
{
    MapCompiledData compiled;
    compiled.revision = revision;

    for (const MapBrush& brush : asset.brushes)
    {
        for (uint32 faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
        {
            const MapFace& face = brush.faces[faceIndex];
            if (face.vertexIndices.size() < 3)
                continue;

            MapCompiledSubmesh submesh;
            submesh.indexStart = static_cast<uint32>(compiled.indices.size());
            submesh.materialPath = face.materialPath;
            submesh.brushId = brush.brushId;
            submesh.faceIndex = faceIndex;

            const Vec3 faceNormal = ComputeMapFaceNormal(brush, face);
            const Vec3 projectionNormal = SelectProjectionNormal(face.uv.projectionMode, faceNormal);
            Vec3 axisU;
            Vec3 axisV;
            BuildFaceBasis(projectionNormal, axisU, axisV);
            const MapFaceLightmapBasis lightmapBasis = ComputeMapFaceLightmapBasis(brush, face);

            const uint32 baseVertex = static_cast<uint32>(compiled.vertices.size());
            const float rotationRad = face.uv.rotationDeg * (3.14159265f / 180.0f);
            for (uint32 vertexIndex : face.vertexIndices)
            {
                const Vec3 position = brush.vertices[vertexIndex];
                float u = Vec3::Dot(position, axisU);
                float v = Vec3::Dot(position, axisV);
                RotateUV(u, v, rotationRad);

                MapCompiledVertex vertex;
                vertex.position = position;
                vertex.normal = faceNormal;
                vertex.u = (u * face.uv.scaleU) + face.uv.offsetU;
                vertex.v = (v * face.uv.scaleV) + face.uv.offsetV;
                const Vec2 lightmapUv = ComputeMapFaceLightmapUV(lightmapBasis, position);
                vertex.u2 = lightmapUv.x;
                vertex.v2 = lightmapUv.y;
                compiled.vertices.push_back(vertex);

                if (compiled.vertices.size() == 1)
                {
                    compiled.boundsMin = position;
                    compiled.boundsMax = position;
                }
                else
                {
                    compiled.boundsMin.x = std::min(compiled.boundsMin.x, position.x);
                    compiled.boundsMin.y = std::min(compiled.boundsMin.y, position.y);
                    compiled.boundsMin.z = std::min(compiled.boundsMin.z, position.z);
                    compiled.boundsMax.x = std::max(compiled.boundsMax.x, position.x);
                    compiled.boundsMax.y = std::max(compiled.boundsMax.y, position.y);
                    compiled.boundsMax.z = std::max(compiled.boundsMax.z, position.z);
                }
            }

            for (uint32 index = 1; index + 1 < face.vertexIndices.size(); ++index)
            {
                compiled.indices.push_back(baseVertex);
                compiled.indices.push_back(baseVertex + index);
                compiled.indices.push_back(baseVertex + index + 1);

                MapCompiledTriangle triangle;
                triangle.a = compiled.vertices[baseVertex].position;
                triangle.b = compiled.vertices[baseVertex + index].position;
                triangle.c = compiled.vertices[baseVertex + index + 1].position;
                triangle.normal = faceNormal;
                triangle.brushId = brush.brushId;
                triangle.faceIndex = faceIndex;
                triangle.submeshIndex = static_cast<uint32>(compiled.submeshes.size());
                compiled.collisionTriangles.push_back(triangle);
            }

            submesh.indexCount = static_cast<uint32>(compiled.indices.size()) - submesh.indexStart;
            if (submesh.indexCount > 0)
                compiled.submeshes.push_back(std::move(submesh));
        }
    }

    return compiled;
}

} // namespace Dot
