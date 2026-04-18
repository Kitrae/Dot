#include "MapDocument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Dot
{

namespace
{

float DistancePointToRay(const Vec3& point, const Ray& ray, float& outRayT)
{
    outRayT = Vec3::Dot(point - ray.origin, ray.direction);
    const Vec3 closestPoint = outRayT <= 0.0f ? ray.origin : ray.origin + (ray.direction * outRayT);
    return Vec3::Distance(point, closestPoint);
}

float DistanceRayToSegment(const Ray& ray, const Vec3& a, const Vec3& b, float& outRayT)
{
    const Vec3 segment = b - a;
    const Vec3 diff = ray.origin - a;
    const float aDot = Vec3::Dot(ray.direction, ray.direction);
    const float bDot = Vec3::Dot(ray.direction, segment);
    const float cDot = Vec3::Dot(segment, segment);
    const float dDot = Vec3::Dot(ray.direction, diff);
    const float eDot = Vec3::Dot(segment, diff);
    const float denom = (aDot * cDot) - (bDot * bDot);

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if (std::abs(denom) > 1e-6f)
    {
        rayT = ((bDot * eDot) - (cDot * dDot)) / denom;
        segmentT = ((aDot * eDot) - (bDot * dDot)) / denom;
    }

    rayT = std::max(0.0f, rayT);
    segmentT = std::max(0.0f, std::min(1.0f, segmentT));
    outRayT = rayT;

    const Vec3 pointOnRay = ray.origin + (ray.direction * rayT);
    const Vec3 pointOnSegment = a + (segment * segmentT);
    return Vec3::Distance(pointOnRay, pointOnSegment);
}

Vec3 ComputeBrushCenter(const MapBrush& brush)
{
    if (brush.vertices.empty())
        return Vec3::Zero();

    Vec3 center = Vec3::Zero();
    for (const Vec3& vertex : brush.vertices)
        center += vertex;
    return center / static_cast<float>(brush.vertices.size());
}

bool RaycastTriangleOnly(const Ray& ray, const MapCompiledTriangle& triangle, float& outDistance)
{
    const Vec3 edge1 = triangle.b - triangle.a;
    const Vec3 edge2 = triangle.c - triangle.a;
    const Vec3 p = Vec3::Cross(ray.direction, edge2);
    const float det = Vec3::Dot(edge1, p);
    if (std::abs(det) < 1e-6f)
        return false;

    const float invDet = 1.0f / det;
    const Vec3 t = ray.origin - triangle.a;
    const float u = Vec3::Dot(t, p) * invDet;
    if (u < 0.0f || u > 1.0f)
        return false;

    const Vec3 q = Vec3::Cross(t, edge1);
    const float v = Vec3::Dot(ray.direction, q) * invDet;
    if (v < 0.0f || (u + v) > 1.0f)
        return false;

    const float distance = Vec3::Dot(edge2, q) * invDet;
    if (distance < 0.0f)
        return false;

    outDistance = distance;
    return true;
}

MapSelectionMode InferSelectionMode(const MapSelection& selection)
{
    if (selection.vertexIndex >= 0)
        return MapSelectionMode::Vertex;
    if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0)
        return MapSelectionMode::Edge;
    if (selection.faceIndex >= 0)
        return MapSelectionMode::Face;
    return MapSelectionMode::Brush;
}

bool SameSelection(const MapSelection& a, const MapSelection& b)
{
    return a.brushId == b.brushId && a.faceIndex == b.faceIndex && a.vertexIndex == b.vertexIndex &&
           a.edgeVertexA == b.edgeVertexA && a.edgeVertexB == b.edgeVertexB;
}

uint64 BuildMapSelectionKey(const MapSelection& selection)
{
    const uint64 brushBits = static_cast<uint64>(selection.brushId) & 0xFFFFFFFFull;
    const uint64 faceBits = static_cast<uint64>(static_cast<uint16>(selection.faceIndex + 1)) << 32U;
    const uint64 vertexBits = static_cast<uint64>(static_cast<uint16>(selection.vertexIndex + 1)) << 48U;
    return brushBits ^ faceBits ^ vertexBits ^
           (static_cast<uint64>(static_cast<uint16>(selection.edgeVertexA + 1)) << 8U) ^
           (static_cast<uint64>(static_cast<uint16>(selection.edgeVertexB + 1)) << 24U);
}

std::vector<MapSelection> BuildEffectiveSelections(const MapSelection& primarySelection,
                                                   const std::vector<MapSelection>& selections)
{
    if (!selections.empty())
        return selections;
    if (primarySelection.brushId != 0)
        return {primarySelection};
    return {};
}

template <typename TSet>
bool ContainsBrushId(const TSet& set, uint32 brushId)
{
    return set.find(brushId) != set.end();
}

template <typename Fn>
bool ForEachUniqueSelectedBrush(MapAsset& asset, const MapSelection& primarySelection,
                                const std::vector<MapSelection>& selections, Fn&& fn)
{
    std::vector<uint32> brushIds;
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(primarySelection, selections);
    brushIds.reserve(effectiveSelections.size());

    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0)
            continue;
        if (std::find(brushIds.begin(), brushIds.end(), selection.brushId) == brushIds.end())
            brushIds.push_back(selection.brushId);
    }

    bool changed = false;
    for (uint32 brushId : brushIds)
    {
        MapBrush* brush = asset.FindBrush(brushId);
        if (!brush)
            continue;
        changed = fn(*brush) || changed;
    }

    return changed;
}

template <typename Fn>
bool ForEachSelectedFace(MapAsset& asset, const MapSelection& primarySelection, const std::vector<MapSelection>& selections,
                         Fn&& fn)
{
    bool changed = false;
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(primarySelection, selections);
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0 || selection.faceIndex < 0)
            continue;

        MapBrush* brush = asset.FindBrush(selection.brushId);
        if (!brush || selection.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;

        changed = fn(*brush, brush->faces[static_cast<size_t>(selection.faceIndex)], selection) || changed;
    }

    return changed;
}

constexpr float kClipPlaneEpsilon = 0.001f;
constexpr float kHollowBoxEpsilon = 0.001f;

float SignedDistanceToPlane(const Vec3& point, const Vec3& planePoint, const Vec3& planeNormal)
{
    return Vec3::Dot(point - planePoint, planeNormal);
}

void AppendUniquePoint(std::vector<Vec3>& points, const Vec3& point, float epsilon = kClipPlaneEpsilon)
{
    for (const Vec3& existing : points)
    {
        if (existing.ApproxEqual(point, epsilon))
            return;
    }
    points.push_back(point);
}

void RemoveDegeneratePolygonPoints(std::vector<Vec3>& points)
{
    if (points.size() < 2)
        return;

    std::vector<Vec3> cleaned;
    cleaned.reserve(points.size());
    for (const Vec3& point : points)
    {
        if (cleaned.empty() || !cleaned.back().ApproxEqual(point, kClipPlaneEpsilon))
            cleaned.push_back(point);
    }

    if (cleaned.size() >= 2 && cleaned.front().ApproxEqual(cleaned.back(), kClipPlaneEpsilon))
        cleaned.pop_back();

    points = std::move(cleaned);
}

std::vector<Vec3> ClipPolygonAgainstPlane(const std::vector<Vec3>& input, const Vec3& planePoint, const Vec3& planeNormal,
                                         std::vector<Vec3>& capPoints)
{
    std::vector<Vec3> output;
    if (input.empty())
        return output;

    output.reserve(input.size() + 2);

    for (size_t i = 0; i < input.size(); ++i)
    {
        const Vec3& current = input[i];
        const Vec3& next = input[(i + 1) % input.size()];
        const float currentDistance = SignedDistanceToPlane(current, planePoint, planeNormal);
        const float nextDistance = SignedDistanceToPlane(next, planePoint, planeNormal);
        const bool currentInside = currentDistance <= kClipPlaneEpsilon;
        const bool nextInside = nextDistance <= kClipPlaneEpsilon;

        if (std::abs(currentDistance) <= kClipPlaneEpsilon)
            AppendUniquePoint(capPoints, current);

        if (currentInside && nextInside)
        {
            output.push_back(next);
            continue;
        }

        if (currentInside != nextInside)
        {
            const float denom = currentDistance - nextDistance;
            const float t = std::abs(denom) > 1e-6f ? (currentDistance / denom) : 0.0f;
            const Vec3 intersection = current + ((next - current) * t);
            output.push_back(intersection);
            AppendUniquePoint(capPoints, intersection);

            if (nextInside)
                output.push_back(next);
        }
    }

    RemoveDegeneratePolygonPoints(output);
    return output;
}

Vec3 ComputePolygonNormal(const std::vector<Vec3>& polygon)
{
    if (polygon.size() < 3)
        return Vec3::Zero();

    for (size_t i = 2; i < polygon.size(); ++i)
    {
        const Vec3 normal = Vec3::Cross(polygon[1] - polygon[0], polygon[i] - polygon[0]);
        if (normal.LengthSquared() > 1e-8f)
            return normal.Normalized();
    }

    return Vec3::Zero();
}

