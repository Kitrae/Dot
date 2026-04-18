// =============================================================================
// Dot Engine - Primitive Mesh Data
// =============================================================================
// Procedurally generated mesh data for primitive shapes.
// =============================================================================

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Dot
{

struct PrimitiveVertex
{
    float x, y, z;    // Position
    float nx, ny, nz; // Normal
    float r, g, b, a; // Color
    float u, v;       // UV coordinates (texture coords)
    float u2, v2;     // Lightmap UV coordinates
    float tx = 1.0f, ty = 0.0f, tz = 0.0f, tw = 1.0f; // Tangent.xyz + handedness sign in w

    PrimitiveVertex() = default;

    PrimitiveVertex(float inX, float inY, float inZ, float inNx, float inNy, float inNz, float inR, float inG,
                    float inB, float inA, float inU, float inV)
        : x(inX), y(inY), z(inZ), nx(inNx), ny(inNy), nz(inNz), r(inR), g(inG), b(inB), a(inA), u(inU), v(inV),
          u2(inU), v2(inV), tx(1.0f), ty(0.0f), tz(0.0f), tw(1.0f)
    {
    }

    PrimitiveVertex(float inX, float inY, float inZ, float inNx, float inNy, float inNz, float inR, float inG,
                    float inB, float inA, float inU, float inV, float inU2, float inV2)
        : x(inX), y(inY), z(inZ), nx(inNx), ny(inNy), nz(inNz), r(inR), g(inG), b(inB), a(inA), u(inU), v(inV),
          u2(inU2), v2(inV2), tx(1.0f), ty(0.0f), tz(0.0f), tw(1.0f)
    {
    }
};

/// Represents a subset of a mesh with its own material
struct Submesh
{
    uint32_t indexStart = 0;  ///< Starting index in the index buffer
    uint32_t indexCount = 0;  ///< Number of indices for this submesh
    std::string materialPath; ///< Relative path to .dotmat material file
    std::string texturePath;  ///< Cached texture path extracted from .dotmat (added for performance)
};

struct MeshData
{
    std::vector<PrimitiveVertex> vertices;
    std::vector<uint32_t> indices; // 32-bit indices for large meshes (>65535 vertices)

    /// Per-material submeshes. If empty, treat entire mesh as one submesh.
    std::vector<Submesh> submeshes;

    // Bounding box
    float boundsMinX = 0, boundsMinY = 0, boundsMinZ = 0;
    float boundsMaxX = 0, boundsMaxY = 0, boundsMaxZ = 0;
};

inline void ComputeMeshTangents(MeshData& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty())
        return;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    auto dot = [](const Vec3& a, const Vec3& b) -> float { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto cross = [](const Vec3& a, const Vec3& b) -> Vec3 {
        return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    auto normalize = [](const Vec3& v) -> Vec3 {
        const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (lenSq <= 1e-20f)
            return Vec3{1.0f, 0.0f, 0.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return Vec3{v.x * invLen, v.y * invLen, v.z * invLen};
    };
    auto fallbackTangent = [&](const Vec3& normal) -> Vec3 {
        const Vec3 reference = std::abs(normal.z) < 0.999f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
        return normalize(cross(reference, normal));
    };

    std::vector<Vec3> tan1(mesh.vertices.size());
    std::vector<Vec3> tan2(mesh.vertices.size());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const uint32_t i0 = mesh.indices[i + 0];
        const uint32_t i1 = mesh.indices[i + 1];
        const uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
            continue;

        const PrimitiveVertex& v0 = mesh.vertices[i0];
        const PrimitiveVertex& v1 = mesh.vertices[i1];
        const PrimitiveVertex& v2 = mesh.vertices[i2];

        const Vec3 p0{v0.x, v0.y, v0.z};
        const Vec3 p1{v1.x, v1.y, v1.z};
        const Vec3 p2{v2.x, v2.y, v2.z};

        const float x1 = p1.x - p0.x;
        const float y1 = p1.y - p0.y;
        const float z1 = p1.z - p0.z;
        const float x2 = p2.x - p0.x;
        const float y2 = p2.y - p0.y;
        const float z2 = p2.z - p0.z;

        const float s1 = v1.u - v0.u;
        const float t1 = v1.v - v0.v;
        const float s2 = v2.u - v0.u;
        const float t2 = v2.v - v0.v;

        const float det = s1 * t2 - s2 * t1;
        if (std::abs(det) <= 1e-8f)
            continue;

        const float invDet = 1.0f / det;
        const Vec3 sdir{(t2 * x1 - t1 * x2) * invDet, (t2 * y1 - t1 * y2) * invDet,
                        (t2 * z1 - t1 * z2) * invDet};
        const Vec3 tdir{(s1 * x2 - s2 * x1) * invDet, (s1 * y2 - s2 * y1) * invDet,
                        (s1 * z2 - s2 * z1) * invDet};

        tan1[i0].x += sdir.x;
        tan1[i0].y += sdir.y;
        tan1[i0].z += sdir.z;
        tan1[i1].x += sdir.x;
        tan1[i1].y += sdir.y;
        tan1[i1].z += sdir.z;
        tan1[i2].x += sdir.x;
        tan1[i2].y += sdir.y;
        tan1[i2].z += sdir.z;

        tan2[i0].x += tdir.x;
        tan2[i0].y += tdir.y;
        tan2[i0].z += tdir.z;
        tan2[i1].x += tdir.x;
        tan2[i1].y += tdir.y;
        tan2[i1].z += tdir.z;
        tan2[i2].x += tdir.x;
        tan2[i2].y += tdir.y;
        tan2[i2].z += tdir.z;
    }

    for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex)
    {
        PrimitiveVertex& vertex = mesh.vertices[vertexIndex];
        const Vec3 normal = normalize(Vec3{vertex.nx, vertex.ny, vertex.nz});
        Vec3 tangent = tan1[vertexIndex];
        const float tangentLenSq = dot(tangent, tangent);

        if (tangentLenSq <= 1e-8f)
        {
            tangent = fallbackTangent(normal);
            vertex.tx = tangent.x;
            vertex.ty = tangent.y;
            vertex.tz = tangent.z;
            vertex.tw = 1.0f;
            continue;
        }

        tangent = Vec3{tangent.x - normal.x * dot(normal, tangent), tangent.y - normal.y * dot(normal, tangent),
                       tangent.z - normal.z * dot(normal, tangent)};
        tangent = normalize(tangent);

        const Vec3 bitangent = tan2[vertexIndex];
        const float handedness = dot(cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
        vertex.tx = tangent.x;
        vertex.ty = tangent.y;
        vertex.tz = tangent.z;
        vertex.tw = handedness;
    }
}

// =============================================================================
// Cube
// =============================================================================

inline MeshData GenerateCube()
{
    MeshData mesh;
    constexpr float kCellW = 1.0f / 3.0f;
    constexpr float kCellH = 1.0f / 2.0f;
    const auto makeUv2 = [](int cellX, int cellY, float u, float v)
    {
        return std::pair<float, float>((static_cast<float>(cellX) + u) * kCellW,
                                       (static_cast<float>(cellY) + v) * kCellH);
    };

    const auto front00 = makeUv2(0, 0, 0.0f, 0.0f);
    const auto front10 = makeUv2(0, 0, 1.0f, 0.0f);
    const auto front11 = makeUv2(0, 0, 1.0f, 1.0f);
    const auto front01 = makeUv2(0, 0, 0.0f, 1.0f);

    const auto back00 = makeUv2(1, 0, 0.0f, 0.0f);
    const auto back10 = makeUv2(1, 0, 1.0f, 0.0f);
    const auto back11 = makeUv2(1, 0, 1.0f, 1.0f);
    const auto back01 = makeUv2(1, 0, 0.0f, 1.0f);

    const auto top00 = makeUv2(2, 0, 0.0f, 0.0f);
    const auto top10 = makeUv2(2, 0, 1.0f, 0.0f);
    const auto top11 = makeUv2(2, 0, 1.0f, 1.0f);
    const auto top01 = makeUv2(2, 0, 0.0f, 1.0f);

    const auto bottom00 = makeUv2(0, 1, 0.0f, 0.0f);
    const auto bottom10 = makeUv2(0, 1, 1.0f, 0.0f);
    const auto bottom11 = makeUv2(0, 1, 1.0f, 1.0f);
    const auto bottom01 = makeUv2(0, 1, 0.0f, 1.0f);

    const auto right00 = makeUv2(1, 1, 0.0f, 0.0f);
    const auto right10 = makeUv2(1, 1, 1.0f, 0.0f);
    const auto right11 = makeUv2(1, 1, 1.0f, 1.0f);
    const auto right01 = makeUv2(1, 1, 0.0f, 1.0f);

    const auto left00 = makeUv2(2, 1, 0.0f, 0.0f);
    const auto left10 = makeUv2(2, 1, 1.0f, 0.0f);
    const auto left11 = makeUv2(2, 1, 1.0f, 1.0f);
    const auto left01 = makeUv2(2, 1, 0.0f, 1.0f);

    mesh.vertices = {
        // Front face - normal: (0, 0, 1) - UV: (0,0) bottom-left to (1,1) top-right
        {-0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 1, 1, 1, 0, 0, front00.first, front00.second},
        {0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 1, 1, 1, 1, 0, front10.first, front10.second},
        {0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1, 1, 1, 1, 1, front11.first, front11.second},
        {-0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1, 1, 1, 0, 1, front01.first, front01.second},
        // Back face - normal: (0, 0, -1)
        {0.5f, -0.5f, -0.5f, 0, 0, -1, 1, 1, 1, 1, 0, 0, back00.first, back00.second},
        {-0.5f, -0.5f, -0.5f, 0, 0, -1, 1, 1, 1, 1, 1, 0, back10.first, back10.second},
        {-0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1, 1, 1, 1, 1, back11.first, back11.second},
        {0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1, 1, 1, 0, 1, back01.first, back01.second},
        // Top face - normal: (0, 1, 0)
        {-0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 1, 1, 1, 0, 0, top00.first, top00.second},
        {0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 1, 1, 1, 1, 0, top10.first, top10.second},
        {0.5f, 0.5f, -0.5f, 0, 1, 0, 1, 1, 1, 1, 1, 1, top11.first, top11.second},
        {-0.5f, 0.5f, -0.5f, 0, 1, 0, 1, 1, 1, 1, 0, 1, top01.first, top01.second},
        // Bottom face - normal: (0, -1, 0)
        {-0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 1, 1, 1, 0, 0, bottom00.first, bottom00.second},
        {0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 1, 1, 1, 1, 0, bottom10.first, bottom10.second},
        {0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 1, 1, 1, 1, 1, bottom11.first, bottom11.second},
        {-0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 1, 1, 1, 0, 1, bottom01.first, bottom01.second},
        // Right face - normal: (1, 0, 0)
        {0.5f, -0.5f, 0.5f, 1, 0, 0, 1, 1, 1, 1, 0, 0, right00.first, right00.second},
        {0.5f, -0.5f, -0.5f, 1, 0, 0, 1, 1, 1, 1, 1, 0, right10.first, right10.second},
        {0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 1, 1, 1, 1, 1, right11.first, right11.second},
        {0.5f, 0.5f, 0.5f, 1, 0, 0, 1, 1, 1, 1, 0, 1, right01.first, right01.second},
        // Left face - normal: (-1, 0, 0)
        {-0.5f, -0.5f, -0.5f, -1, 0, 0, 1, 1, 1, 1, 0, 0, left00.first, left00.second},
        {-0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 1, 1, 1, 1, 0, left10.first, left10.second},
        {-0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 1, 1, 1, 1, 1, left11.first, left11.second},
        {-0.5f, 0.5f, -0.5f, -1, 0, 0, 1, 1, 1, 1, 0, 1, left01.first, left01.second},
    };
    mesh.indices = {
        0,  1,  2,  0,  2,  3,  // Front
        4,  5,  6,  4,  6,  7,  // Back
        8,  9,  10, 8,  10, 11, // Top
        12, 13, 14, 12, 14, 15, // Bottom
        16, 17, 18, 16, 18, 19, // Right
        20, 21, 22, 20, 22, 23, // Left
    };
    return mesh;
}

// =============================================================================
// Sphere (UV sphere)
// =============================================================================

inline MeshData GenerateSphere(int latSegments = 16, int lonSegments = 32)
{
    MeshData mesh;
    const float PI = 3.14159265f;
    const float radius = 0.5f;

    // Generate vertices with UVs
    for (int lat = 0; lat <= latSegments; ++lat)
    {
        float theta = lat * PI / latSegments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);
        float v = static_cast<float>(lat) / latSegments; // V: 0 at top, 1 at bottom

        for (int lon = 0; lon <= lonSegments; ++lon)
        {
            float phi = lon * 2.0f * PI / lonSegments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);
            float u = static_cast<float>(lon) / lonSegments; // U: 0 to 1 around sphere

            float x = cosPhi * sinTheta;
            float y = cosTheta;
            float z = sinPhi * sinTheta;

            // White color (texture will provide color)
            mesh.vertices.push_back({x * radius, y * radius, z * radius, x, y, z, 1, 1, 1, 1, u, v});
        }
    }

    // Generate indices
    for (int lat = 0; lat < latSegments; ++lat)
    {
        for (int lon = 0; lon < lonSegments; ++lon)
        {
            uint16_t first = static_cast<uint16_t>(lat * (lonSegments + 1) + lon);
            uint16_t second = static_cast<uint16_t>(first + lonSegments + 1);

            mesh.indices.push_back(first);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second);

            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second + 1);
        }
    }

    return mesh;
}

