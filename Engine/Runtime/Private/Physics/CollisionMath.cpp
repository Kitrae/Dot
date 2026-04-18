#include "Core/Physics/CollisionMath.h"

#include <algorithm>
#include <cmath>

namespace Dot
{

// Find the contact points between two OBBs
// Improved: Use face center when the collision normal aligns with a face (stable resting)
static void FindContactPoints(const OBB& a, const OBB& b, const Vec3& normal, float depth, ContactManifold& manifold)
{
    // Check if the collision normal aligns with one of B's faces
    // If so, use the face center as contact point (much more stable for resting)
    Vec3 axesB[3] = {b.orientation.GetColumn(0), b.orientation.GetColumn(1), b.orientation.GetColumn(2)};

    const float faceAlignThreshold = 0.98f; // cos(~11 degrees)

    for (int i = 0; i < 3; i++)
    {
        float dotAbs = std::abs(Vec3::Dot(normal, axesB[i]));
        if (dotAbs > faceAlignThreshold)
        {
            // Normal aligns with this face of B
            // Contact point is the center of that face, projected onto A
            float sign = (Vec3::Dot(normal, axesB[i]) > 0) ? 1.0f : -1.0f;
            Vec3 faceCenter = b.center - axesB[i] * b.halfExtents[i] * sign;

            // Move contact point to the surface of A along the normal
            Vec3 contactPoint = faceCenter + normal * (depth * 0.5f);
            manifold.AddContact(contactPoint, normal, depth);
            return;
        }
    }

    // Also check if normal aligns with A's faces
    Vec3 axesA[3] = {a.orientation.GetColumn(0), a.orientation.GetColumn(1), a.orientation.GetColumn(2)};

    for (int i = 0; i < 3; i++)
    {
        float dotAbs = std::abs(Vec3::Dot(normal, axesA[i]));
        if (dotAbs > faceAlignThreshold)
        {
            // Normal aligns with this face of A
            float sign = (Vec3::Dot(normal, axesA[i]) > 0) ? -1.0f : 1.0f;
            Vec3 faceCenter = a.center - axesA[i] * a.halfExtents[i] * sign;

            Vec3 contactPoint = faceCenter - normal * (depth * 0.5f);
            manifold.AddContact(contactPoint, normal, depth);
            return;
        }
    }

    // Edge-edge contact: fall back to corner-based approach
    Vec3 cornersA[8], cornersB[8];
    a.GetCorners(cornersA);
    b.GetCorners(cornersB);

    // Find deepest penetrating corners and use their average
    // This gives a better contact point for edge-edge cases
    Vec3 contactSum = Vec3::Zero();
    int contactCount = 0;

    for (int i = 0; i < 8; i++)
    {
        float d = Vec3::Dot(cornersB[i] - a.center, normal);
        if (d < depth * 0.5f)
        {
            contactSum = contactSum + cornersB[i];
            contactCount++;
        }
    }

    if (contactCount > 0)
    {
        Vec3 avgContact = contactSum * (1.0f / static_cast<float>(contactCount));
        manifold.AddContact(avgContact, normal, depth);
    }
    else
    {
        // Fallback: use midpoint between centers
        Vec3 midpoint = (a.center + b.center) * 0.5f;
        manifold.AddContact(midpoint, normal, depth);
    }
}

bool OBBvsOBB(const OBB& a, const OBB& b, ContactManifold& manifold)
{
    manifold.numContacts = 0;

    // Get the axes to test (15 axes for OBB vs OBB)
    Vec3 axesA[3] = {a.orientation.GetColumn(0), a.orientation.GetColumn(1), a.orientation.GetColumn(2)};
    Vec3 axesB[3] = {b.orientation.GetColumn(0), b.orientation.GetColumn(1), b.orientation.GetColumn(2)};

    float minDepth = 1e10f;
    Vec3 minAxis = Vec3(0, 1, 0);

    // Test A's 3 face normals
    for (int i = 0; i < 3; i++)
    {
        float depth;
        if (!OverlapOnAxis(a, b, axesA[i], depth))
            return false;

        if (depth < minDepth)
        {
            minDepth = depth;
            minAxis = axesA[i];
        }
    }

    // Test B's 3 face normals
    for (int i = 0; i < 3; i++)
    {
        float depth;
        if (!OverlapOnAxis(a, b, axesB[i], depth))
            return false;

        if (depth < minDepth)
        {
            minDepth = depth;
            minAxis = axesB[i];
        }
    }

    // Test 9 edge-edge axes (cross products)
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            Vec3 axis = Vec3::Cross(axesA[i], axesB[j]);
            float len = axis.Length();
            if (len < 0.0001f)
                continue;

            float depth;
            if (!OverlapOnAxis(a, b, axis, depth))
                return false;

            depth /= len;

            if (depth < minDepth)
            {
                minDepth = depth;
                minAxis = axis / len;
            }
        }
    }

    // Ensure normal points from B to A
    Vec3 centerDiff = a.center - b.center;
    if (Vec3::Dot(minAxis, centerDiff) < 0)
    {
        minAxis = -minAxis;
    }

    // Find contact points
    FindContactPoints(a, b, minAxis, minDepth, manifold);

    return true;
}