bool BuildClippedBrush(const MapBrush& sourceBrush, const Vec3& planePoint, const Vec3& planeNormal,
                       const std::string& capMaterialPath, const MapFaceUV& capUv, MapBrush& outBrush,
                       int& outCapFaceIndex)
{
    bool foundInside = false;
    bool foundOutside = false;
    for (const Vec3& vertex : sourceBrush.vertices)
    {
        const float distance = SignedDistanceToPlane(vertex, planePoint, planeNormal);
        if (distance <= kClipPlaneEpsilon)
            foundInside = true;
        if (distance > kClipPlaneEpsilon)
            foundOutside = true;
    }

    if (!foundInside || !foundOutside)
        return false;

    struct PendingFace
    {
        std::vector<Vec3> polygon;
        MapFace face;
    };

    std::vector<PendingFace> pendingFaces;
    pendingFaces.reserve(sourceBrush.faces.size() + 1);
    std::vector<Vec3> capPoints;

    for (const MapFace& face : sourceBrush.faces)
    {
        std::vector<Vec3> polygon;
        polygon.reserve(face.vertexIndices.size());
        for (uint32 vertexIndex : face.vertexIndices)
            polygon.push_back(sourceBrush.vertices[vertexIndex]);

        std::vector<Vec3> clippedPolygon = ClipPolygonAgainstPlane(polygon, planePoint, planeNormal, capPoints);
        if (clippedPolygon.size() < 3)
            continue;

        PendingFace pending;
        pending.polygon = std::move(clippedPolygon);
        pending.face = face;
        pendingFaces.push_back(std::move(pending));
    }

    RemoveDegeneratePolygonPoints(capPoints);
    if (capPoints.size() < 3)
        return false;

    Vec3 centroid = Vec3::Zero();
    for (const Vec3& point : capPoints)
        centroid += point;
    centroid /= static_cast<float>(capPoints.size());

    Vec3 axisU = std::abs(planeNormal.y) < 0.95f ? Vec3::Cross(Vec3::UnitY(), planeNormal).Normalized()
                                                 : Vec3::Cross(Vec3::UnitX(), planeNormal).Normalized();
    if (axisU.LengthSquared() <= 1e-8f)
        axisU = Vec3::UnitX();
    Vec3 axisV = Vec3::Cross(planeNormal, axisU).Normalized();
    if (axisV.LengthSquared() <= 1e-8f)
        axisV = Vec3::UnitY();

    std::sort(capPoints.begin(), capPoints.end(),
              [&](const Vec3& a, const Vec3& b)
              {
                  const Vec3 relA = a - centroid;
                  const Vec3 relB = b - centroid;
                  const float angleA = std::atan2(Vec3::Dot(relA, axisV), Vec3::Dot(relA, axisU));
                  const float angleB = std::atan2(Vec3::Dot(relB, axisV), Vec3::Dot(relB, axisU));
                  return angleA < angleB;
              });
    RemoveDegeneratePolygonPoints(capPoints);
    if (capPoints.size() < 3)
        return false;

    Vec3 capNormal = ComputePolygonNormal(capPoints);
    if (capNormal.LengthSquared() <= 1e-8f)
        return false;
    if (Vec3::Dot(capNormal, planeNormal) < 0.0f)
        std::reverse(capPoints.begin(), capPoints.end());

    PendingFace capFace;
    capFace.polygon = capPoints;
    capFace.face.materialPath = capMaterialPath;
    capFace.face.uv = capUv;
    pendingFaces.push_back(std::move(capFace));

    MapBrush clippedBrush = sourceBrush;
    clippedBrush.vertices.clear();
    clippedBrush.faces.clear();

    auto findOrAddVertex = [&clippedBrush](const Vec3& point) -> uint32
    {
        for (size_t i = 0; i < clippedBrush.vertices.size(); ++i)
        {
            if (clippedBrush.vertices[i].ApproxEqual(point, kClipPlaneEpsilon))
                return static_cast<uint32>(i);
        }

        clippedBrush.vertices.push_back(point);
        return static_cast<uint32>(clippedBrush.vertices.size() - 1);
    };

    outCapFaceIndex = -1;
    for (const PendingFace& pending : pendingFaces)
    {
        if (pending.polygon.size() < 3)
            continue;

        MapFace rebuiltFace = pending.face;
        rebuiltFace.vertexIndices.clear();
        rebuiltFace.vertexIndices.reserve(pending.polygon.size());
        for (const Vec3& point : pending.polygon)
            rebuiltFace.vertexIndices.push_back(findOrAddVertex(point));

        std::vector<uint32> uniqueIndices;
        uniqueIndices.reserve(rebuiltFace.vertexIndices.size());
        for (uint32 vertexIndex : rebuiltFace.vertexIndices)
        {
            if (uniqueIndices.empty() || uniqueIndices.back() != vertexIndex)
                uniqueIndices.push_back(vertexIndex);
        }
        if (uniqueIndices.size() >= 2 && uniqueIndices.front() == uniqueIndices.back())
            uniqueIndices.pop_back();
        rebuiltFace.vertexIndices = std::move(uniqueIndices);

        if (rebuiltFace.vertexIndices.size() < 3)
            continue;

        clippedBrush.faces.push_back(std::move(rebuiltFace));
        if (&pending == &pendingFaces.back())
            outCapFaceIndex = static_cast<int>(clippedBrush.faces.size() - 1);
    }

    if (!ValidateMapBrushConvex(clippedBrush))
        return false;

    outBrush = std::move(clippedBrush);
    return outCapFaceIndex >= 0;
}

MapFaceUV BuildFittedFaceUv(const MapBrush& brush, const MapFace& face)
{
    MapFaceUV uv = face.uv;
    uv.offsetU = 0.0f;
    uv.offsetV = 0.0f;
    uv.rotationDeg = 0.0f;

    const MapFaceLightmapBasis basis = ComputeMapFaceLightmapBasis(brush, face);
    uv.scaleU = 1.0f / std::max(basis.rangeU, 0.001f);
    uv.scaleV = 1.0f / std::max(basis.rangeV, 0.001f);
    uv.offsetU = -basis.minU * uv.scaleU;
    uv.offsetV = -basis.minV * uv.scaleV;
    return uv;
}

bool BuildExtrudedBrush(const MapBrush& sourceBrush, int faceIndex, float distance, MapBrush& outBrush,
                        int& outExtrudedFaceIndex)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(sourceBrush.faces.size()) || std::abs(distance) <= 1e-5f)
        return false;

    struct BrushPlane
    {
        Vec3 point = Vec3::Zero();
        Vec3 normal = Vec3::Up();
        MapFace sourceFace;
        bool isExtrudedFace = false;
    };

    auto referenceForPlane = [](const Vec3& normal) -> Vec3
    {
        return std::abs(normal.y) < 0.95f ? Vec3::UnitY() : Vec3::UnitX();
    };

    auto planeDistance = [](const BrushPlane& plane) -> float
    {
        return Vec3::Dot(plane.normal, plane.point);
    };

    auto samePlane = [&](const BrushPlane& a, const BrushPlane& b) -> bool
    {
        if (Vec3::Dot(a.normal, b.normal) < 0.999f)
            return false;
        return std::abs(planeDistance(a) - planeDistance(b)) <= 0.0015f;
    };

    auto intersectPlanes = [&](const BrushPlane& a, const BrushPlane& b, const BrushPlane& c, Vec3& outPoint) -> bool
    {
        const Vec3 bc = Vec3::Cross(b.normal, c.normal);
        const float denom = Vec3::Dot(a.normal, bc);
        if (std::abs(denom) <= 1e-6f)
            return false;

        const float distanceA = planeDistance(a);
        const float distanceB = planeDistance(b);
        const float distanceC = planeDistance(c);
        outPoint = ((bc * distanceA) + (Vec3::Cross(c.normal, a.normal) * distanceB) +
                    (Vec3::Cross(a.normal, b.normal) * distanceC)) /
                   denom;
        return true;
    };

    auto buildBrushFromPlanes = [&](const std::vector<BrushPlane>& planes, MapBrush& rebuiltBrush,
                                    int& rebuiltExtrudedFaceIndex) -> bool
    {
        rebuiltBrush = sourceBrush;
        rebuiltBrush.vertices.clear();
        rebuiltBrush.faces.clear();
        rebuiltExtrudedFaceIndex = -1;

        std::vector<Vec3> candidateVertices;
        candidateVertices.reserve(planes.size() * 4);
        for (size_t i = 0; i < planes.size(); ++i)
        {
            for (size_t j = i + 1; j < planes.size(); ++j)
            {
                for (size_t k = j + 1; k < planes.size(); ++k)
                {
                    Vec3 intersection = Vec3::Zero();
                    if (!intersectPlanes(planes[i], planes[j], planes[k], intersection))
                        continue;

                    bool insideAllPlanes = true;
                    for (const BrushPlane& plane : planes)
                    {
                        if (SignedDistanceToPlane(intersection, plane.point, plane.normal) > 0.0025f)
                        {
                            insideAllPlanes = false;
                            break;
                        }
                    }

                    if (insideAllPlanes)
                        AppendUniquePoint(candidateVertices, intersection, 0.0025f);
                }
            }
        }

        if (candidateVertices.size() < 4)
            return false;

        auto findOrAddVertex = [&](const Vec3& point) -> uint32
        {
            for (size_t i = 0; i < rebuiltBrush.vertices.size(); ++i)
            {
                if (rebuiltBrush.vertices[i].ApproxEqual(point, 0.0025f))
                    return static_cast<uint32>(i);
            }

            rebuiltBrush.vertices.push_back(point);
            return static_cast<uint32>(rebuiltBrush.vertices.size() - 1);
        };

        for (const BrushPlane& plane : planes)
        {
            std::vector<Vec3> facePolygon;
            facePolygon.reserve(candidateVertices.size());
            for (const Vec3& candidate : candidateVertices)
            {
                if (std::abs(SignedDistanceToPlane(candidate, plane.point, plane.normal)) <= 0.0025f)
                    AppendUniquePoint(facePolygon, candidate, 0.0025f);
            }

            if (facePolygon.size() < 3)
            {
                if (plane.isExtrudedFace)
                    return false;
                continue;
            }

            Vec3 faceCenter = Vec3::Zero();
            for (const Vec3& point : facePolygon)
                faceCenter += point;
            faceCenter /= static_cast<float>(facePolygon.size());

            const Vec3 axisU = Vec3::Cross(referenceForPlane(plane.normal), plane.normal).Normalized();
            Vec3 resolvedAxisU = axisU;
            if (resolvedAxisU.LengthSquared() <= 1e-8f)
                resolvedAxisU = Vec3::UnitX();
            Vec3 axisV = Vec3::Cross(plane.normal, resolvedAxisU).Normalized();
            if (axisV.LengthSquared() <= 1e-8f)
                axisV = Vec3::UnitY();

            std::sort(facePolygon.begin(), facePolygon.end(), [&](const Vec3& lhs, const Vec3& rhs)
            {
                const Vec3 lhsOffset = lhs - faceCenter;
                const Vec3 rhsOffset = rhs - faceCenter;
                const float lhsAngle = std::atan2(Vec3::Dot(lhsOffset, axisV), Vec3::Dot(lhsOffset, resolvedAxisU));
                const float rhsAngle = std::atan2(Vec3::Dot(rhsOffset, axisV), Vec3::Dot(rhsOffset, resolvedAxisU));
                return lhsAngle < rhsAngle;
            });

            RemoveDegeneratePolygonPoints(facePolygon);
            if (facePolygon.size() < 3)
            {
                if (plane.isExtrudedFace)
                    return false;
                continue;
            }

            if (Vec3::Dot(ComputePolygonNormal(facePolygon), plane.normal) < 0.0f)
                std::reverse(facePolygon.begin(), facePolygon.end());

            MapFace rebuiltFace = plane.sourceFace;
            rebuiltFace.vertexIndices.clear();
            rebuiltFace.vertexIndices.reserve(facePolygon.size());
            for (const Vec3& point : facePolygon)
                rebuiltFace.vertexIndices.push_back(findOrAddVertex(point));

            rebuiltBrush.faces.push_back(std::move(rebuiltFace));
            if (plane.isExtrudedFace)
                rebuiltExtrudedFaceIndex = static_cast<int>(rebuiltBrush.faces.size() - 1);
        }

        return rebuiltExtrudedFaceIndex >= 0 && ValidateMapBrushConvex(rebuiltBrush);
    };

    const MapFace& sourceFace = sourceBrush.faces[static_cast<size_t>(faceIndex)];
    if (sourceFace.vertexIndices.size() < 3)
        return false;

    std::vector<BrushPlane> planes;
    planes.reserve(sourceBrush.faces.size());
    for (size_t i = 0; i < sourceBrush.faces.size(); ++i)
    {
        const MapFace& source = sourceBrush.faces[i];
        if (source.vertexIndices.size() < 3)
            continue;

        BrushPlane plane;
        plane.point = sourceBrush.vertices[source.vertexIndices.front()];
        plane.normal = ComputeMapFaceNormal(sourceBrush, source);
        plane.sourceFace = source;
        plane.isExtrudedFace = i == static_cast<size_t>(faceIndex);
        if (plane.isExtrudedFace)
            plane.point += plane.normal * distance;

        bool duplicatePlane = false;
        for (const BrushPlane& existing : planes)
        {
            if (samePlane(existing, plane))
            {
                duplicatePlane = true;
                break;
            }
        }

        if (!duplicatePlane)
            planes.push_back(std::move(plane));
    }

    if (planes.size() < 4)
        return false;

    return buildBrushFromPlanes(planes, outBrush, outExtrudedFaceIndex);
}

