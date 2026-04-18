#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec3.h"

#include <string>
#include <vector>

namespace Dot
{

enum class MapProjectionMode : uint8
{
    Auto = 0,
    XY,
    XZ,
    YZ
};

struct MapFaceUV
{
    MapProjectionMode projectionMode = MapProjectionMode::Auto;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float rotationDeg = 0.0f;
};

struct MapFaceBakeData
{
    bool bakeValid = false;
    bool bakeStale = false;
    bool useBakedLighting = true;
    float lightmapIntensity = 1.0f;
    std::string lightmapTexturePath;
    std::string lightmapSidecarPath;
    std::string bakeSignature;
    float lightmapScaleU = 1.0f;
    float lightmapScaleV = 1.0f;
    float lightmapOffsetU = 0.0f;
    float lightmapOffsetV = 0.0f;
};

struct MapFace
{
    std::vector<uint32> vertexIndices;
    std::string materialPath;
    MapFaceUV uv;
    MapFaceBakeData bakedLighting;
};

struct MapBrushBakeSettings
{
    bool participateInBake = true;
    bool receiveBakedLighting = true;
    bool castBakedShadows = true;
    float resolutionScale = 1.0f;
};

struct MapBrush
{
    uint32 brushId = 0;
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<MapFace> faces;
    MapBrushBakeSettings bakedLighting;
};

struct MapAsset
{
    uint32 version = 1;
    uint32 nextBrushId = 1;
    std::vector<MapBrush> brushes;

    void Clear()
    {
        version = 1;
        nextBrushId = 1;
        brushes.clear();
    }

    MapBrush* FindBrush(uint32 brushId)
    {
        for (MapBrush& brush : brushes)
        {
            if (brush.brushId == brushId)
                return &brush;
        }
        return nullptr;
    }

    const MapBrush* FindBrush(uint32 brushId) const
    {
        for (const MapBrush& brush : brushes)
        {
            if (brush.brushId == brushId)
                return &brush;
        }
        return nullptr;
    }
};

struct MapCompiledVertex
{
    Vec3 position;
    Vec3 normal;
    float u = 0.0f;
    float v = 0.0f;
    float u2 = 0.0f;
    float v2 = 0.0f;
};

struct MapCompiledSubmesh
{
    uint32 indexStart = 0;
    uint32 indexCount = 0;
    std::string materialPath;
    uint32 brushId = 0;
    uint32 faceIndex = 0;
};

struct MapCompiledTriangle
{
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Vec3 normal;
    uint32 brushId = 0;
    uint32 faceIndex = 0;
    uint32 submeshIndex = 0;
};

struct MapCompiledData
{
    std::vector<MapCompiledVertex> vertices;
    std::vector<uint32> indices;
    std::vector<MapCompiledSubmesh> submeshes;
    std::vector<MapCompiledTriangle> collisionTriangles;
    Vec3 boundsMin = Vec3::Zero();
    Vec3 boundsMax = Vec3::Zero();
    uint64 revision = 0;

    bool IsEmpty() const
    {
        return vertices.empty() || indices.empty() || collisionTriangles.empty();
    }
};

DOT_CORE_API MapBrush CreateBoxBrush(uint32 brushId, const Vec3& center, const Vec3& halfExtents,
                                     const std::string& defaultMaterialPath = {});
DOT_CORE_API Vec3 ComputeMapFaceNormal(const MapBrush& brush, const MapFace& face);
DOT_CORE_API bool ValidateMapBrushConvex(const MapBrush& brush);
struct MapFaceLightmapBasis
{
    Vec3 axisU = Vec3::UnitX();
    Vec3 axisV = Vec3::UnitY();
    float minU = 0.0f;
    float minV = 0.0f;
    float rangeU = 1.0f;
    float rangeV = 1.0f;
};

DOT_CORE_API MapFaceLightmapBasis ComputeMapFaceLightmapBasis(const MapBrush& brush, const MapFace& face);
DOT_CORE_API Vec2 ComputeMapFaceLightmapUV(const MapFaceLightmapBasis& basis, const Vec3& position);

} // namespace Dot