bool SphereVsSphere(const Sphere& a, const Sphere& b, ContactManifold& manifold)
{
    manifold.numContacts = 0;

    Vec3 diff = b.center - a.center;
    float distSq = diff.LengthSquared();
    float radiusSum = a.radius + b.radius;

    if (distSq >= radiusSum * radiusSum)
        return false;

    float dist = std::sqrt(distSq);
    Vec3 normal;

    if (dist > 0.0001f)
    {
        normal = diff / dist;
    }
    else
    {
        normal = Vec3(0, 1, 0);
    }

    float depth = radiusSum - dist;

    Vec3 contactPoint = a.center + normal * a.radius;
    manifold.AddContact(contactPoint, normal, depth);

    return true;
}

bool OBBvsSphere(const OBB& obb, const Sphere& sphere, ContactManifold& manifold)
{
    manifold.numContacts = 0;

    // Transform sphere center to OBB's local space
    Vec3 localSphereCenter = sphere.center - obb.center;
    Mat3 invRot = obb.orientation.Transpose();
    Vec3 localCenter = invRot.Transform(localSphereCenter);

    // Find closest point on OBB to sphere center (in local space)
    Vec3 closestLocal = Vec3(std::clamp(localCenter.x, -obb.halfExtents.x, obb.halfExtents.x),
                             std::clamp(localCenter.y, -obb.halfExtents.y, obb.halfExtents.y),
                             std::clamp(localCenter.z, -obb.halfExtents.z, obb.halfExtents.z));

    // Distance from closest point to sphere center
    Vec3 diff = localCenter - closestLocal;
    float distSq = diff.LengthSquared();

    if (distSq > sphere.radius * sphere.radius)
        return false;

    // Transform closest point back to world space
    Vec3 closestWorld = obb.center + obb.orientation.Transform(closestLocal);

    Vec3 normal;
    float depth;

    if (distSq > 0.0001f)
    {
        float dist = std::sqrt(distSq);
        // Normal points from OBB surface TO sphere center (away from OBB)
        normal = (sphere.center - closestWorld).Normalized();
        depth = sphere.radius - dist;
    }
    else
    {
        // Sphere center inside OBB - push out along nearest face
        Vec3 absDiff = Vec3(obb.halfExtents.x - std::abs(localCenter.x), obb.halfExtents.y - std::abs(localCenter.y),
                            obb.halfExtents.z - std::abs(localCenter.z));

        int minAxis = 0;
        if (absDiff.y < absDiff.x)
            minAxis = 1;
        if (absDiff.z < (minAxis == 0 ? absDiff.x : absDiff.y))
            minAxis = 2;

        Vec3 localNormal = Vec3::Zero();
        if (minAxis == 0)
            localNormal.x = (localCenter.x > 0) ? 1.0f : -1.0f;
        else if (minAxis == 1)
            localNormal.y = (localCenter.y > 0) ? 1.0f : -1.0f;
        else
            localNormal.z = (localCenter.z > 0) ? 1.0f : -1.0f;

        normal = obb.orientation.Transform(localNormal);
        depth = sphere.radius + (minAxis == 0 ? absDiff.x : (minAxis == 1 ? absDiff.y : absDiff.z));
    }

    // Contact point is on sphere surface, toward the OBB
    Vec3 contactPoint = sphere.center - normal * sphere.radius;
    manifold.AddContact(contactPoint, normal, depth);
    return true;
}