bool BuildHollowBrushes(const MapBrush& sourceBrush, float wallThickness, std::vector<MapBrush>& outBrushes)
{
    outBrushes.clear();
    if (wallThickness <= kHollowBoxEpsilon || sourceBrush.faces.size() < 4)
        return false;

    struct BrushPlane
    {
        Vec3 point = Vec3::Zero();
        Vec3 normal = Vec3::Up();
        MapFace sourceFace;
    };

    auto referenceForPlane = [](const Vec3& normal) -> Vec3
    {
        return std::abs(normal.y) < 0.95f ? Vec3::UnitY() : Vec3::UnitX();
    };

    auto planeDistance = [](const BrushPlane& plane) -> float
    {
        return Vec3::Dot(plane.normal, plane.point);
    };

    auto samePlane = [&](const BrushPlane& a, const BrushPlane& b) -> bool
    {
        if (Vec3::Dot(a.normal, b.normal) < 0.999f)
            return false;
        return std::abs(planeDistance(a) - planeDistance(b)) <= 0.0015f;
    };

    auto intersectPlanes = [&](const BrushPlane& a, const BrushPlane& b, const BrushPlane& c, Vec3& outPoint) -> bool
    {
        const Vec3 bc = Vec3::Cross(b.normal, c.normal);
        const float denom = Vec3::Dot(a.normal, bc);
        if (std::abs(denom) <= 1e-6f)
            return false;

        const float distanceA = planeDistance(a);
        const float distanceB = planeDistance(b);
        const float distanceC = planeDistance(c);
        outPoint = ((bc * distanceA) + (Vec3::Cross(c.normal, a.normal) * distanceB) +
                    (Vec3::Cross(a.normal, b.normal) * distanceC)) /
                   denom;
        return true;
    };

    auto buildBrushFromPlanes = [&](const std::vector<BrushPlane>& planes, MapBrush& rebuiltBrush) -> bool
    {
        rebuiltBrush = sourceBrush;
        rebuiltBrush.vertices.clear();
        rebuiltBrush.faces.clear();

        std::vector<Vec3> candidateVertices;
        candidateVertices.reserve(planes.size() * 4);
        for (size_t i = 0; i < planes.size(); ++i)
        {
            for (size_t j = i + 1; j < planes.size(); ++j)
            {
                for (size_t k = j + 1; k < planes.size(); ++k)
                {
                    Vec3 intersection = Vec3::Zero();
                    if (!intersectPlanes(planes[i], planes[j], planes[k], intersection))
                        continue;

                    bool insideAllPlanes = true;
                    for (const BrushPlane& plane : planes)
                    {
                        if (SignedDistanceToPlane(intersection, plane.point, plane.normal) > 0.0025f)
                        {
                            insideAllPlanes = false;
                            break;
                        }
                    }

                    if (insideAllPlanes)
                        AppendUniquePoint(candidateVertices, intersection, 0.0025f);
                }
            }
        }

        if (candidateVertices.size() < 4)
            return false;

        auto findOrAddVertex = [&](const Vec3& point) -> uint32
        {
            for (size_t i = 0; i < rebuiltBrush.vertices.size(); ++i)
            {
                if (rebuiltBrush.vertices[i].ApproxEqual(point, 0.0025f))
                    return static_cast<uint32>(i);
            }

            rebuiltBrush.vertices.push_back(point);
            return static_cast<uint32>(rebuiltBrush.vertices.size() - 1);
        };

        for (const BrushPlane& plane : planes)
        {
            std::vector<Vec3> facePolygon;
            facePolygon.reserve(candidateVertices.size());
            for (const Vec3& candidate : candidateVertices)
            {
                if (std::abs(SignedDistanceToPlane(candidate, plane.point, plane.normal)) <= 0.0025f)
                    AppendUniquePoint(facePolygon, candidate, 0.0025f);
            }

            if (facePolygon.size() < 3)
                continue;

            Vec3 faceCenter = Vec3::Zero();
            for (const Vec3& point : facePolygon)
                faceCenter += point;
            faceCenter /= static_cast<float>(facePolygon.size());

            Vec3 axisU = Vec3::Cross(referenceForPlane(plane.normal), plane.normal).Normalized();
            if (axisU.LengthSquared() <= 1e-8f)
                axisU = Vec3::UnitX();
            Vec3 axisV = Vec3::Cross(plane.normal, axisU).Normalized();
            if (axisV.LengthSquared() <= 1e-8f)
                axisV = Vec3::UnitY();

            std::sort(facePolygon.begin(), facePolygon.end(), [&](const Vec3& lhs, const Vec3& rhs)
            {
                const Vec3 lhsOffset = lhs - faceCenter;
                const Vec3 rhsOffset = rhs - faceCenter;
                const float lhsAngle = std::atan2(Vec3::Dot(lhsOffset, axisV), Vec3::Dot(lhsOffset, axisU));
                const float rhsAngle = std::atan2(Vec3::Dot(rhsOffset, axisV), Vec3::Dot(rhsOffset, axisU));
                return lhsAngle < rhsAngle;
            });

            RemoveDegeneratePolygonPoints(facePolygon);
            if (facePolygon.size() < 3)
                continue;

            if (Vec3::Dot(ComputePolygonNormal(facePolygon), plane.normal) < 0.0f)
                std::reverse(facePolygon.begin(), facePolygon.end());

            MapFace rebuiltFace = plane.sourceFace;
            rebuiltFace.vertexIndices.clear();
            rebuiltFace.vertexIndices.reserve(facePolygon.size());
            for (const Vec3& point : facePolygon)
                rebuiltFace.vertexIndices.push_back(findOrAddVertex(point));

            rebuiltBrush.faces.push_back(std::move(rebuiltFace));
        }

        return !rebuiltBrush.faces.empty() && ValidateMapBrushConvex(rebuiltBrush);
    };

    std::vector<BrushPlane> outerPlanes;
    outerPlanes.reserve(sourceBrush.faces.size());
    for (const MapFace& face : sourceBrush.faces)
    {
        if (face.vertexIndices.size() < 3)
            continue;

        BrushPlane plane;
        plane.point = sourceBrush.vertices[face.vertexIndices.front()];
        plane.normal = ComputeMapFaceNormal(sourceBrush, face);
        plane.sourceFace = face;

        bool duplicatePlane = false;
        for (const BrushPlane& existing : outerPlanes)
        {
            if (samePlane(existing, plane))
            {
                duplicatePlane = true;
                break;
            }
        }

        if (!duplicatePlane)
            outerPlanes.push_back(std::move(plane));
    }

    if (outerPlanes.size() < 4)
        return false;

    std::vector<BrushPlane> innerPlanes = outerPlanes;
    for (BrushPlane& innerPlane : innerPlanes)
        innerPlane.point -= innerPlane.normal * wallThickness;

    MapBrush innerBrush;
    if (!buildBrushFromPlanes(innerPlanes, innerBrush))
        return false;

    outBrushes.reserve(outerPlanes.size());
    for (const BrushPlane& outerPlane : outerPlanes)
    {
        std::vector<BrushPlane> shellPlanes = outerPlanes;

        BrushPlane innerBoundary;
        innerBoundary.point = outerPlane.point - (outerPlane.normal * wallThickness);
        innerBoundary.normal = outerPlane.normal * -1.0f;
        innerBoundary.sourceFace = outerPlane.sourceFace;
        shellPlanes.push_back(std::move(innerBoundary));

        MapBrush shellBrush;
        if (!buildBrushFromPlanes(shellPlanes, shellBrush))
            return false;

        outBrushes.push_back(std::move(shellBrush));
    }

    return !outBrushes.empty();
}