// =============================================================================
// Cylinder
// =============================================================================

inline MeshData GenerateCylinder(int segments = 32)
{
    MeshData mesh;
    const float PI = 3.14159265f;
    const float radius = 0.5f;
    const float halfHeight = 0.5f;

    uint16_t baseIndex = 0;

    // Side vertices with cylindrical UV mapping
    for (int i = 0; i <= segments; ++i)
    {
        float angle = i * 2.0f * PI / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        // Normalized direction for side normal
        float nx = std::cos(angle);
        float nz = std::sin(angle);

        // UV: u wraps around (0 to 1), v is height (0 at bottom, 1 at top)
        float u = static_cast<float>(i) / segments;

        // Bottom vertex (orange) - side normal, v=0
        mesh.vertices.push_back({x, -halfHeight, z, nx, 0, nz, 1.0f, 0.5f, 0.0f, 1.0f, u, 0.0f});
        // Top vertex (orange) - side normal, v=1
        mesh.vertices.push_back({x, halfHeight, z, nx, 0, nz, 1.0f, 0.6f, 0.1f, 1.0f, u, 1.0f});
    }

    // Side indices
    for (int i = 0; i < segments; ++i)
    {
        uint16_t bl = static_cast<uint16_t>(baseIndex + i * 2);
        uint16_t br = static_cast<uint16_t>(baseIndex + (i + 1) * 2);
        uint16_t tl = static_cast<uint16_t>(bl + 1);
        uint16_t tr = static_cast<uint16_t>(br + 1);

        mesh.indices.push_back(bl);
        mesh.indices.push_back(tl);
        mesh.indices.push_back(br);

        mesh.indices.push_back(br);
        mesh.indices.push_back(tl);
        mesh.indices.push_back(tr);
    }

    // Top cap center - normal points up, UV at center (0.5, 0.5)
    baseIndex = static_cast<uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({0, halfHeight, 0, 0, 1, 0, 0.2f, 0.6f, 1.0f, 1.0f, 0.5f, 0.5f}); // Blue
    uint16_t topCenter = baseIndex;

    // Top cap ring - normal points up, UV in circular pattern
    for (int i = 0; i <= segments; ++i)
    {
        float angle = i * 2.0f * PI / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        // Planar UV mapping for cap (0-1 range centered at 0.5)
        float u = std::cos(angle) * 0.5f + 0.5f;
        float v = std::sin(angle) * 0.5f + 0.5f;
        mesh.vertices.push_back({x, halfHeight, z, 0, 1, 0, 0.3f, 0.7f, 1.0f, 1.0f, u, v});
    }

    // Top cap indices
    for (int i = 0; i < segments; ++i)
    {
        mesh.indices.push_back(topCenter);
        mesh.indices.push_back(static_cast<uint16_t>(topCenter + 1 + i + 1));
        mesh.indices.push_back(static_cast<uint16_t>(topCenter + 1 + i));
    }

    // Bottom cap center - normal points down, UV at center (0.5, 0.5)
    baseIndex = static_cast<uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({0, -halfHeight, 0, 0, -1, 0, 0.8f, 0.2f, 0.2f, 1.0f, 0.5f, 0.5f}); // Red
    uint16_t botCenter = baseIndex;

    // Bottom cap ring - normal points down, UV in circular pattern
    for (int i = 0; i <= segments; ++i)
    {
        float angle = i * 2.0f * PI / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        // Planar UV mapping for cap
        float u = std::cos(angle) * 0.5f + 0.5f;
        float v = std::sin(angle) * 0.5f + 0.5f;
        mesh.vertices.push_back({x, -halfHeight, z, 0, -1, 0, 0.9f, 0.3f, 0.3f, 1.0f, u, v});
    }

    // Bottom cap indices (reversed winding)
    for (int i = 0; i < segments; ++i)
    {
        mesh.indices.push_back(botCenter);
        mesh.indices.push_back(static_cast<uint16_t>(botCenter + 1 + i));
        mesh.indices.push_back(static_cast<uint16_t>(botCenter + 1 + i + 1));
    }

    return mesh;
}

