#pragma once

#include "Core/Math/Vec3.h"

#include <algorithm>
#include <cmath>

namespace Dot
{

// 3x3 Rotation Matrix for OBB orientation
struct Mat3
{
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    // Create rotation matrix from Euler angles (degrees)
    static Mat3 FromEulerDegrees(float x, float y, float z)
    {
        const float degToRad = 0.0174532925f;
        float rx = x * degToRad;
        float ry = y * degToRad;
        float rz = z * degToRad;

        float cx = std::cos(rx), sx = std::sin(rx);
        float cy = std::cos(ry), sy = std::sin(ry);
        float cz = std::cos(rz), sz = std::sin(rz);

        Mat3 result;
        // ZYX rotation order (common in games)
        result.m[0][0] = cy * cz;
        result.m[0][1] = cz * sx * sy - cx * sz;
        result.m[0][2] = cx * cz * sy + sx * sz;
        result.m[1][0] = cy * sz;
        result.m[1][1] = cx * cz + sx * sy * sz;
        result.m[1][2] = cx * sy * sz - cz * sx;
        result.m[2][0] = -sy;
        result.m[2][1] = cy * sx;
        result.m[2][2] = cx * cy;

        return result;
    }

    // Get column (axis) of the matrix
    Vec3 GetColumn(int i) const { return Vec3(m[0][i], m[1][i], m[2][i]); }

    // Get row of the matrix
    Vec3 GetRow(int i) const { return Vec3(m[i][0], m[i][1], m[i][2]); }

    // Transform a vector by this rotation
    Vec3 Transform(const Vec3& v) const
    {
        return Vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                    m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
    }

    // Transpose (for rotation matrices, transpose = inverse)
    Mat3 Transpose() const
    {
        Mat3 result;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.m[i][j] = m[j][i];
        return result;
    }
};

// Oriented Bounding Box
struct OBB
{
    Vec3 center;      // World position of center
    Vec3 halfExtents; // Half-size along each local axis
    Mat3 orientation; // Rotation matrix (columns are local axes in world space)

    // Get the 8 corners of the OBB in world space
    void GetCorners(Vec3 corners[8]) const
    {
        Vec3 ax = orientation.GetColumn(0) * halfExtents.x;
        Vec3 ay = orientation.GetColumn(1) * halfExtents.y;
        Vec3 az = orientation.GetColumn(2) * halfExtents.z;

        corners[0] = center - ax - ay - az;
        corners[1] = center + ax - ay - az;
        corners[2] = center + ax + ay - az;
        corners[3] = center - ax + ay - az;
        corners[4] = center - ax - ay + az;
        corners[5] = center + ax - ay + az;
        corners[6] = center + ax + ay + az;
        corners[7] = center - ax + ay + az;
    }
};

// Contact point information
struct ContactPoint
{
    Vec3 point;  // World space contact point
    Vec3 normal; // Contact normal (from B to A)
    float depth; // Penetration depth
};

// Collision manifold with contact points
struct ContactManifold
{
    ContactPoint contacts[4]; // Up to 4 contact points (face-face contact)
    int numContacts = 0;

    void AddContact(const Vec3& point, const Vec3& normal, float depth)
    {
        if (numContacts < 4)
        {
            contacts[numContacts].point = point;
            contacts[numContacts].normal = normal;
            contacts[numContacts].depth = depth;
            numContacts++;
        }
    }
};

// Project OBB onto axis and return min/max
inline void ProjectOBB(const OBB& obb, const Vec3& axis, float& outMin, float& outMax)
{
    // Project center
    float centerProj = Vec3::Dot(obb.center, axis);

    // Project half-extents along each local axis onto the test axis
    float r = std::abs(Vec3::Dot(obb.orientation.GetColumn(0), axis)) * obb.halfExtents.x +
              std::abs(Vec3::Dot(obb.orientation.GetColumn(1), axis)) * obb.halfExtents.y +
              std::abs(Vec3::Dot(obb.orientation.GetColumn(2), axis)) * obb.halfExtents.z;

    outMin = centerProj - r;
    outMax = centerProj + r;
}

// Check overlap on a single axis (SAT test)
inline bool OverlapOnAxis(const OBB& a, const OBB& b, const Vec3& axis, float& depth)
{
    float len = axis.Length();
    if (len < 0.0001f)
    {
        depth = 1e10f;
        return true; // Degenerate axis, ignore
    }

    Vec3 normAxis = axis / len;

    float minA, maxA, minB, maxB;
    ProjectOBB(a, normAxis, minA, maxA);
    ProjectOBB(b, normAxis, minB, maxB);

    // Check for gap
    if (maxA < minB || maxB < minA)
        return false; // Separating axis found

    // Calculate overlap
    depth = std::min(maxA - minB, maxB - minA);
    return true;
}

// OBB vs OBB collision using SAT (Separating Axis Theorem)
// Returns true if collision, fills manifold with contact info
bool OBBvsOBB(const OBB& a, const OBB& b, ContactManifold& manifold);

// Sphere collision
struct Sphere
{
    Vec3 center;
    float radius;
};

bool SphereVsSphere(const Sphere& a, const Sphere& b, ContactManifold& manifold);
bool OBBvsSphere(const OBB& obb, const Sphere& sphere, ContactManifold& manifold);

// ========== Raycasting ==========

// Ray for raycasting
struct Ray
{
    Vec3 origin;
    Vec3 direction; // Should be normalized

    // Get point along ray at distance t
    Vec3 GetPoint(float t) const { return origin + direction * t; }
    void GetPoint(float t, float& outX, float& outY, float& outZ) const
    {
        const Vec3 point = GetPoint(t);
        outX = point.x;
        outY = point.y;
        outZ = point.z;
    }
};

// Raycast hit result
struct RaycastHit
{
    bool hit = false;          // Did the ray hit?
    float distance = 0.0f;     // Distance to hit point
    Vec3 point = Vec3::Zero(); // World space hit point
    Vec3 normal = Vec3::Up();  // Surface normal at hit
};

// Raycast against OBB
// @param ray The ray to cast
// @param obb The oriented bounding box
// @param maxDistance Maximum ray distance
// @param outHit Result structure
// @return true if hit
bool RaycastVsOBB(const Ray& ray, const OBB& obb, float maxDistance, RaycastHit& outHit);

// Raycast against Sphere
bool RaycastVsSphere(const Ray& ray, const Sphere& sphere, float maxDistance, RaycastHit& outHit);

} // namespace Dot