void ComputeBrushBounds(const MapBrush& brush, Vec3& outMin, Vec3& outMax)
{
    if (brush.vertices.empty())
    {
        outMin = Vec3::Zero();
        outMax = Vec3::Zero();
        return;
    }

    outMin = brush.vertices.front();
    outMax = brush.vertices.front();
    for (const Vec3& vertex : brush.vertices)
    {
        outMin.x = std::min(outMin.x, vertex.x);
        outMin.y = std::min(outMin.y, vertex.y);
        outMin.z = std::min(outMin.z, vertex.z);
        outMax.x = std::max(outMax.x, vertex.x);
        outMax.y = std::max(outMax.y, vertex.y);
        outMax.z = std::max(outMax.z, vertex.z);
    }
}

bool IsAxisAlignedBoxBrush(const MapBrush& brush, Vec3& outCenter, Vec3& outHalfExtents)
{
    if (brush.vertices.size() != 8 || brush.faces.size() != 6)
        return false;

    Vec3 minBounds;
    Vec3 maxBounds;
    ComputeBrushBounds(brush, minBounds, maxBounds);
    outCenter = (minBounds + maxBounds) * 0.5f;
    outHalfExtents = (maxBounds - minBounds) * 0.5f;
    if (outHalfExtents.x <= kHollowBoxEpsilon || outHalfExtents.y <= kHollowBoxEpsilon ||
        outHalfExtents.z <= kHollowBoxEpsilon)
    {
        return false;
    }

    for (const MapFace& face : brush.faces)
    {
        if (face.vertexIndices.size() != 4)
            return false;
    }

    int cornerMask = 0;
    for (const Vec3& vertex : brush.vertices)
    {
        const bool onMinX = std::abs(vertex.x - minBounds.x) <= kHollowBoxEpsilon;
        const bool onMaxX = std::abs(vertex.x - maxBounds.x) <= kHollowBoxEpsilon;
        const bool onMinY = std::abs(vertex.y - minBounds.y) <= kHollowBoxEpsilon;
        const bool onMaxY = std::abs(vertex.y - maxBounds.y) <= kHollowBoxEpsilon;
        const bool onMinZ = std::abs(vertex.z - minBounds.z) <= kHollowBoxEpsilon;
        const bool onMaxZ = std::abs(vertex.z - maxBounds.z) <= kHollowBoxEpsilon;
        if ((onMinX == onMaxX) || (onMinY == onMaxY) || (onMinZ == onMaxZ))
            return false;

        const int cornerIndex = (onMaxX ? 1 : 0) | (onMaxY ? 2 : 0) | (onMaxZ ? 4 : 0);
        cornerMask |= (1 << cornerIndex);
    }

    return cornerMask == 0xFF;
}

const MapFace* FindBoxFaceForNormal(const MapBrush& brush, const Vec3& expectedNormal)
{
    const MapFace* bestFace = nullptr;
    float bestDot = -1.0f;
    for (const MapFace& face : brush.faces)
    {
        const Vec3 normal = ComputeMapFaceNormal(brush, face);
        const float alignment = Vec3::Dot(normal, expectedNormal);
        if (alignment > bestDot)
        {
            bestDot = alignment;
            bestFace = &face;
        }
    }

    if (bestDot < 0.99f)
        return nullptr;
    return bestFace;
}

void ApplyBoxFaceMaterial(MapBrush& brush, const std::string& materialPath, const MapFaceUV& uv)
{
    for (MapFace& face : brush.faces)
    {
        face.materialPath = materialPath;
        face.uv = uv;
    }
}

bool FacesShareEdge(const MapFace& a, const MapFace& b)
{
    int sharedVertexCount = 0;
    for (uint32 indexA : a.vertexIndices)
    {
        for (uint32 indexB : b.vertexIndices)
        {
            if (indexA == indexB)
            {
                ++sharedVertexCount;
                if (sharedVertexCount >= 2)
                    return true;
            }
        }
    }
    return false;
}

} // namespace

MapDocument::MapDocument()
    : m_StaticWorldGeometry(std::make_shared<StaticWorldGeometry>())
{
    New();
}

void MapDocument::New()
{
    m_Path.clear();
    m_Asset.Clear();
    m_Compiled = {};
    m_StaticWorldGeometry->Clear();
    m_Selection.Clear();
    m_Selections.clear();
    m_HiddenBrushIds.clear();
    m_LockedBrushIds.clear();
    m_LastError.clear();
    m_Revision = 1;
    m_Dirty = false;
}

bool MapDocument::Load(const std::filesystem::path& path)
{
    MapAsset loadedAsset;
    if (!m_Serializer.Load(loadedAsset, path.string()))
    {
        m_LastError = m_Serializer.GetLastError();
        return false;
    }

    m_Path = path;
    m_Asset = std::move(loadedAsset);
    m_Selection.Clear();
    m_Selections.clear();
    m_HiddenBrushIds.clear();
    m_LockedBrushIds.clear();
    m_LastError.clear();
    m_Dirty = false;
    ++m_Revision;
    RebuildCompiledData();
    return true;
}

bool MapDocument::Save()
{
    if (m_Path.empty())
    {
        m_LastError = "Map has no path";
        return false;
    }

    if (!m_Serializer.Save(m_Asset, m_Path.string()))
    {
        m_LastError = m_Serializer.GetLastError();
        return false;
    }

    m_LastError.clear();
    m_Dirty = false;
    return true;
}

bool MapDocument::SaveAs(const std::filesystem::path& path)
{
    m_Path = path;
    return Save();
}

void MapDocument::SetSelection(const MapSelection& selection)
{
    m_Selection = selection;
    m_Selections.clear();
    if (selection.brushId == 0)
        return;
    m_Selections.push_back(selection);
    m_SelectionMode = InferSelectionMode(selection);
}

void MapDocument::SetSelections(const std::vector<MapSelection>& selections)
{
    m_Selections.clear();
    m_Selection.Clear();

    for (const MapSelection& selection : selections)
    {
        if (selection.brushId == 0)
            continue;
        if (m_Selections.empty())
            m_SelectionMode = InferSelectionMode(selection);
        if (InferSelectionMode(selection) != m_SelectionMode)
            continue;

        bool alreadySelected = false;
        for (const MapSelection& existing : m_Selections)
        {
            if (SameSelection(existing, selection))
            {
                alreadySelected = true;
                break;
            }
        }
        if (!alreadySelected)
            m_Selections.push_back(selection);
    }

    if (!m_Selections.empty())
        m_Selection = m_Selections.front();
}

bool MapDocument::AddSelection(const MapSelection& selection)
{
    if (selection.brushId == 0)
        return false;
    if (m_Selections.empty())
    {
        SetSelection(selection);
        return true;
    }
    if (InferSelectionMode(selection) != InferSelectionMode(m_Selection))
        return false;
    if (IsSelectionSelected(selection))
        return false;

    m_Selections.push_back(selection);
    return true;
}

bool MapDocument::ToggleSelection(const MapSelection& selection)
{
    if (selection.brushId == 0)
        return false;

    for (size_t i = 0; i < m_Selections.size(); ++i)
    {
        if (!SameSelection(m_Selections[i], selection))
            continue;

        m_Selections.erase(m_Selections.begin() + static_cast<std::ptrdiff_t>(i));
        if (m_Selections.empty())
        {
            m_Selection.Clear();
        }
        else
        {
            if (SameSelection(m_Selection, selection))
                m_Selection = m_Selections.front();
        }
        return true;
    }

    return AddSelection(selection);
}

bool MapDocument::IsSelectionSelected(const MapSelection& selection) const
{
    for (const MapSelection& existing : m_Selections)
    {
        if (SameSelection(existing, selection))
            return true;
    }
    return false;
}

void MapDocument::ClearSelection()
{
    m_Selection.Clear();
    m_Selections.clear();
}

bool MapDocument::IsBrushHidden(uint32 brushId) const
{
    return ContainsBrushId(m_HiddenBrushIds, brushId);
}

bool MapDocument::IsBrushLocked(uint32 brushId) const
{
    return ContainsBrushId(m_LockedBrushIds, brushId);
}

bool MapDocument::SetBrushHidden(uint32 brushId, bool hidden)
{
    if (brushId == 0 || !m_Asset.FindBrush(brushId))
        return false;

    const bool currentlyHidden = IsBrushHidden(brushId);
    if (currentlyHidden == hidden)
        return false;

    if (hidden)
    {
        m_HiddenBrushIds.insert(brushId);
        std::vector<MapSelection> remainingSelections;
        remainingSelections.reserve(m_Selections.size());
        for (const MapSelection& selection : m_Selections)
        {
            if (selection.brushId != brushId)
                remainingSelections.push_back(selection);
        }
        if (remainingSelections.size() != m_Selections.size())
            SetSelections(remainingSelections);
        else if (m_Selection.brushId == brushId)
            ClearSelection();
    }
    else
    {
        m_HiddenBrushIds.erase(brushId);
    }

    ++m_Revision;
    return true;
}