// =============================================================================
// Plane
// =============================================================================

inline MeshData GeneratePlane()
{
    MeshData mesh;
    // Simple quad facing up (Y+) - all normals point up
    // Format: {x, y, z, nx, ny, nz, r, g, b, a, u, v}
    mesh.vertices = {
        {-0.5f, 0.0f, -0.5f, 0, 1, 0, 0.4f, 0.7f, 0.4f, 1.0f, 0.0f, 0.0f}, // bottom-left UV
        {0.5f, 0.0f, -0.5f, 0, 1, 0, 0.5f, 0.8f, 0.5f, 1.0f, 1.0f, 0.0f},  // bottom-right UV
        {0.5f, 0.0f, 0.5f, 0, 1, 0, 0.4f, 0.7f, 0.4f, 1.0f, 1.0f, 1.0f},   // top-right UV
        {-0.5f, 0.0f, 0.5f, 0, 1, 0, 0.5f, 0.8f, 0.5f, 1.0f, 0.0f, 1.0f},  // top-left UV
    };
    mesh.indices = {0, 2, 1, 0, 3, 2};
    return mesh;
}

// =============================================================================
// Cone
// =============================================================================

inline MeshData GenerateCone(int segments = 32)
{
    MeshData mesh;
    const float PI = 3.14159265f;
    const float radius = 0.5f;
    const float height = 1.0f;

    // Calculate the slope of the cone for proper normals
    // Normal at base: pointing outward and up
    float slopeAngle = std::atan2(radius, height);
    float cosSlope = std::cos(slopeAngle);
    float sinSlope = std::sin(slopeAngle);

    // Apex vertex (purple) - use average of surrounding normals (pointing up)
    // UV at top center (0.5, 1.0)
    uint16_t apex = 0;
    mesh.vertices.push_back({0, height * 0.5f, 0, 0, sinSlope, 0, 0.8f, 0.2f, 0.8f, 1.0f, 0.5f, 1.0f});

    // Base ring with side normals
    for (int i = 0; i <= segments; ++i)
    {
        float angle = i * 2.0f * PI / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        // Normal points outward and up
        float nx = std::cos(angle) * cosSlope;
        float ny = sinSlope;
        float nz = std::sin(angle) * cosSlope;
        // Color gradient around base
        float r = (std::cos(angle) + 1.0f) * 0.4f + 0.2f;
        float g = 0.3f;
        float b = (std::sin(angle) + 1.0f) * 0.4f + 0.2f;
        // UV: u wraps around (0-1), v=0 at base
        float u = static_cast<float>(i) / segments;
        mesh.vertices.push_back({x, -height * 0.5f, z, nx, ny, nz, r, g, b, 1.0f, u, 0.0f});
    }

    // Side indices
    for (int i = 0; i < segments; ++i)
    {
        mesh.indices.push_back(apex);
        mesh.indices.push_back(static_cast<uint16_t>(1 + i + 1));
        mesh.indices.push_back(static_cast<uint16_t>(1 + i));
    }

    // Base center - normal points down, UV at center (0.5, 0.5)
    uint16_t baseCenter = static_cast<uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({0, -height * 0.5f, 0, 0, -1, 0, 0.3f, 0.3f, 0.3f, 1.0f, 0.5f, 0.5f});

    // Base ring (for base cap) - normal points down, planar UV
    uint16_t baseStart = static_cast<uint16_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i)
    {
        float angle = i * 2.0f * PI / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        // Planar UV mapping for cap
        float u = std::cos(angle) * 0.5f + 0.5f;
        float v = std::sin(angle) * 0.5f + 0.5f;
        mesh.vertices.push_back({x, -height * 0.5f, z, 0, -1, 0, 0.4f, 0.4f, 0.4f, 1.0f, u, v});
    }

    // Base indices
    for (int i = 0; i < segments; ++i)
    {
        mesh.indices.push_back(baseCenter);
        mesh.indices.push_back(static_cast<uint16_t>(baseStart + i + 1));
        mesh.indices.push_back(static_cast<uint16_t>(baseStart + i));
    }

    return mesh;
}

