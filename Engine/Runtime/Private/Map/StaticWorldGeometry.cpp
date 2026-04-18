#include "Core/Map/StaticWorldGeometry.h"

#include <algorithm>
#include <limits>

namespace Dot
{

namespace
{

float Clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

Vec3 ClosestPointOnTriangle(const Vec3& point, const MapCompiledTriangle& triangle)
{
    const Vec3 ab = triangle.b - triangle.a;
    const Vec3 ac = triangle.c - triangle.a;
    const Vec3 ap = point - triangle.a;

    const float d1 = Vec3::Dot(ab, ap);
    const float d2 = Vec3::Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
        return triangle.a;

    const Vec3 bp = point - triangle.b;
    const float d3 = Vec3::Dot(ab, bp);
    const float d4 = Vec3::Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
        return triangle.b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        return triangle.a + (ab * v);
    }

    const Vec3 cp = point - triangle.c;
    const float d5 = Vec3::Dot(ab, cp);
    const float d6 = Vec3::Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
        return triangle.c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return triangle.a + (ac * w);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const Vec3 bc = triangle.c - triangle.b;
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return triangle.b + (bc * w);
    }

    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return triangle.a + (ab * v) + (ac * w);
}

bool RaycastTriangle(const Ray& ray, const MapCompiledTriangle& triangle, float maxDistance, StaticWorldHit& outHit)
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
    if (distance < 0.0f || distance > maxDistance)
        return false;

    outHit.hit = true;
    outHit.distance = distance;
    outHit.point = ray.GetPoint(distance);
    outHit.normal = triangle.normal;
    outHit.brushId = triangle.brushId;
    outHit.faceIndex = triangle.faceIndex;
    return true;
}

bool ResolveSpherePenetration(const std::vector<MapCompiledTriangle>& triangles, Vec3& center, float radius, StaticWorldHit* outHit)
{
    bool resolved = false;
    StaticWorldHit lastHit;

    for (int iteration = 0; iteration < 4; ++iteration)
    {
        bool foundPenetration = false;
        float bestPenetration = 0.0f;
        Vec3 bestPush = Vec3::Zero();
        StaticWorldHit bestHit;

        for (const MapCompiledTriangle& triangle : triangles)
        {
            const Vec3 closestPoint = ClosestPointOnTriangle(center, triangle);
            const Vec3 delta = center - closestPoint;
            const float distanceSq = delta.LengthSquared();
            if (distanceSq >= (radius * radius))
                continue;

            const float distance = std::sqrt(std::max(distanceSq, 0.0f));
            const float penetration = radius - distance;
            Vec3 pushNormal = distance > 1e-5f ? (delta * (1.0f / distance)) : triangle.normal;
            if (Vec3::Dot(pushNormal, triangle.normal) < 0.0f)
                pushNormal = triangle.normal;
            if (triangle.normal.y > 0.5f && pushNormal.y > 0.0f)
                pushNormal = Vec3::Up();

            if (!foundPenetration || penetration > bestPenetration)
            {
                foundPenetration = true;
                bestPenetration = penetration;
                bestPush = pushNormal * (penetration + 0.001f);
                bestHit.hit = true;
                bestHit.distance = distance;
                bestHit.point = closestPoint;
                bestHit.normal = triangle.normal;
                bestHit.brushId = triangle.brushId;
                bestHit.faceIndex = triangle.faceIndex;
            }
        }

        if (!foundPenetration)
            break;

        center += bestPush;
        lastHit = bestHit;
        resolved = true;
    }

    if (resolved && outHit)
        *outHit = lastHit;
    return resolved;
}

} // namespace

void StaticWorldGeometry::Clear()
{
    m_Triangles.clear();
    ++m_Revision;
}

void StaticWorldGeometry::Build(const MapCompiledData& compiledData)
{
    m_Triangles = compiledData.collisionTriangles;
    ++m_Revision;
}