bool MapDocument::SetBrushLocked(uint32 brushId, bool locked)
{
    if (brushId == 0 || !m_Asset.FindBrush(brushId))
        return false;

    const bool currentlyLocked = IsBrushLocked(brushId);
    if (currentlyLocked == locked)
        return false;

    if (locked)
        m_LockedBrushIds.insert(brushId);
    else
        m_LockedBrushIds.erase(brushId);

    ++m_Revision;
    return true;
}

bool MapDocument::HideSelectedBrushes()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    bool changed = false;
    for (const MapSelection& selection : effectiveSelections)
    {
        changed = SetBrushHidden(selection.brushId, true) || changed;
    }
    return changed;
}

bool MapDocument::HideUnselectedBrushes()
{
    std::unordered_set<uint32> selectedBrushIds;
    for (const MapSelection& selection : BuildEffectiveSelections(m_Selection, m_Selections))
    {
        if (selection.brushId != 0)
            selectedBrushIds.insert(selection.brushId);
    }

    bool changed = false;
    for (const MapBrush& brush : m_Asset.brushes)
    {
        if (selectedBrushIds.find(brush.brushId) != selectedBrushIds.end())
            continue;
        changed = SetBrushHidden(brush.brushId, true) || changed;
    }
    return changed;
}

bool MapDocument::UnhideAllBrushes()
{
    if (m_HiddenBrushIds.empty())
        return false;
    m_HiddenBrushIds.clear();
    ++m_Revision;
    return true;
}

bool MapDocument::LockSelectedBrushes()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    bool changed = false;
    for (const MapSelection& selection : effectiveSelections)
    {
        changed = SetBrushLocked(selection.brushId, true) || changed;
    }
    return changed;
}

bool MapDocument::UnlockAllBrushes()
{
    if (m_LockedBrushIds.empty())
        return false;
    m_LockedBrushIds.clear();
    ++m_Revision;
    return true;
}

bool MapDocument::IsolateSelectedBrushes()
{
    const bool unhidAnything = UnhideAllBrushes();
    const bool hidAnything = HideUnselectedBrushes();
    return unhidAnything || hidAnything;
}

MapBrush* MapDocument::GetSelectedBrush()
{
    return m_Selection.brushId != 0 ? m_Asset.FindBrush(m_Selection.brushId) : nullptr;
}

const MapBrush* MapDocument::GetSelectedBrush() const
{
    return m_Selection.brushId != 0 ? m_Asset.FindBrush(m_Selection.brushId) : nullptr;
}

MapFace* MapDocument::GetSelectedFace()
{
    MapBrush* brush = GetSelectedBrush();
    if (!brush || m_Selection.faceIndex < 0 || m_Selection.faceIndex >= static_cast<int>(brush->faces.size()))
        return nullptr;
    return &brush->faces[static_cast<size_t>(m_Selection.faceIndex)];
}

const MapFace* MapDocument::GetSelectedFace() const
{
    const MapBrush* brush = GetSelectedBrush();
    if (!brush || m_Selection.faceIndex < 0 || m_Selection.faceIndex >= static_cast<int>(brush->faces.size()))
        return nullptr;
    return &brush->faces[static_cast<size_t>(m_Selection.faceIndex)];
}

Vec3 MapDocument::GetSelectedBrushCenter() const
{
    const MapBrush* brush = GetSelectedBrush();
    return brush ? ComputeBrushCenter(*brush) : Vec3::Zero();
}

Vec3 MapDocument::GetSelectionPivot() const
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return Vec3::Zero();

    Vec3 pivot = Vec3::Zero();
    int pivotCount = 0;

    for (const MapSelection& selection : effectiveSelections)
    {
        const MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush)
            continue;

        if (selection.vertexIndex >= 0 && selection.vertexIndex < static_cast<int>(brush->vertices.size()))
        {
            pivot += brush->vertices[static_cast<size_t>(selection.vertexIndex)];
            ++pivotCount;
            continue;
        }

        if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0 &&
            selection.edgeVertexA < static_cast<int>(brush->vertices.size()) &&
            selection.edgeVertexB < static_cast<int>(brush->vertices.size()))
        {
            pivot += (brush->vertices[static_cast<size_t>(selection.edgeVertexA)] +
                      brush->vertices[static_cast<size_t>(selection.edgeVertexB)]) *
                     0.5f;
            ++pivotCount;
            continue;
        }

        if (selection.faceIndex >= 0 && selection.faceIndex < static_cast<int>(brush->faces.size()))
        {
            const MapFace& face = brush->faces[static_cast<size_t>(selection.faceIndex)];
            if (face.vertexIndices.empty())
                continue;

            Vec3 facePivot = Vec3::Zero();
            for (uint32 vertexIndex : face.vertexIndices)
                facePivot += brush->vertices[vertexIndex];
            pivot += facePivot / static_cast<float>(face.vertexIndices.size());
            ++pivotCount;
            continue;
        }

        pivot += ComputeBrushCenter(*brush);
        ++pivotCount;
    }

    return pivotCount > 0 ? (pivot / static_cast<float>(pivotCount)) : Vec3::Zero();
}

uint32 MapDocument::CreateBoxBrush(const Vec3& center, const Vec3& halfExtents, const std::string& defaultMaterialPath)
{
    const uint32 brushId = m_Asset.nextBrushId++;
    m_Asset.brushes.push_back(Dot::CreateBoxBrush(brushId, center, halfExtents, defaultMaterialPath));
    SetSelection(MapSelection{brushId});
    MarkDirty();
    return brushId;
}

uint32 MapDocument::PasteBrush(const MapBrush& brush, const Vec3& offset, bool appendCopySuffix)
{
    MapBrush duplicate = brush;
    duplicate.brushId = m_Asset.nextBrushId++;
    if (appendCopySuffix)
        duplicate.name += " Copy";

    for (Vec3& vertex : duplicate.vertices)
        vertex += offset;

    m_Asset.brushes.push_back(std::move(duplicate));
    SetSelection(MapSelection{m_Asset.brushes.back().brushId});
    MarkDirty();
    return m_Selection.brushId;
}

bool MapDocument::DeleteSelectedBrush()
{
    if (m_Selections.empty() && m_Selection.brushId == 0)
        return false;

    std::vector<uint32> selectedBrushIds;
    if (!m_Selections.empty())
    {
        for (const MapSelection& selection : m_Selections)
        {
            if (selection.brushId == 0)
                continue;
            if (std::find(selectedBrushIds.begin(), selectedBrushIds.end(), selection.brushId) == selectedBrushIds.end())
                selectedBrushIds.push_back(selection.brushId);
        }
    }
    else if (m_Selection.brushId != 0)
    {
        selectedBrushIds.push_back(m_Selection.brushId);
    }

    const auto it = std::remove_if(
        m_Asset.brushes.begin(), m_Asset.brushes.end(),
        [&](const MapBrush& brush)
        { return std::find(selectedBrushIds.begin(), selectedBrushIds.end(), brush.brushId) != selectedBrushIds.end(); });
    if (it == m_Asset.brushes.end())
        return false;

    m_Asset.brushes.erase(it, m_Asset.brushes.end());
    ClearSelection();
    MarkDirty();
    return true;
}

bool MapDocument::DuplicateSelectedBrush()
{
    std::vector<const MapBrush*> selectedBrushes;
    if (!m_Selections.empty())
    {
        for (const MapSelection& selection : m_Selections)
        {
            if (selection.brushId == 0)
                continue;
            const MapBrush* brush = m_Asset.FindBrush(selection.brushId);
            if (!brush)
                continue;
            if (std::find(selectedBrushes.begin(), selectedBrushes.end(), brush) == selectedBrushes.end())
                selectedBrushes.push_back(brush);
        }
    }
    else
    {
        const MapBrush* brush = GetSelectedBrush();
        if (brush)
            selectedBrushes.push_back(brush);
    }

    if (selectedBrushes.empty())
        return false;

    std::vector<MapSelection> duplicatedSelections;
    duplicatedSelections.reserve(selectedBrushes.size());
    const Vec3 pasteOffset(1.0f, 0.0f, 0.0f);
    for (const MapBrush* brush : selectedBrushes)
    {
        const uint32 pastedId = PasteBrush(*brush, pasteOffset, true);
        if (pastedId != 0)
            duplicatedSelections.push_back(MapSelection{pastedId});
    }

    if (duplicatedSelections.empty())
        return false;

    SetSelections(duplicatedSelections);
    return true;
}

bool MapDocument::HollowSelectedBrush(float wallThickness)
{
    MapBrush* selectedBrush = GetSelectedBrush();
    if (!selectedBrush)
        return false;

    std::vector<MapBrush> hollowBrushes;
    if (!BuildHollowBrushes(*selectedBrush, wallThickness, hollowBrushes))
        return false;

    const uint32 originalBrushId = selectedBrush->brushId;
    const std::string baseName =
        selectedBrush->name.empty() ? ("Brush " + std::to_string(originalBrushId)) : selectedBrush->name;
    for (size_t i = 0; i < hollowBrushes.size(); ++i)
    {
        hollowBrushes[i].brushId = (i == 0) ? originalBrushId : m_Asset.nextBrushId++;
        hollowBrushes[i].name = baseName + " Shell " + std::to_string(i + 1);
    }

    auto selectedIt = std::find_if(m_Asset.brushes.begin(), m_Asset.brushes.end(),
                                   [&](const MapBrush& brush) { return brush.brushId == originalBrushId; });
    if (selectedIt == m_Asset.brushes.end())
        return false;

    const size_t replaceIndex = static_cast<size_t>(std::distance(m_Asset.brushes.begin(), selectedIt));
    *selectedIt = std::move(hollowBrushes.front());
    for (size_t i = 1; i < hollowBrushes.size(); ++i)
    {
        m_Asset.brushes.insert(m_Asset.brushes.begin() + static_cast<std::ptrdiff_t>(replaceIndex + i),
                               std::move(hollowBrushes[i]));
    }

    SetSelection(MapSelection{originalBrushId});
    m_SelectionMode = MapSelectionMode::Brush;
    MarkDirty();
    return true;
}

