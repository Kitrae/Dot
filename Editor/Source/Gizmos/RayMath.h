// =============================================================================
// Dot Engine - Ray Math Utilities
// =============================================================================
// Ray picking and intersection tests for gizmo interaction.
// =============================================================================

#pragma once

#include "Core/Physics/CollisionMath.h"

#include <algorithm>
#include <cmath>

namespace Dot
{

/// Axis-aligned bounding box
struct AABB
{
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    /// Create AABB centered at point with given half-extents
    static AABB FromCenterSize(float cx, float cy, float cz, float hx, float hy, float hz)
    {
        AABB box;
        box.minX = cx - hx;
        box.minY = cy - hy;
        box.minZ = cz - hz;
        box.maxX = cx + hx;
        box.maxY = cy + hy;
        box.maxZ = cz + hz;
        return box;
    }
};

/// Plane in 3D (ax + by + cz + d = 0)
struct Plane
{
    float a, b, c, d;

    /// Create plane from point and normal
    static Plane FromPointNormal(float px, float py, float pz, float nx, float ny, float nz)
    {
        Plane p;
        p.a = nx;
        p.b = ny;
        p.c = nz;
        p.d = -(nx * px + ny * py + nz * pz);
        return p;
    }
};

// =============================================================================
// Intersection Tests
// =============================================================================

/// Ray-AABB intersection, returns true if hit and sets tMin/tMax
inline bool RayAABBIntersect(const Ray& ray, const AABB& box, float& tMin, float& tMax)
{
    float t1 = (box.minX - ray.origin.x) / (ray.direction.x != 0 ? ray.direction.x : 0.0001f);
    float t2 = (box.maxX - ray.origin.x) / (ray.direction.x != 0 ? ray.direction.x : 0.0001f);
    float t3 = (box.minY - ray.origin.y) / (ray.direction.y != 0 ? ray.direction.y : 0.0001f);
    float t4 = (box.maxY - ray.origin.y) / (ray.direction.y != 0 ? ray.direction.y : 0.0001f);
    float t5 = (box.minZ - ray.origin.z) / (ray.direction.z != 0 ? ray.direction.z : 0.0001f);
    float t6 = (box.maxZ - ray.origin.z) / (ray.direction.z != 0 ? ray.direction.z : 0.0001f);

    tMin = std::max({std::min(t1, t2), std::min(t3, t4), std::min(t5, t6)});
    tMax = std::min({std::max(t1, t2), std::max(t3, t4), std::max(t5, t6)});

    return tMax >= 0 && tMin <= tMax;
}

/// Ray-Plane intersection, returns distance t (negative if no hit)
inline float RayPlaneIntersect(const Ray& ray, const Plane& plane)
{
    float denom = plane.a * ray.direction.x + plane.b * ray.direction.y + plane.c * ray.direction.z;
    if (std::abs(denom) < 0.0001f)
        return -1.0f; // Parallel

    float numer = -(plane.a * ray.origin.x + plane.b * ray.origin.y + plane.c * ray.origin.z + plane.d);
    return numer / denom;
}

/// Distance from point to ray (closest approach)
inline float PointRayDistance(const Ray& ray, float px, float py, float pz, float& closestT)
{
    // Vector from ray origin to point
    float dx = px - ray.origin.x;
    float dy = py - ray.origin.y;
    float dz = pz - ray.origin.z;

    // Project onto ray direction
    closestT = dx * ray.direction.x + dy * ray.direction.y + dz * ray.direction.z;
    if (closestT < 0)
        closestT = 0; // Clamp to ray origin

    // Get closest point on ray
    float cpx, cpy, cpz;
    const Vec3 closestPoint = ray.GetPoint(closestT);
    cpx = closestPoint.x;
    cpy = closestPoint.y;
    cpz = closestPoint.z;

    // Distance
    float ddx = px - cpx;
    float ddy = py - cpy;
    float ddz = pz - cpz;
    return std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
}

/// Distance from ray to line segment (for picking arrow shafts)
inline float RayLineSegmentDistance(const Ray& ray, float ax, float ay, float az, float bx, float by, float bz,
                                    float& rayT, float& segT)
{
    // Direction of line segment
    float dx = bx - ax;
    float dy = by - ay;
    float dz = bz - az;

    // Vector between origins
    float wx = ray.origin.x - ax;
    float wy = ray.origin.y - ay;
    float wz = ray.origin.z - az;

    float a = ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y + ray.direction.z * ray.direction.z;
    float b = ray.direction.x * dx + ray.direction.y * dy + ray.direction.z * dz;
    float c = dx * dx + dy * dy + dz * dz;
    float d = ray.direction.x * wx + ray.direction.y * wy + ray.direction.z * wz;
    float e = dx * wx + dy * wy + dz * wz;

    float denom = a * c - b * b;
    if (std::abs(denom) < 0.0001f)
    {
        rayT = 0;
        segT = e / c;
    }
    else
    {
        rayT = (b * e - c * d) / denom;
        segT = (a * e - b * d) / denom;
    }

    // Clamp to valid ranges
    if (rayT < 0)
        rayT = 0;
    if (segT < 0)
        segT = 0;
    if (segT > 1)
        segT = 1;

    // Get closest points
    float rpx, rpy, rpz;
    const Vec3 rayPoint = ray.GetPoint(rayT);
    rpx = rayPoint.x;
    rpy = rayPoint.y;
    rpz = rayPoint.z;

    float spx = ax + segT * dx;
    float spy = ay + segT * dy;
    float spz = az + segT * dz;

    // Distance between closest points
    float ddx = rpx - spx;
    float ddy = rpy - spy;
    float ddz = rpz - spz;
    return std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
}

} // namespace Dot