// =============================================================================
// Capsule (sphere + cylinder + sphere)
// =============================================================================

inline MeshData GenerateCapsule(int segments = 16, int rings = 8)
{
    MeshData mesh;
    const float PI = 3.14159265f;
    const float radius = 0.25f;
    const float halfHeight = 0.25f; // Cylinder part

    // Top hemisphere - normals point outward from hemisphere center
    // UV: v ranges from 1.0 (top pole) to 0.75 (equator of top hemisphere)
    for (int lat = 0; lat <= rings; ++lat)
    {
        float theta = lat * (PI / 2.0f) / rings; // 0 to PI/2
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);
        // v: 1.0 at top pole, 0.75 at equator
        float v = 1.0f - (static_cast<float>(lat) / rings) * 0.25f;

        for (int lon = 0; lon <= segments; ++lon)
        {
            float phi = lon * 2.0f * PI / segments;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            float x = cosPhi * sinTheta * radius;
            float y = cosTheta * radius + halfHeight;
            float z = sinPhi * sinTheta * radius;

            // Normal is direction from hemisphere center (same as sphere normal)
            float nx = cosPhi * sinTheta;
            float ny = cosTheta;
            float nz = sinPhi * sinTheta;

            float u = static_cast<float>(lon) / segments;
            mesh.vertices.push_back({x, y, z, nx, ny, nz, 0.2f, 0.6f, 0.9f, 1.0f, u, v}); // Blue
        }
    }

    // Top hemisphere indices
    for (int lat = 0; lat < rings; ++lat)
    {
        for (int lon = 0; lon < segments; ++lon)
        {
            uint16_t first = static_cast<uint16_t>(lat * (segments + 1) + lon);
            uint16_t second = static_cast<uint16_t>(first + segments + 1);

            mesh.indices.push_back(first);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second);

            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second + 1);
        }
    }

    // Cylinder part - radial normals, UV: v from 0.75 (top) to 0.25 (bottom)
    uint16_t cylBase = static_cast<uint16_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i)
    {
        float angle = i * 2.0f * PI / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        float nx = std::cos(angle);
        float nz = std::sin(angle);
        float u = static_cast<float>(i) / segments;

        mesh.vertices.push_back({x, halfHeight, z, nx, 0, nz, 0.3f, 0.7f, 0.5f, 1.0f, u, 0.75f});  // Green top
        mesh.vertices.push_back({x, -halfHeight, z, nx, 0, nz, 0.3f, 0.7f, 0.5f, 1.0f, u, 0.25f}); // Green bottom
    }

    // Cylinder indices
    for (int i = 0; i < segments; ++i)
    {
        uint16_t tl = static_cast<uint16_t>(cylBase + i * 2);
        uint16_t tr = static_cast<uint16_t>(cylBase + (i + 1) * 2);
        uint16_t bl = static_cast<uint16_t>(tl + 1);
        uint16_t br = static_cast<uint16_t>(tr + 1);

        mesh.indices.push_back(tl);
        mesh.indices.push_back(tr);
        mesh.indices.push_back(bl);

        mesh.indices.push_back(tr);
        mesh.indices.push_back(br);
        mesh.indices.push_back(bl);
    }

    // Bottom hemisphere - normals point outward from hemisphere center
    // UV: v ranges from 0.25 (equator) to 0.0 (bottom pole)
    uint16_t botBase = static_cast<uint16_t>(mesh.vertices.size());
    for (int lat = 0; lat <= rings; ++lat)
    {
        float theta = (PI / 2.0f) + lat * (PI / 2.0f) / rings; // PI/2 to PI
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);
        // v: 0.25 at equator, 0.0 at bottom pole
        float v = 0.25f - (static_cast<float>(lat) / rings) * 0.25f;

        for (int lon = 0; lon <= segments; ++lon)
        {
            float phi = lon * 2.0f * PI / segments;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            float x = cosPhi * sinTheta * radius;
            float y = cosTheta * radius - halfHeight;
            float z = sinPhi * sinTheta * radius;

            // Normal points outward
            float nx = cosPhi * sinTheta;
            float ny = cosTheta;
            float nz = sinPhi * sinTheta;

            float u = static_cast<float>(lon) / segments;
            mesh.vertices.push_back({x, y, z, nx, ny, nz, 0.9f, 0.4f, 0.2f, 1.0f, u, v}); // Orange
        }
    }

    // Bottom hemisphere indices
    for (int lat = 0; lat < rings; ++lat)
    {
        for (int lon = 0; lon < segments; ++lon)
        {
            uint16_t first = static_cast<uint16_t>(botBase + lat * (segments + 1) + lon);
            uint16_t second = static_cast<uint16_t>(first + segments + 1);

            mesh.indices.push_back(first);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second);

            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second + 1);
        }
    }

    return mesh;
}

} // namespace Dot