bool MapDocument::BuildSelectedBrushHollowPreview(float wallThickness, std::vector<MapBrush>& outBrushes) const
{
    outBrushes.clear();
    const MapBrush* selectedBrush = GetSelectedBrush();
    if (!selectedBrush)
        return false;

    return BuildHollowBrushes(*selectedBrush, wallThickness, outBrushes);
}

bool MapDocument::TranslateSelectedBrush(const Vec3& delta, bool rebuildStaticWorld)
{
    const bool changed = ForEachUniqueSelectedBrush(m_Asset, m_Selection, m_Selections,
                                                    [&](MapBrush& brush)
                                                    {
                                                        if (IsBrushLocked(brush.brushId))
                                                            return false;
                                                        for (Vec3& vertex : brush.vertices)
                                                            vertex += delta;
                                                        return true;
                                                    });
    if (!changed)
        return false;

    MarkDirty(true, rebuildStaticWorld);
    return true;
}

bool MapDocument::TranslateSelectedFace(float distance, bool rebuildStaticWorld)
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return false;

    std::unordered_map<uint32, std::vector<Vec3>> originalVertices;
    bool changed = false;

    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0 || selection.faceIndex < 0)
            continue;
        if (IsBrushLocked(selection.brushId))
            continue;

        MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;

        if (originalVertices.find(selection.brushId) == originalVertices.end())
            originalVertices.emplace(selection.brushId, brush->vertices);

        MapFace& face = brush->faces[static_cast<size_t>(selection.faceIndex)];
        const Vec3 normal = ComputeMapFaceNormal(*brush, face);
        for (uint32 vertexIndex : face.vertexIndices)
            brush->vertices[vertexIndex] += normal * distance;
        changed = true;
    }

    if (!changed)
        return false;

    for (const auto& [brushId, previousVertices] : originalVertices)
    {
        MapBrush* brush = m_Asset.FindBrush(brushId);
        if (!brush || ValidateMapBrushConvex(*brush))
            continue;

        for (const auto& [revertBrushId, revertVertices] : originalVertices)
        {
            if (MapBrush* revertBrush = m_Asset.FindBrush(revertBrushId))
                revertBrush->vertices = revertVertices;
        }
        return false;
    }

    MarkDirty(true, rebuildStaticWorld);
    return true;
}

bool MapDocument::ExtrudeSelectedFace(float distance)
{
    MapBrush* brush = GetSelectedBrush();
    MapFace* face = GetSelectedFace();
    if (!brush || !face)
        return false;

    MapBrush extrudedBrush;
    int extrudedFaceIndex = -1;
    if (!BuildExtrudedBrush(*brush, m_Selection.faceIndex, distance, extrudedBrush, extrudedFaceIndex))
        return false;

    *brush = std::move(extrudedBrush);
    MapSelection selection;
    selection.brushId = brush->brushId;
    selection.faceIndex = extrudedFaceIndex;
    SetSelection(selection);
    m_SelectionMode = MapSelectionMode::Face;
    MarkDirty();
    return true;
}

bool MapDocument::ClipSelectedBrush(float planeOffset, bool flipPlane)
{
    MapBrush* brush = GetSelectedBrush();
    MapFace* selectedFace = GetSelectedFace();
    if (!brush || !selectedFace)
        return false;

    const Vec3 selectedFacePoint = brush->vertices[selectedFace->vertexIndices.front()];
    const Vec3 baseNormal = ComputeMapFaceNormal(*brush, *selectedFace);
    const Vec3 planePoint = selectedFacePoint + (baseNormal * planeOffset);
    const Vec3 keepNormal = flipPlane ? (baseNormal * -1.0f) : baseNormal;

    MapBrush clippedBrush;
    int capFaceIndex = -1;
    if (!BuildClippedBrush(*brush, planePoint, keepNormal, selectedFace->materialPath, selectedFace->uv, clippedBrush,
                           capFaceIndex))
    {
        return false;
    }

    *brush = std::move(clippedBrush);
    MapSelection selection;
    selection.brushId = brush->brushId;
    selection.faceIndex = capFaceIndex;
    SetSelection(selection);
    m_SelectionMode = MapSelectionMode::Face;
    MarkDirty();
    return true;
}

bool MapDocument::BuildSelectedBrushClipPreview(float planeOffset, bool flipPlane, std::vector<Vec3>& outPolygon,
                                                Vec3& outPlaneNormal) const
{
    outPolygon.clear();
    outPlaneNormal = Vec3::Zero();

    const MapBrush* brush = GetSelectedBrush();
    const MapFace* selectedFace = GetSelectedFace();
    if (!brush || !selectedFace)
        return false;

    const Vec3 selectedFacePoint = brush->vertices[selectedFace->vertexIndices.front()];
    const Vec3 baseNormal = ComputeMapFaceNormal(*brush, *selectedFace);
    const Vec3 planePoint = selectedFacePoint + (baseNormal * planeOffset);
    const Vec3 keepNormal = flipPlane ? (baseNormal * -1.0f) : baseNormal;

    MapBrush clippedBrush;
    int capFaceIndex = -1;
    if (!BuildClippedBrush(*brush, planePoint, keepNormal, selectedFace->materialPath, selectedFace->uv, clippedBrush,
                           capFaceIndex))
    {
        return false;
    }

    if (capFaceIndex < 0 || capFaceIndex >= static_cast<int>(clippedBrush.faces.size()))
        return false;

    const MapFace& capFace = clippedBrush.faces[static_cast<size_t>(capFaceIndex)];
    if (capFace.vertexIndices.size() < 3)
        return false;

    outPolygon.reserve(capFace.vertexIndices.size());
    for (uint32 vertexIndex : capFace.vertexIndices)
        outPolygon.push_back(clippedBrush.vertices[vertexIndex]);

    outPlaneNormal = ComputeMapFaceNormal(clippedBrush, capFace);
    return outPlaneNormal.LengthSquared() > 1e-8f;
}

bool MapDocument::BuildSelectedFaceExtrudePreview(float distance, std::vector<Vec3>& outPolygon, Vec3& outFaceNormal) const
{
    outPolygon.clear();
    outFaceNormal = Vec3::Zero();

    const MapBrush* brush = GetSelectedBrush();
    const MapFace* selectedFace = GetSelectedFace();
    if (!brush || !selectedFace)
        return false;

    MapBrush extrudedBrush;
    int extrudedFaceIndex = -1;
    if (!BuildExtrudedBrush(*brush, m_Selection.faceIndex, distance, extrudedBrush, extrudedFaceIndex))
        return false;

    if (extrudedFaceIndex < 0 || extrudedFaceIndex >= static_cast<int>(extrudedBrush.faces.size()))
        return false;

    const MapFace& extrudedFace = extrudedBrush.faces[static_cast<size_t>(extrudedFaceIndex)];
    if (extrudedFace.vertexIndices.size() < 3)
        return false;

    outPolygon.reserve(extrudedFace.vertexIndices.size());
    for (uint32 vertexIndex : extrudedFace.vertexIndices)
        outPolygon.push_back(extrudedBrush.vertices[vertexIndex]);

    outFaceNormal = ComputeMapFaceNormal(extrudedBrush, extrudedFace);
    return outFaceNormal.LengthSquared() > 1e-8f;
}

bool MapDocument::TranslateSelectedEdge(const Vec3& delta, bool rebuildStaticWorld)
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return false;

    std::unordered_map<uint32, std::vector<Vec3>> originalVertices;
    bool changed = false;
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0 || selection.edgeVertexA < 0 || selection.edgeVertexB < 0)
            continue;
        if (IsBrushLocked(selection.brushId))
            continue;

        MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.edgeVertexA >= static_cast<int>(brush->vertices.size()) ||
            selection.edgeVertexB >= static_cast<int>(brush->vertices.size()))
        {
            continue;
        }

        if (originalVertices.find(selection.brushId) == originalVertices.end())
            originalVertices.emplace(selection.brushId, brush->vertices);

        brush->vertices[static_cast<size_t>(selection.edgeVertexA)] += delta;
        brush->vertices[static_cast<size_t>(selection.edgeVertexB)] += delta;
        changed = true;
    }

    if (!changed)
        return false;

    for (const auto& [brushId, previousVertices] : originalVertices)
    {
        MapBrush* brush = m_Asset.FindBrush(brushId);
        if (!brush || ValidateMapBrushConvex(*brush))
            continue;

        for (const auto& [revertBrushId, revertVertices] : originalVertices)
        {
            if (MapBrush* revertBrush = m_Asset.FindBrush(revertBrushId))
                revertBrush->vertices = revertVertices;
        }
        return false;
    }

    MarkDirty(true, rebuildStaticWorld);
    return true;
}