// ========== Raycasting Implementations ==========

bool RaycastVsOBB(const Ray& ray, const OBB& obb, float maxDistance, RaycastHit& outHit)
{
    outHit.hit = false;

    // Transform ray to OBB local space
    Vec3 localOrigin = ray.origin - obb.center;
    Mat3 invRot = obb.orientation.Transpose();
    Vec3 p = invRot.Transform(localOrigin);
    Vec3 d = invRot.Transform(ray.direction);

    float tMin = 0.0f;
    float tMax = maxDistance;
    Vec3 normalLocal = Vec3::Zero();

    // Test against each axis-aligned slab in local space
    for (int i = 0; i < 3; i++)
    {
        float pi = (i == 0) ? p.x : ((i == 1) ? p.y : p.z);
        float di = (i == 0) ? d.x : ((i == 1) ? d.y : d.z);
        float ei = (i == 0) ? obb.halfExtents.x : ((i == 1) ? obb.halfExtents.y : obb.halfExtents.z);

        if (std::abs(di) < 0.0001f)
        {
            // Ray parallel to slab
            if (pi < -ei || pi > ei)
                return false;
        }
        else
        {
            float invD = 1.0f / di;
            float t1 = (-ei - pi) * invD;
            float t2 = (ei - pi) * invD;

            int faceSign = 1;
            if (t1 > t2)
            {
                std::swap(t1, t2);
                faceSign = -1;
            }

            if (t1 > tMin)
            {
                tMin = t1;
                normalLocal = Vec3::Zero();
                if (i == 0)
                    normalLocal.x = static_cast<float>(-faceSign);
                else if (i == 1)
                    normalLocal.y = static_cast<float>(-faceSign);
                else
                    normalLocal.z = static_cast<float>(-faceSign);
            }

            tMax = std::min(tMax, t2);

            if (tMin > tMax)
                return false;
        }
    }

    if (tMin < 0.0f || tMin > maxDistance)
        return false;

    outHit.hit = true;
    outHit.distance = tMin;
    outHit.point = ray.GetPoint(tMin);
    outHit.normal = obb.orientation.Transform(normalLocal);

    return true;
}

bool RaycastVsSphere(const Ray& ray, const Sphere& sphere, float maxDistance, RaycastHit& outHit)
{
    outHit.hit = false;

    Vec3 oc = ray.origin - sphere.center;
    float a = Vec3::Dot(ray.direction, ray.direction);
    float b = 2.0f * Vec3::Dot(oc, ray.direction);
    float c = Vec3::Dot(oc, oc) - sphere.radius * sphere.radius;

    float discriminant = b * b - 4 * a * c;

    if (discriminant < 0.0f)
        return false;

    float sqrtD = std::sqrt(discriminant);
    float t = (-b - sqrtD) / (2.0f * a);

    // If t is negative, try the other root
    if (t < 0.0f)
        t = (-b + sqrtD) / (2.0f * a);

    if (t < 0.0f || t > maxDistance)
        return false;

    outHit.hit = true;
    outHit.distance = t;
    outHit.point = ray.GetPoint(t);
    outHit.normal = (outHit.point - sphere.center).Normalized();

    return true;
}

} // namespace Dot