bool StaticWorldGeometry::Raycast(const Ray& ray, float maxDistance, StaticWorldHit& outHit) const
{
    bool foundHit = false;
    StaticWorldHit bestHit;
    bestHit.distance = maxDistance;

    for (const MapCompiledTriangle& triangle : m_Triangles)
    {
        StaticWorldHit candidate;
        if (!RaycastTriangle(ray, triangle, maxDistance, candidate))
            continue;

        if (!foundHit || candidate.distance < bestHit.distance)
        {
            bestHit = candidate;
            foundHit = true;
        }
    }

    if (foundHit)
        outHit = bestHit;
    return foundHit;
}

bool StaticWorldGeometry::OverlapSphere(const Vec3& center, float radius, StaticWorldHit& outHit) const
{
    bool foundHit = false;
    float bestDistanceSq = std::numeric_limits<float>::max();

    for (const MapCompiledTriangle& triangle : m_Triangles)
    {
        const Vec3 closestPoint = ClosestPointOnTriangle(center, triangle);
        const Vec3 delta = center - closestPoint;
        const float distanceSq = delta.LengthSquared();
        if (distanceSq > (radius * radius))
            continue;

        if (!foundHit || distanceSq < bestDistanceSq)
        {
            bestDistanceSq = distanceSq;
            outHit.hit = true;
            outHit.point = closestPoint;
            outHit.normal = triangle.normal;
            outHit.distance = std::sqrt(std::max(distanceSq, 0.0f));
            outHit.brushId = triangle.brushId;
            outHit.faceIndex = triangle.faceIndex;
            foundHit = true;
        }
    }

    return foundHit;
}

Vec3 StaticWorldGeometry::MoveSphereWithSliding(const Vec3& start, float radius, const Vec3& desiredMove, int maxIterations,
                                                StaticWorldHit* outHit) const
{
    if (!HasGeometry())
    {
        if (outHit)
            *outHit = {};
        return desiredMove;
    }

    if (desiredMove.LengthSquared() <= 1e-8f)
    {
        Vec3 resolvedCenter = start;
        StaticWorldHit hit;
        ResolveSpherePenetration(m_Triangles, resolvedCenter, radius, &hit);
        if (outHit)
            *outHit = hit;
        return resolvedCenter - start;
    }

    Vec3 appliedMove = Vec3::Zero();
    Vec3 remainingMove = desiredMove;
    StaticWorldHit lastHit;

    for (int iteration = 0; iteration < maxIterations && remainingMove.LengthSquared() > 1e-6f; ++iteration)
    {
        Vec3 currentCenter = start + appliedMove;
        if (ResolveSpherePenetration(m_Triangles, currentCenter, radius, &lastHit))
            appliedMove = currentCenter - start;

        StaticWorldHit endOverlap;
        if (!OverlapSphere(currentCenter + remainingMove, radius, endOverlap))
        {
            appliedMove += remainingMove;
            currentCenter = start + appliedMove;
            if (ResolveSpherePenetration(m_Triangles, currentCenter, radius, &lastHit))
                appliedMove = currentCenter - start;
            break;
        }

        float minT = 0.0f;
        float maxT = 1.0f;
        for (int binaryStep = 0; binaryStep < 10; ++binaryStep)
        {
            const float t = (minT + maxT) * 0.5f;
            StaticWorldHit overlapAtT;
            if (OverlapSphere(currentCenter + (remainingMove * t), radius, overlapAtT))
            {
                maxT = t;
                lastHit = overlapAtT;
            }
            else
            {
                minT = t;
            }
        }

        const float safeT = Clamp01(minT - 0.001f);
        appliedMove += remainingMove * safeT;

        const Vec3 unresolved = remainingMove * (1.0f - safeT);
        const float intoSurface = Vec3::Dot(unresolved, lastHit.normal);
        remainingMove = intoSurface < 0.0f ? (unresolved - (lastHit.normal * intoSurface)) : unresolved;

        if (remainingMove.LengthSquared() <= 1e-6f)
            break;
    }

    if (outHit)
        *outHit = lastHit;
    return appliedMove;
}

} // namespace Dot