bool MapDocument::TranslateSelectedVertex(const Vec3& delta, bool rebuildStaticWorld)
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return false;

    std::unordered_map<uint32, std::vector<Vec3>> originalVertices;
    bool changed = false;
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0 || selection.vertexIndex < 0)
            continue;
        if (IsBrushLocked(selection.brushId))
            continue;

        MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.vertexIndex >= static_cast<int>(brush->vertices.size()))
            continue;

        if (originalVertices.find(selection.brushId) == originalVertices.end())
            originalVertices.emplace(selection.brushId, brush->vertices);

        brush->vertices[static_cast<size_t>(selection.vertexIndex)] += delta;
        changed = true;
    }

    if (!changed)
        return false;

    for (const auto& [brushId, previousVertices] : originalVertices)
    {
        MapBrush* brush = m_Asset.FindBrush(brushId);
        if (!brush || ValidateMapBrushConvex(*brush))
            continue;

        for (const auto& [revertBrushId, revertVertices] : originalVertices)
        {
            if (MapBrush* revertBrush = m_Asset.FindBrush(revertBrushId))
                revertBrush->vertices = revertVertices;
        }
        return false;
    }

    MarkDirty(true, rebuildStaticWorld);
    return true;
}

bool MapDocument::SetSelectedFaceMaterial(const std::string& materialPath)
{
    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                             [&](MapBrush&, MapFace& face, const MapSelection& selection)
                                             {
                                               if (IsBrushLocked(selection.brushId))
                                                   return false;
                                               if (face.materialPath == materialPath)
                                                   return false;
                                                 face.materialPath = materialPath;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::SetSelectedBrushMaterial(const std::string& materialPath)
{
    bool changed = ForEachUniqueSelectedBrush(m_Asset, m_Selection, m_Selections,
                                                [&](MapBrush& brush)
                                                {
                                                    if (IsBrushLocked(brush.brushId))
                                                        return false;
                                                    bool brushChanged = false;
                                                  for (MapFace& face : brush.faces)
                                                  {
                                                      if (face.materialPath == materialPath)
                                                          continue;
                                                      face.materialPath = materialPath;
                                                      brushChanged = true;
                                                  }
                                                  return brushChanged;
                                              });

    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::SetSelectedFaceSurface(const std::string& materialPath, const MapFaceUV& uv)
{
    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                             [&](MapBrush&, MapFace& face, const MapSelection& selection)
                                             {
                                               if (IsBrushLocked(selection.brushId))
                                                   return false;
                                               if (face.materialPath == materialPath && face.uv.projectionMode == uv.projectionMode &&
                                                     face.uv.scaleU == uv.scaleU && face.uv.scaleV == uv.scaleV &&
                                                     face.uv.offsetU == uv.offsetU && face.uv.offsetV == uv.offsetV &&
                                                     face.uv.rotationDeg == uv.rotationDeg)
                                                 {
                                                     return false;
                                                 }

                                                 face.materialPath = materialPath;
                                                 face.uv = uv;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::SetSelectedFaceUV(const MapFaceUV& uv)
{
    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                             [&](MapBrush&, MapFace& face, const MapSelection& selection)
                                             {
                                               if (IsBrushLocked(selection.brushId))
                                                   return false;
                                               if (face.uv.projectionMode == uv.projectionMode && face.uv.scaleU == uv.scaleU &&
                                                     face.uv.scaleV == uv.scaleV && face.uv.offsetU == uv.offsetU &&
                                                     face.uv.offsetV == uv.offsetV &&
                                                     face.uv.rotationDeg == uv.rotationDeg)
                                                 {
                                                     return false;
                                                 }

                                                 face.uv = uv;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::FitSelectedFaceUV()
{
    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                           [&](MapBrush& brush, MapFace& face, const MapSelection&)
                                           {
                                               if (IsBrushLocked(brush.brushId))
                                                   return false;
                                               const MapFaceUV fittedUv = BuildFittedFaceUv(brush, face);
                                                 if (face.uv.projectionMode == fittedUv.projectionMode &&
                                                     face.uv.scaleU == fittedUv.scaleU && face.uv.scaleV == fittedUv.scaleV &&
                                                     face.uv.offsetU == fittedUv.offsetU && face.uv.offsetV == fittedUv.offsetV &&
                                                     face.uv.rotationDeg == fittedUv.rotationDeg)
                                                 {
                                                     return false;
                                                 }

                                                 face.uv = fittedUv;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::NudgeSelectedFaceUV(float deltaU, float deltaV)
{
    if (std::abs(deltaU) <= 1e-6f && std::abs(deltaV) <= 1e-6f)
        return false;

    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                             [&](MapBrush&, MapFace& face, const MapSelection& selection)
                                             {
                                               if (IsBrushLocked(selection.brushId))
                                                   return false;
                                               face.uv.offsetU += deltaU;
                                                 face.uv.offsetV += deltaV;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::RotateSelectedFaceUV(float deltaDegrees)
{
    if (std::abs(deltaDegrees) <= 1e-6f)
        return false;

    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                             [&](MapBrush&, MapFace& face, const MapSelection& selection)
                                             {
                                               if (IsBrushLocked(selection.brushId))
                                                   return false;
                                               face.uv.rotationDeg += deltaDegrees;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::FlipSelectedFaceUV(bool flipU, bool flipV)
{
    if (!flipU && !flipV)
        return false;

    const bool changed = ForEachSelectedFace(m_Asset, m_Selection, m_Selections,
                                             [&](MapBrush&, MapFace& face, const MapSelection& selection)
                                             {
                                               if (IsBrushLocked(selection.brushId))
                                                   return false;
                                               if (flipU)
                                                     face.uv.scaleU *= -1.0f;
                                                 if (flipV)
                                                     face.uv.scaleV *= -1.0f;
                                                 return true;
                                             });
    if (!changed)
        return false;

    MarkDirty();
    return true;
}

bool MapDocument::SelectAllCoplanarFaces()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return false;

    struct SeedPlane
    {
        Vec3 normal = Vec3::Zero();
        float d = 0.0f;
    };

    std::vector<SeedPlane> seedPlanes;
    seedPlanes.reserve(effectiveSelections.size());
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0 || selection.faceIndex < 0)
            continue;

        const MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;

        const MapFace& face = brush->faces[static_cast<size_t>(selection.faceIndex)];
        const Vec3 normal = ComputeMapFaceNormal(*brush, face);
        if (normal.LengthSquared() <= 1e-8f)
            continue;

        const Vec3 pointOnPlane = brush->vertices[face.vertexIndices.front()];
        SeedPlane plane;
        plane.normal = normal;
        plane.d = -Vec3::Dot(normal, pointOnPlane);
        seedPlanes.push_back(plane);
    }

    if (seedPlanes.empty())
        return false;

    constexpr float kCoplanarNormalThreshold = 0.999f;
    constexpr float kCoplanarPlaneTolerance = 0.05f;
    std::vector<MapSelection> selections;

    for (const MapBrush& brush : m_Asset.brushes)
    {
        for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
        {
            const MapFace& candidateFace = brush.faces[faceIndex];
            const Vec3 candidateNormal = ComputeMapFaceNormal(brush, candidateFace);
            if (candidateNormal.LengthSquared() <= 1e-8f || candidateFace.vertexIndices.empty())
                continue;

            const Vec3 pointOnCandidate = brush.vertices[candidateFace.vertexIndices.front()];
            for (const SeedPlane& seedPlane : seedPlanes)
            {
                if (std::abs(Vec3::Dot(candidateNormal, seedPlane.normal)) < kCoplanarNormalThreshold)
                    continue;

                const float planeDistance = std::abs(Vec3::Dot(seedPlane.normal, pointOnCandidate) + seedPlane.d);
                if (planeDistance > kCoplanarPlaneTolerance)
                    continue;

                MapSelection selection;
                selection.brushId = brush.brushId;
                selection.faceIndex = static_cast<int>(faceIndex);
                selections.push_back(selection);
                break;
            }
        }
    }

    if (selections.empty())
        return false;

    SetSelections(selections);
    return true;
}

bool MapDocument::SelectFacesWithSameMaterial()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return false;

    std::unordered_set<std::string> materialPaths;
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0 || selection.faceIndex < 0)
            continue;

        const MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;

        materialPaths.insert(brush->faces[static_cast<size_t>(selection.faceIndex)].materialPath);
    }

    if (materialPaths.empty())
        return false;

    std::vector<MapSelection> selections;
    for (const MapBrush& brush : m_Asset.brushes)
    {
        for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
        {
            if (!materialPaths.contains(brush.faces[faceIndex].materialPath))
                continue;

            MapSelection selection;
            selection.brushId = brush.brushId;
            selection.faceIndex = static_cast<int>(faceIndex);
            selections.push_back(selection);
        }
    }

    if (selections.empty())
        return false;

    SetSelections(selections);
    return true;
}

bool MapDocument::SelectLinkedBrushFaces()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty())
        return false;

    std::vector<uint32> brushIds;
    brushIds.reserve(effectiveSelections.size());
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId == 0)
            continue;
        if (std::find(brushIds.begin(), brushIds.end(), selection.brushId) == brushIds.end())
            brushIds.push_back(selection.brushId);
    }

    if (brushIds.empty())
        return false;

    std::vector<MapSelection> selections;
    for (uint32 brushId : brushIds)
    {
        const MapBrush* brush = m_Asset.FindBrush(brushId);
        if (!brush)
            continue;

        for (size_t faceIndex = 0; faceIndex < brush->faces.size(); ++faceIndex)
        {
            MapSelection selection;
            selection.brushId = brushId;
            selection.faceIndex = static_cast<int>(faceIndex);
            selections.push_back(selection);
        }
    }

    if (selections.empty())
        return false;

    SetSelections(selections);
    return true;
}

bool MapDocument::GrowFaceSelection()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty() || effectiveSelections.front().faceIndex < 0)
        return false;

    std::unordered_set<uint64> selectionSet;
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId != 0 && selection.faceIndex >= 0)
            selectionSet.insert(BuildMapSelectionKey(selection));
    }
    if (selectionSet.empty())
        return false;

    std::vector<MapSelection> grownSelections = effectiveSelections;
    for (const MapSelection& selection : effectiveSelections)
    {
        const MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.faceIndex < 0 || selection.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;

        const MapFace& sourceFace = brush->faces[static_cast<size_t>(selection.faceIndex)];
        for (size_t faceIndex = 0; faceIndex < brush->faces.size(); ++faceIndex)
        {
            if (static_cast<int>(faceIndex) == selection.faceIndex)
                continue;

            if (!FacesShareEdge(sourceFace, brush->faces[faceIndex]))
                continue;

            MapSelection adjacentSelection;
            adjacentSelection.brushId = brush->brushId;
            adjacentSelection.faceIndex = static_cast<int>(faceIndex);
            const uint64 adjacentKey = BuildMapSelectionKey(adjacentSelection);
            if (!selectionSet.contains(adjacentKey))
            {
                selectionSet.insert(adjacentKey);
                grownSelections.push_back(adjacentSelection);
            }
        }
    }

    if (grownSelections.size() == effectiveSelections.size())
        return false;

    SetSelections(grownSelections);
    return true;
}

bool MapDocument::ShrinkFaceSelection()
{
    const std::vector<MapSelection> effectiveSelections = BuildEffectiveSelections(m_Selection, m_Selections);
    if (effectiveSelections.empty() || effectiveSelections.front().faceIndex < 0)
        return false;

    std::unordered_set<uint64> selectionSet;
    for (const MapSelection& selection : effectiveSelections)
    {
        if (selection.brushId != 0 && selection.faceIndex >= 0)
            selectionSet.insert(BuildMapSelectionKey(selection));
    }
    if (selectionSet.empty())
        return false;

    std::vector<MapSelection> shrunkenSelections;
    for (const MapSelection& selection : effectiveSelections)
    {
        const MapBrush* brush = m_Asset.FindBrush(selection.brushId);
        if (!brush || selection.faceIndex < 0 || selection.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;

        const MapFace& sourceFace = brush->faces[static_cast<size_t>(selection.faceIndex)];
        bool foundNeighbor = false;
        bool boundaryFace = false;
        for (size_t faceIndex = 0; faceIndex < brush->faces.size(); ++faceIndex)
        {
            if (static_cast<int>(faceIndex) == selection.faceIndex)
                continue;
            if (!FacesShareEdge(sourceFace, brush->faces[faceIndex]))
                continue;

            foundNeighbor = true;
            MapSelection adjacentSelection;
            adjacentSelection.brushId = brush->brushId;
            adjacentSelection.faceIndex = static_cast<int>(faceIndex);
            if (!selectionSet.contains(BuildMapSelectionKey(adjacentSelection)))
            {
                boundaryFace = true;
                break;
            }
        }

        if (foundNeighbor && !boundaryFace)
            shrunkenSelections.push_back(selection);
    }

    if (shrunkenSelections.size() == effectiveSelections.size())
        return false;

    if (shrunkenSelections.empty())
        ClearSelection();
    else
        SetSelections(shrunkenSelections);
    return true;
}

bool MapDocument::RenameSelectedBrush(const std::string& name)
{
    MapBrush* brush = GetSelectedBrush();
    if (!brush)
        return false;

    brush->name = name;
    MarkDirty();
    return true;
}

bool MapDocument::Pick(const Ray& ray, MapSelection& outSelection, float* outDistance) const
{
    switch (m_SelectionMode)
    {
        case MapSelectionMode::Brush:
            return PickBrush(ray, outSelection, outDistance);
        case MapSelectionMode::Face:
            return PickFace(ray, outSelection, outDistance);
        case MapSelectionMode::Edge:
            return PickEdge(ray, outSelection, outDistance);
        case MapSelectionMode::Vertex:
            return PickVertex(ray, outSelection, outDistance);
        default:
            return false;
    }
}

void MapDocument::ApplySnapshot(const MapAsset& asset, const std::vector<MapSelection>& selections,
                                const MapSelection& selection, const std::unordered_set<uint32>& hiddenBrushIds,
                                const std::unordered_set<uint32>& lockedBrushIds, bool dirty)
{
    m_Asset = asset;
    m_HiddenBrushIds = hiddenBrushIds;
    m_LockedBrushIds = lockedBrushIds;
    SetSelections(selections);
    if (m_Selections.empty() && selection.brushId != 0)
        SetSelection(selection);
    else if (!m_Selections.empty())
        m_Selection = selection.brushId != 0 ? selection : m_Selections.front();
    m_Dirty = dirty;
    ++m_Revision;
    RebuildCompiledData();
}

void MapDocument::RebuildCompiledData(bool rebuildStaticWorld)
{
    m_Compiled = MapCompiler::Compile(m_Asset, m_Revision);
    if (rebuildStaticWorld)
    {
        if (!m_StaticWorldGeometry)
            m_StaticWorldGeometry = std::make_shared<StaticWorldGeometry>();
        m_StaticWorldGeometry->Build(m_Compiled);
    }
}

void MapDocument::MarkDirty(bool rebuildCompiledData, bool rebuildStaticWorld)
{
    ++m_Revision;
    m_Dirty = true;
    if (rebuildCompiledData)
        RebuildCompiledData(rebuildStaticWorld);
}

bool MapDocument::PickBrush(const Ray& ray, MapSelection& outSelection, float* outDistance) const
{
    MapSelection faceSelection;
    if (!PickFace(ray, faceSelection, outDistance))
        return false;

    outSelection.Clear();
    outSelection.brushId = faceSelection.brushId;
    return true;
}

bool MapDocument::PickFace(const Ray& ray, MapSelection& outSelection, float* outDistance) const
{
    bool found = false;
    float bestDistance = std::numeric_limits<float>::max();
    MapSelection bestSelection;

    for (const MapCompiledTriangle& triangle : m_Compiled.collisionTriangles)
    {
        if (IsBrushHidden(triangle.brushId) || IsBrushLocked(triangle.brushId))
            continue;
        float distance = 0.0f;
        if (!RaycastTriangleOnly(ray, triangle, distance))
            continue;

        if (!found || distance < bestDistance)
        {
            bestDistance = distance;
            bestSelection.Clear();
            bestSelection.brushId = triangle.brushId;
            bestSelection.faceIndex = static_cast<int>(triangle.faceIndex);
            found = true;
        }
    }

    if (found)
    {
        outSelection = bestSelection;
        if (outDistance)
            *outDistance = bestDistance;
    }

    return found;
}

bool MapDocument::PickVertex(const Ray& ray, MapSelection& outSelection, float* outDistance) const
{
    bool found = false;
    float bestDistanceToRay = std::numeric_limits<float>::max();
    float bestRayT = 0.0f;
    MapSelection bestSelection;

    for (const MapBrush& brush : m_Asset.brushes)
    {
        if (IsBrushHidden(brush.brushId) || IsBrushLocked(brush.brushId))
            continue;
        for (size_t vertexIndex = 0; vertexIndex < brush.vertices.size(); ++vertexIndex)
        {
            float rayT = 0.0f;
            const float distanceToRay = DistancePointToRay(brush.vertices[vertexIndex], ray, rayT);
            const float threshold = std::max(0.1f, rayT * 0.02f);
            if (distanceToRay > threshold)
                continue;

            if (!found || distanceToRay < bestDistanceToRay)
            {
                bestDistanceToRay = distanceToRay;
                bestRayT = rayT;
                bestSelection.Clear();
                bestSelection.brushId = brush.brushId;
                bestSelection.vertexIndex = static_cast<int>(vertexIndex);
                found = true;
            }
        }
    }

    if (found)
    {
        outSelection = bestSelection;
        if (outDistance)
            *outDistance = bestRayT;
    }
    return found;
}

bool MapDocument::PickEdge(const Ray& ray, MapSelection& outSelection, float* outDistance) const
{
    bool found = false;
    float bestDistanceToRay = std::numeric_limits<float>::max();
    float bestRayT = 0.0f;
    MapSelection bestSelection;

    for (const MapBrush& brush : m_Asset.brushes)
    {
        if (IsBrushHidden(brush.brushId) || IsBrushLocked(brush.brushId))
            continue;
        std::unordered_set<uint64> seenEdges;
        for (const MapFace& face : brush.faces)
        {
            for (size_t edgeIndex = 0; edgeIndex < face.vertexIndices.size(); ++edgeIndex)
            {
                const uint32 a = face.vertexIndices[edgeIndex];
                const uint32 b = face.vertexIndices[(edgeIndex + 1) % face.vertexIndices.size()];
                const uint32 minEdge = std::min(a, b);
                const uint32 maxEdge = std::max(a, b);
                const uint64 edgeKey = (static_cast<uint64>(minEdge) << 32ull) | static_cast<uint64>(maxEdge);
                if (!seenEdges.insert(edgeKey).second)
                    continue;

                float rayT = 0.0f;
                const float distanceToRay = DistanceRayToSegment(ray, brush.vertices[a], brush.vertices[b], rayT);
                const float threshold = std::max(0.12f, rayT * 0.02f);
                if (distanceToRay > threshold)
                    continue;

                if (!found || distanceToRay < bestDistanceToRay)
                {
                    bestDistanceToRay = distanceToRay;
                    bestRayT = rayT;
                    bestSelection.Clear();
                    bestSelection.brushId = brush.brushId;
                    bestSelection.edgeVertexA = static_cast<int>(a);
                    bestSelection.edgeVertexB = static_cast<int>(b);
                    found = true;
                }
            }
        }
    }

    if (found)
    {
        outSelection = bestSelection;
        if (outDistance)
            *outDistance = bestRayT;
    }
    return found;
}

} // namespace Dot
