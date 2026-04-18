// =============================================================================
// Dot Engine - Lightmap Baker
// =============================================================================

#include "LightmapBaker.h"

#include "LightmapBakerSettings.h"
#include "../Map/MapDocument.h"
#include "../Rendering/FbxLoader.h"
#include "../Rendering/ObjLoader.h"
#include "../Rendering/PrimitiveMeshes.h"

#include "Core/Assets/AssetManager.h"
#include "Core/Map/MapTypes.h"
#include "Core/ECS/World.h"
#include "Core/Jobs/Job.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Math/Vec2.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/StaticLightingComponent.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

namespace Dot
{

namespace
{

struct BakeTriangle
{
    Vec3 positions[3];
    Vec3 normals[3];
    Vec2 uvs[3];
    uint64_t ownerId = 0;
};

enum class BakeObjectSource : uint8_t
{
    Entity = 0,
    MapFace,
};

struct BakedDirectionalLight
{
    Vec3 direction = Vec3::Forward();
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    bool castShadows = true;
};

struct BakedPointLight
{
    Vec3 position = Vec3::Zero();
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    float range = 10.0f;
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;
    bool castShadows = false;
};

struct BakedSpotLight
{
    Vec3 position = Vec3::Zero();
    Vec3 direction = Vec3::Forward();
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    float range = 10.0f;
    float innerCos = 0.9f;
    float outerCos = 0.8f;
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;
    bool castShadows = false;
};

struct BakeObject
{
    BakeObjectSource source = BakeObjectSource::Entity;
    Entity entity = kNullEntity;
    uint32 brushId = 0;
    uint32 faceIndex = 0;
    uint64_t ownerId = 0;
    int atlasIndex = 0;
    int tileX = 0;
    int tileY = 0;
    int tileSize = 0;
    float atlasScaleU = 1.0f;
    float atlasScaleV = 1.0f;
    float atlasOffsetU = 0.0f;
    float atlasOffsetV = 0.0f;
    Vec3 surfaceNormal = Vec3::Up();
    std::vector<Vec3> boundaryPositions;
    std::vector<Vec2> boundaryUvs;
    std::vector<BakeTriangle> triangles;
};

struct AtlasImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

struct PixelColor
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct PixelCoverage
{
    bool written = false;
};

struct QuantizedPoint
{
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const QuantizedPoint& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator<(const QuantizedPoint& other) const
    {
        if (x != other.x)
            return x < other.x;
        if (y != other.y)
            return y < other.y;
        return z < other.z;
    }
};

struct SharedEdgeKey
{
    QuantizedPoint a;
    QuantizedPoint b;

    bool operator==(const SharedEdgeKey& other) const
    {
        return a == other.a && b == other.b;
    }
};

struct SharedEdgeKeyHasher
{
    size_t operator()(const SharedEdgeKey& key) const
    {
        size_t hash = 1469598103934665603ull;
        auto mix = [&](int value)
        {
            hash ^= static_cast<size_t>(value);
            hash *= 1099511628211ull;
        };
        mix(key.a.x);
        mix(key.a.y);
        mix(key.a.z);
        mix(key.b.x);
        mix(key.b.y);
        mix(key.b.z);
        return hash;
    }
};

struct SharedEdgeEntry
{
    uint64_t ownerId = 0;
    Vec3 worldA = Vec3::Zero();
    Vec3 worldB = Vec3::Zero();
    Vec3 normal = Vec3::Up();
};

float Saturate(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

PixelColor ClampColor(const PixelColor& color)
{
    return {Saturate(color.r), Saturate(color.g), Saturate(color.b)};
}

PixelColor LerpColor(const PixelColor& a, const PixelColor& b, float t)
{
    return {
        a.r + ((b.r - a.r) * t),
        a.g + ((b.g - a.g) * t),
        a.b + ((b.b - a.b) * t),
    };
}

PixelColor GetDefaultBakeAmbient()
{
    // Match the editor renderer's default ambient baseline so unlit brush faces do not collapse to pure black.
    return {0.3f * 0.3f, 0.35f * 0.3f, 0.4f * 0.3f};
}

std::string BuildSceneName(const std::string& scenePath)
{
    if (scenePath.empty())
        return "UnsavedScene";

    const std::filesystem::path path(scenePath);
    const std::string stem = path.stem().string();
    return stem.empty() ? "UnsavedScene" : stem;
}

uint64_t MakeEntityOwnerId(Entity entity)
{
    return static_cast<uint64_t>(entity.id);
}

uint64_t MakeMapFaceOwnerId(uint32 brushId, uint32 faceIndex)
{
    return (1ull << 63) | (static_cast<uint64_t>(brushId) << 32) | static_cast<uint64_t>(faceIndex);
}

QuantizedPoint QuantizePoint(const Vec3& position)
{
    constexpr float scale = 1000.0f;
    return {static_cast<int>(std::round(position.x * scale)), static_cast<int>(std::round(position.y * scale)),
            static_cast<int>(std::round(position.z * scale))};
}

SharedEdgeKey MakeSharedEdgeKey(const Vec3& a, const Vec3& b)
{
    QuantizedPoint qa = QuantizePoint(a);
    QuantizedPoint qb = QuantizePoint(b);
    if (qb < qa)
        std::swap(qa, qb);
    return {qa, qb};
}

struct MapBakeSelection
{
    bool active = false;
    uint32 brushId = 0;
    int faceIndex = -1;

    bool Matches(uint32 candidateBrushId, uint32 candidateFaceIndex) const
    {
        if (!active)
            return true;
        if (candidateBrushId != brushId)
            return false;
        return faceIndex < 0 || candidateFaceIndex == static_cast<uint32>(faceIndex);
    }
};

MapBakeSelection BuildMapBakeSelection(const MapDocument* mapDocument, const std::vector<Entity>* selectedEntities)
{
    if (!mapDocument || !selectedEntities)
        return {};

    const MapSelection& selection = mapDocument->GetSelection();
    if (selection.brushId == 0)
        return {};

    MapBakeSelection result;
    result.active = true;
    result.brushId = selection.brushId;
    result.faceIndex = selection.faceIndex;
    return result;
}

Mat4 ComputeWorldMatrix(World& world, Entity entity, std::unordered_map<uint32, Mat4>& cache)
{
    const uint32 key = entity.GetIndex();
    const auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second;

    auto* transform = world.GetComponent<TransformComponent>(entity);
    Mat4 local = transform ? transform->GetLocalMatrix() : Mat4::Identity();

    Mat4 worldMatrix = local;
    if (auto* hierarchy = world.GetComponent<HierarchyComponent>(entity); hierarchy && hierarchy->parent.IsValid())
        worldMatrix = ComputeWorldMatrix(world, hierarchy->parent, cache) * local;

    cache.emplace(key, worldMatrix);
    return worldMatrix;
}

bool HasRenderableGeometry(World& world, Entity entity)
{
    return world.HasComponent<PrimitiveComponent>(entity) || world.HasComponent<MeshComponent>(entity);
}

bool LoadEntityMeshData(World& world, Entity entity, std::vector<MeshData>& outMeshes)
{
    outMeshes.clear();

    if (auto* primitive = world.GetComponent<PrimitiveComponent>(entity))
    {
        switch (primitive->type)
        {
            case PrimitiveType::Cube:
                outMeshes.push_back(GenerateCube());
                return true;
            case PrimitiveType::Sphere:
                outMeshes.push_back(GenerateSphere());
                return true;
            case PrimitiveType::Cylinder:
                outMeshes.push_back(GenerateCylinder());
                return true;
            case PrimitiveType::Plane:
                outMeshes.push_back(GeneratePlane());
                return true;
            case PrimitiveType::Cone:
                outMeshes.push_back(GenerateCone());
                return true;
            case PrimitiveType::Capsule:
                outMeshes.push_back(GenerateCapsule());
                return true;
            default:
                return false;
        }
    }

    auto* meshComponent = world.GetComponent<MeshComponent>(entity);
    if (!meshComponent || meshComponent->meshPath.empty())
        return false;

    const std::filesystem::path fullPath = AssetManager::Get().GetFullPath(meshComponent->meshPath);
    std::string extension = fullPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    std::string unusedColormap;
    if (extension == ".fbx")
        return LoadFbxFile(fullPath.string(), outMeshes, unusedColormap, fullPath.stem().string());

    MeshData meshData;
    if (!LoadObjFile(fullPath, meshData))
        return false;

    outMeshes.push_back(std::move(meshData));
    return true;
}

float ComputeMaxExtent(const std::vector<BakeTriangle>& triangles)
{
    if (triangles.empty())
        return 1.0f;

    Vec3 minBounds = triangles.front().positions[0];
    Vec3 maxBounds = triangles.front().positions[0];
    for (const BakeTriangle& triangle : triangles)
    {
        for (const Vec3& position : triangle.positions)
        {
            minBounds.x = std::min(minBounds.x, position.x);
            minBounds.y = std::min(minBounds.y, position.y);
            minBounds.z = std::min(minBounds.z, position.z);
            maxBounds.x = std::max(maxBounds.x, position.x);
            maxBounds.y = std::max(maxBounds.y, position.y);
            maxBounds.z = std::max(maxBounds.z, position.z);
        }
    }

    const Vec3 extents = maxBounds - minBounds;
    return std::max({extents.x, extents.y, extents.z, 1.0f});
}

bool ExtractTriangles(World& world, Entity entity, std::unordered_map<uint32, Mat4>& worldMatrixCache,
                      std::vector<BakeTriangle>& outTriangles)
{
    outTriangles.clear();

    std::vector<MeshData> meshes;
    if (!LoadEntityMeshData(world, entity, meshes))
        return false;

    const Mat4 worldMatrix = ComputeWorldMatrix(world, entity, worldMatrixCache);
    auto* meshComponent = world.GetComponent<MeshComponent>(entity);
    const int requestedSubmesh = meshComponent ? meshComponent->submeshIndex : -1;

    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        if (requestedSubmesh >= 0 && static_cast<int>(meshIndex) != requestedSubmesh)
            continue;

        const MeshData& mesh = meshes[meshIndex];
        if (mesh.indices.empty() || mesh.vertices.empty())
            continue;

        for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
        {
            const PrimitiveVertex& v0 = mesh.vertices[mesh.indices[index]];
            const PrimitiveVertex& v1 = mesh.vertices[mesh.indices[index + 1]];
            const PrimitiveVertex& v2 = mesh.vertices[mesh.indices[index + 2]];

            BakeTriangle triangle;
            triangle.ownerId = MakeEntityOwnerId(entity);
            triangle.positions[0] = worldMatrix.TransformPoint(Vec3(v0.x, v0.y, v0.z));
            triangle.positions[1] = worldMatrix.TransformPoint(Vec3(v1.x, v1.y, v1.z));
            triangle.positions[2] = worldMatrix.TransformPoint(Vec3(v2.x, v2.y, v2.z));
            triangle.normals[0] = worldMatrix.TransformDirection(Vec3(v0.nx, v0.ny, v0.nz)).Normalized();
            triangle.normals[1] = worldMatrix.TransformDirection(Vec3(v1.nx, v1.ny, v1.nz)).Normalized();
            triangle.normals[2] = worldMatrix.TransformDirection(Vec3(v2.nx, v2.ny, v2.nz)).Normalized();
            triangle.uvs[0] = Vec2(v0.u2, v0.v2);
            triangle.uvs[1] = Vec2(v1.u2, v1.v2);
            triangle.uvs[2] = Vec2(v2.u2, v2.v2);
            outTriangles.push_back(triangle);
        }
    }

    return !outTriangles.empty();
}

bool ExtractMapFaceTriangles(const MapBrush& brush, uint32 faceIndex, std::vector<BakeTriangle>& outTriangles)
{
    outTriangles.clear();
    if (faceIndex >= brush.faces.size())
        return false;

    const MapFace& face = brush.faces[faceIndex];
    if (face.vertexIndices.size() < 3)
        return false;

    const Vec3 faceNormal = ComputeMapFaceNormal(brush, face);
    const MapFaceLightmapBasis lightmapBasis = ComputeMapFaceLightmapBasis(brush, face);
    const uint64_t ownerId = MakeMapFaceOwnerId(brush.brushId, faceIndex);

    for (size_t index = 1; index + 1 < face.vertexIndices.size(); ++index)
    {
        const Vec3& p0 = brush.vertices[face.vertexIndices[0]];
        const Vec3& p1 = brush.vertices[face.vertexIndices[index]];
        const Vec3& p2 = brush.vertices[face.vertexIndices[index + 1]];

        BakeTriangle triangle;
        triangle.ownerId = ownerId;
        triangle.positions[0] = p0;
        triangle.positions[1] = p1;
        triangle.positions[2] = p2;
        triangle.normals[0] = faceNormal;
        triangle.normals[1] = faceNormal;
        triangle.normals[2] = faceNormal;
        triangle.uvs[0] = ComputeMapFaceLightmapUV(lightmapBasis, p0);
        triangle.uvs[1] = ComputeMapFaceLightmapUV(lightmapBasis, p1);
        triangle.uvs[2] = ComputeMapFaceLightmapUV(lightmapBasis, p2);
        outTriangles.push_back(triangle);
    }

    return !outTriangles.empty();
}

bool FindBoundaryEdgeUvs(const BakeObject& object, const Vec3& worldA, const Vec3& worldB, Vec2& outUvA, Vec2& outUvB)
{
    constexpr float epsilon = 0.001f;
    const auto matches = [&](const Vec3& left, const Vec3& right) { return (left - right).LengthSquared() <= (epsilon * epsilon); };

    if (object.boundaryPositions.size() < 2 || object.boundaryPositions.size() != object.boundaryUvs.size())
        return false;

    for (size_t index = 0; index < object.boundaryPositions.size(); ++index)
    {
        const size_t next = (index + 1) % object.boundaryPositions.size();
        const Vec3& edgeA = object.boundaryPositions[index];
        const Vec3& edgeB = object.boundaryPositions[next];
        if (matches(edgeA, worldA) && matches(edgeB, worldB))
        {
            outUvA = object.boundaryUvs[index];
            outUvB = object.boundaryUvs[next];
            return true;
        }
        if (matches(edgeA, worldB) && matches(edgeB, worldA))
        {
            outUvA = object.boundaryUvs[next];
            outUvB = object.boundaryUvs[index];
            return true;
        }
    }

    return false;
}

float DistanceToSegmentSquared(const Vec2& point, const Vec2& a, const Vec2& b, float& outT)
{
    const Vec2 ab = b - a;
    const float lengthSquared = ab.LengthSquared();
    if (lengthSquared <= 1e-8f)
    {
        outT = 0.0f;
        return (point - a).LengthSquared();
    }

    outT = std::clamp(Vec2::Dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
    const Vec2 closest = a + (ab * outT);
    return (point - closest).LengthSquared();
}

Vec3 ComputeDirectionalLightDirection(const TransformComponent& transform)
{
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    const float radX = transform.rotation.x * kDegToRad;
    const float radY = transform.rotation.y * kDegToRad;
    return Vec3(std::sin(radY) * std::cos(radX), -std::sin(radX), std::cos(radY) * std::cos(radX)).Normalized();
}

bool RayIntersectsTriangle(const Vec3& origin, const Vec3& direction, const BakeTriangle& triangle, float maxDistance)
{
    const Vec3 edge1 = triangle.positions[1] - triangle.positions[0];
    const Vec3 edge2 = triangle.positions[2] - triangle.positions[0];
    const Vec3 p = Vec3::Cross(direction, edge2);
    const float det = Vec3::Dot(edge1, p);
    if (std::abs(det) < 1e-6f)
        return false;

    const float invDet = 1.0f / det;
    const Vec3 t = origin - triangle.positions[0];
    const float u = Vec3::Dot(t, p) * invDet;
    if (u < 0.0f || u > 1.0f)
        return false;

    const Vec3 q = Vec3::Cross(t, edge1);
    const float v = Vec3::Dot(direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f)
        return false;

    const float distance = Vec3::Dot(edge2, q) * invDet;
    return distance > 0.001f && distance < maxDistance;
}

bool IsShadowed(const Vec3& origin, const Vec3& direction, float maxDistance, uint64_t ownerId,
                const std::vector<BakeTriangle>& shadowTriangles)
{
    for (const BakeTriangle& triangle : shadowTriangles)
    {
        if (triangle.ownerId == ownerId)
            continue;
        if (RayIntersectsTriangle(origin, direction, triangle, maxDistance))
            return true;
    }
    return false;
}

PixelColor EvaluateLighting(const Vec3& worldPos, const Vec3& normal, uint64_t ownerId,
                            const std::vector<BakedDirectionalLight>& directionalLights,
                            const std::vector<BakedPointLight>& pointLights,
                            const std::vector<BakedSpotLight>& spotLights,
                            const std::vector<BakeTriangle>& shadowTriangles)
{
    PixelColor result = GetDefaultBakeAmbient();
    const Vec3 safeNormal = normal.Normalized();
    const Vec3 rayOrigin = worldPos + safeNormal * 0.01f;

    for (const BakedDirectionalLight& light : directionalLights)
    {
        const Vec3 lightVector = (-light.direction).Normalized();
        const float ndotl = std::max(0.0f, Vec3::Dot(safeNormal, lightVector));
        if (ndotl <= 0.0f)
            continue;

        if (light.castShadows && IsShadowed(rayOrigin, lightVector, 5000.0f, ownerId, shadowTriangles))
            continue;

        result.r += light.color.x * light.intensity * ndotl;
        result.g += light.color.y * light.intensity * ndotl;
        result.b += light.color.z * light.intensity * ndotl;
    }

    for (const BakedPointLight& light : pointLights)
    {
        const Vec3 toLight = light.position - worldPos;
        const float distance = toLight.Length();
        if (distance <= 1e-4f || distance > light.range)
            continue;

        const Vec3 lightVector = toLight / distance;
        const float ndotl = std::max(0.0f, Vec3::Dot(safeNormal, lightVector));
        if (ndotl <= 0.0f)
            continue;

        if (light.castShadows && IsShadowed(rayOrigin, lightVector, distance, ownerId, shadowTriangles))
            continue;

        const float attenuation =
            1.0f / std::max(1.0f, light.constantAttenuation + (light.linearAttenuation * distance) +
                                       (light.quadraticAttenuation * distance * distance));
        const float contribution = light.intensity * ndotl * attenuation;
        result.r += light.color.x * contribution;
        result.g += light.color.y * contribution;
        result.b += light.color.z * contribution;
    }

    for (const BakedSpotLight& light : spotLights)
    {
        const Vec3 toLight = light.position - worldPos;
        const float distance = toLight.Length();
        if (distance <= 1e-4f || distance > light.range)
            continue;

        const Vec3 lightVector = toLight / distance;
        const float ndotl = std::max(0.0f, Vec3::Dot(safeNormal, lightVector));
        if (ndotl <= 0.0f)
            continue;

        const float spotCos = Vec3::Dot((-light.direction).Normalized(), (-lightVector).Normalized());
        if (spotCos <= light.outerCos)
            continue;

        if (light.castShadows && IsShadowed(rayOrigin, lightVector, distance, ownerId, shadowTriangles))
            continue;

        const float spotAttenuation =
            Saturate((spotCos - light.outerCos) / std::max(0.0001f, light.innerCos - light.outerCos));
        const float attenuation =
            1.0f / std::max(1.0f, light.constantAttenuation + (light.linearAttenuation * distance) +
                                       (light.quadraticAttenuation * distance * distance));
        const float contribution = light.intensity * ndotl * attenuation * spotAttenuation;
        result.r += light.color.x * contribution;
        result.g += light.color.y * contribution;
        result.b += light.color.z * contribution;
    }

    return ClampColor(result);
}

bool WriteBmp(const std::filesystem::path& path, const AtlasImage& image)
{
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty())
        return false;

    std::filesystem::create_directories(path.parent_path());

    const int rowStride = ((image.width * 3) + 3) & ~3;
    const uint32_t pixelDataSize = static_cast<uint32_t>(rowStride * image.height);
    const uint32_t fileSize = 54u + pixelDataSize;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &fileSize, sizeof(uint32_t));
    const uint32_t pixelOffset = 54u;
    std::memcpy(header + 10, &pixelOffset, sizeof(uint32_t));
    const uint32_t dibSize = 40u;
    std::memcpy(header + 14, &dibSize, sizeof(uint32_t));
    std::memcpy(header + 18, &image.width, sizeof(int32_t));
    std::memcpy(header + 22, &image.height, sizeof(int32_t));
    const uint16_t planes = 1u;
    const uint16_t bitsPerPixel = 24u;
    std::memcpy(header + 26, &planes, sizeof(uint16_t));
    std::memcpy(header + 28, &bitsPerPixel, sizeof(uint16_t));
    std::memcpy(header + 34, &pixelDataSize, sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(header), sizeof(header));

    std::vector<uint8_t> row(static_cast<size_t>(rowStride), 0);
    for (int y = image.height - 1; y >= 0; --y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const size_t src = static_cast<size_t>((y * image.width + x) * 3);
            const size_t dst = static_cast<size_t>(x * 3);
            row[dst + 0] = image.pixels[src + 2];
            row[dst + 1] = image.pixels[src + 1];
            row[dst + 2] = image.pixels[src + 0];
        }
        file.write(reinterpret_cast<const char*>(row.data()), rowStride);
    }

    return true;
}

void DilateTile(AtlasImage& atlas, std::vector<PixelCoverage>& coverage, const BakeObject& object, int dilationMargin)
{
    if (dilationMargin <= 0)
        return;

    const int width = atlas.width;
    for (int iteration = 0; iteration < dilationMargin; ++iteration)
    {
        auto previousPixels = atlas.pixels;
        auto previousCoverage = coverage;
        for (int y = object.tileY; y < object.tileY + object.tileSize; ++y)
        {
            for (int x = object.tileX; x < object.tileX + object.tileSize; ++x)
            {
                const size_t index = static_cast<size_t>(y * width + x);
                if (previousCoverage[index].written)
                    continue;

                bool filled = false;
                for (int dy = -1; dy <= 1 && !filled; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0)
                            continue;

                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < object.tileX || ny < object.tileY || nx >= object.tileX + object.tileSize ||
                            ny >= object.tileY + object.tileSize)
                        {
                            continue;
                        }

                        const size_t neighbor = static_cast<size_t>(ny * width + nx);
                        if (!previousCoverage[neighbor].written)
                            continue;

                        atlas.pixels[index * 3 + 0] = previousPixels[neighbor * 3 + 0];
                        atlas.pixels[index * 3 + 1] = previousPixels[neighbor * 3 + 1];
                        atlas.pixels[index * 3 + 2] = previousPixels[neighbor * 3 + 2];
                        coverage[index].written = true;
                        filled = true;
                        break;
                    }
                }
            }
        }
    }
}

void BlendEdgeBand(AtlasImage& atlas, const std::vector<PixelCoverage>& coverage, const BakeObject& object, const Vec2& uvA,
                   const Vec2& uvB, const PixelColor& colorA, const PixelColor& colorMid, const PixelColor& colorB)
{
    const float bandWidthUv = 2.0f / std::max(1.0f, static_cast<float>(object.tileSize - 1));
    const float bandWidthSquared = bandWidthUv * bandWidthUv;

    for (int y = object.tileY; y < object.tileY + object.tileSize; ++y)
    {
        for (int x = object.tileX; x < object.tileX + object.tileSize; ++x)
        {
            const size_t pixelIndex = static_cast<size_t>((y * atlas.width) + x);
            if (!coverage[pixelIndex].written)
                continue;

            const float normalizedU =
                static_cast<float>(x - object.tileX) / std::max(1.0f, static_cast<float>(object.tileSize - 1));
            const float normalizedV =
                1.0f - (static_cast<float>(y - object.tileY) / std::max(1.0f, static_cast<float>(object.tileSize - 1)));
            const Vec2 sampleUv(normalizedU, normalizedV);

            float edgeT = 0.0f;
            const float distanceSquared = DistanceToSegmentSquared(sampleUv, uvA, uvB, edgeT);
            if (distanceSquared > bandWidthSquared)
                continue;

            const float blendFactor = 1.0f - std::sqrt(distanceSquared) / bandWidthUv;
            PixelColor targetColor;
            if (edgeT < 0.5f)
                targetColor = LerpColor(colorA, colorMid, edgeT * 2.0f);
            else
                targetColor = LerpColor(colorMid, colorB, (edgeT - 0.5f) * 2.0f);

            PixelColor current{
                atlas.pixels[pixelIndex * 3 + 0] / 255.0f,
                atlas.pixels[pixelIndex * 3 + 1] / 255.0f,
                atlas.pixels[pixelIndex * 3 + 2] / 255.0f,
            };
            const PixelColor blended = ClampColor(LerpColor(current, targetColor, blendFactor));
            atlas.pixels[pixelIndex * 3 + 0] = static_cast<uint8_t>(blended.r * 255.0f);
            atlas.pixels[pixelIndex * 3 + 1] = static_cast<uint8_t>(blended.g * 255.0f);
            atlas.pixels[pixelIndex * 3 + 2] = static_cast<uint8_t>(blended.b * 255.0f);
        }
    }
}

std::string MakeEntityAssetStem(World& world, Entity entity)
{
    std::string stem = "Entity_" + std::to_string(entity.GetIndex());
    if (auto* name = world.GetComponent<NameComponent>(entity))
    {
        stem = name->name;
        std::replace_if(stem.begin(), stem.end(),
                        [](unsigned char ch) { return !(std::isalnum(ch) || ch == '_' || ch == '-'); }, '_');
        if (stem.empty())
            stem = "Entity_" + std::to_string(entity.GetIndex());
    }
    return stem + "_" + std::to_string(entity.GetIndex());
}

std::filesystem::path ResolveProjectAssetPath(const std::string& assetPath)
{
    if (assetPath.empty())
        return {};

    std::filesystem::path path(assetPath);
    if (path.is_absolute())
        return path;

    return std::filesystem::current_path() / "Assets" / path;
}

} // namespace

std::filesystem::path LightmapBaker::GetOutputDirectory(const std::string& scenePath) const
{
    return ResolveProjectAssetPath((std::filesystem::path("Lightmaps") / BuildSceneName(scenePath)).generic_string());
}

void LightmapBaker::OpenOutputFolder(const std::string& scenePath) const
{
    const std::filesystem::path outputDirectory = GetOutputDirectory(scenePath);
    std::error_code error;
    std::filesystem::create_directories(outputDirectory, error);
    ShellExecuteA(nullptr, "open", outputDirectory.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string LightmapBaker::BuildSceneBakeSignature(World& world, const std::string& scenePath, const MapDocument* mapDocument) const
{
    const auto& settings = LightmapBakerSettings::Get();
    std::ostringstream stream;
    stream << BuildSceneName(scenePath) << '|';
    stream << settings.texelsPerUnit << '|' << settings.atlasSize << '|' << settings.padding << '|'
           << settings.dilationMargin << '|' << static_cast<int>(settings.qualityPreset) << '|';

    world.EachEntity(
        [&](Entity entity)
        {
            if (auto* staticLighting = world.GetComponent<StaticLightingComponent>(entity))
            {
                if (!staticLighting->participateInBake || !HasRenderableGeometry(world, entity))
                    return;

                if (auto* transform = world.GetComponent<TransformComponent>(entity))
                {
                    stream << "E" << entity.GetIndex() << ':';
                    stream << transform->position.x << ',' << transform->position.y << ',' << transform->position.z << ',';
                    stream << transform->rotation.x << ',' << transform->rotation.y << ',' << transform->rotation.z << ',';
                    stream << transform->scale.x << ',' << transform->scale.y << ',' << transform->scale.z << ',';
                }

                if (auto* primitive = world.GetComponent<PrimitiveComponent>(entity))
                    stream << "P" << static_cast<int>(primitive->type) << ',';
                if (auto* mesh = world.GetComponent<MeshComponent>(entity))
                    stream << "M" << mesh->meshPath << ',' << mesh->submeshIndex << ',' << mesh->castShadow << ',';

                stream << staticLighting->castBakedShadows << ',' << staticLighting->receiveBakedLighting << ','
                       << staticLighting->resolutionScale << ';';
            }

            if (auto* dirLight = world.GetComponent<DirectionalLightComponent>(entity))
            {
                if (dirLight->lightingMode != LightingMode::Baked)
                    return;
                if (auto* transform = world.GetComponent<TransformComponent>(entity))
                {
                    stream << "DL" << entity.GetIndex() << ':' << transform->rotation.x << ',' << transform->rotation.y
                           << ',' << transform->rotation.z << ',' << dirLight->color.x << ',' << dirLight->color.y << ','
                           << dirLight->color.z << ',' << dirLight->intensity << ',' << dirLight->castShadows << ';';
                }
            }

            if (auto* pointLight = world.GetComponent<PointLightComponent>(entity))
            {
                if (pointLight->lightingMode != LightingMode::Baked)
                    return;
                if (auto* transform = world.GetComponent<TransformComponent>(entity))
                {
                    stream << "PL" << entity.GetIndex() << ':' << transform->position.x << ',' << transform->position.y
                           << ',' << transform->position.z << ',' << pointLight->color.x << ',' << pointLight->color.y
                           << ',' << pointLight->color.z << ',' << pointLight->range << ',' << pointLight->intensity << ','
                           << pointLight->castShadows << ';';
                }
            }

            if (auto* spotLight = world.GetComponent<SpotLightComponent>(entity))
            {
                if (spotLight->lightingMode != LightingMode::Baked)
                    return;
                if (auto* transform = world.GetComponent<TransformComponent>(entity))
                {
                    stream << "SL" << entity.GetIndex() << ':' << transform->position.x << ',' << transform->position.y
                           << ',' << transform->position.z << ',' << transform->rotation.x << ',' << transform->rotation.y
                           << ',' << transform->rotation.z << ',' << spotLight->range << ',' << spotLight->innerConeAngle
                           << ',' << spotLight->outerConeAngle << ',' << spotLight->intensity << ','
                           << spotLight->castShadows << ';';
                }
            }
        });

    if (mapDocument)
    {
        for (const MapBrush& brush : mapDocument->GetAsset().brushes)
        {
            if (!brush.bakedLighting.participateInBake)
                continue;

            stream << "MB" << brush.brushId << ':' << brush.bakedLighting.participateInBake << ','
                   << brush.bakedLighting.receiveBakedLighting << ',' << brush.bakedLighting.castBakedShadows << ','
                   << brush.bakedLighting.resolutionScale << '|';
            for (const Vec3& vertex : brush.vertices)
                stream << "V" << vertex.x << ',' << vertex.y << ',' << vertex.z << ';';
            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                const MapFace& face = brush.faces[faceIndex];
                stream << "F" << faceIndex << ':';
                for (uint32 vertexIndex : face.vertexIndices)
                    stream << vertexIndex << ',';
                stream << ';';
            }
        }
    }

    return stream.str();
}

void LightmapBaker::RefreshBakeStates(World& world, const std::string& scenePath, MapDocument* mapDocument)
{
    const std::string sceneSignature = BuildSceneBakeSignature(world, scenePath, mapDocument);

    world.EachEntity(
        [&](Entity entity)
        {
            auto* staticLighting = world.GetComponent<StaticLightingComponent>(entity);
            if (!staticLighting)
                return;

            std::error_code error;
            const bool hasTexture = !staticLighting->lightmapTexturePath.empty() &&
                                    std::filesystem::exists(ResolveProjectAssetPath(staticLighting->lightmapTexturePath),
                                                            error);
            if (!staticLighting->bakeValid || !hasTexture)
            {
                staticLighting->bakeValid = false;
                staticLighting->bakeStale = staticLighting->participateInBake;
                return;
            }

            staticLighting->bakeStale = staticLighting->bakeSignature != sceneSignature;
        });

    if (!mapDocument)
        return;

    for (MapBrush& brush : mapDocument->GetAsset().brushes)
    {
        for (MapFace& face : brush.faces)
        {
            const bool shouldUseBakeState =
                brush.bakedLighting.participateInBake && brush.bakedLighting.receiveBakedLighting && face.bakedLighting.useBakedLighting;

            std::error_code error;
            const bool hasTexture = !face.bakedLighting.lightmapTexturePath.empty() &&
                                    std::filesystem::exists(ResolveProjectAssetPath(face.bakedLighting.lightmapTexturePath), error);
            if (!face.bakedLighting.bakeValid || !hasTexture)
            {
                face.bakedLighting.bakeValid = false;
                face.bakedLighting.bakeStale = shouldUseBakeState;
                continue;
            }

            face.bakedLighting.bakeStale = shouldUseBakeState && face.bakedLighting.bakeSignature != sceneSignature;
        }
    }
}

bool LightmapBaker::BakeAll(World& world, const std::string& scenePath, MapDocument* mapDocument)
{
    return Bake(world, nullptr, scenePath, mapDocument);
}

bool LightmapBaker::BakeSelected(World& world, const std::vector<Entity>& selectedEntities, const std::string& scenePath,
                                 MapDocument* mapDocument)
{
    return Bake(world, &selectedEntities, scenePath, mapDocument);
}

bool LightmapBaker::ClearBakeData(World& world, const std::string& scenePath, const std::vector<Entity>* entities,
                                  MapDocument* mapDocument)
{
    m_LastSummary = {};
    const MapBakeSelection mapSelection = BuildMapBakeSelection(mapDocument, entities);
    const bool clearSelectedMapFaces = entities && mapSelection.active;

    std::vector<Entity> targets;
    if (entities && !entities->empty())
    {
        targets = *entities;
    }
    else
    {
        world.EachEntity([&](Entity entity)
                         {
                             if (world.HasComponent<StaticLightingComponent>(entity))
                                 targets.push_back(entity);
                         });
    }

    for (Entity entity : targets)
    {
        auto* staticLighting = world.GetComponent<StaticLightingComponent>(entity);
        if (!staticLighting)
            continue;

        if (!staticLighting->lightmapSidecarPath.empty())
        {
            std::error_code error;
            std::filesystem::remove(ResolveProjectAssetPath(staticLighting->lightmapSidecarPath), error);
        }

        staticLighting->lightmapTexturePath.clear();
        staticLighting->lightmapSidecarPath.clear();
        staticLighting->bakeSignature.clear();
        staticLighting->bakeValid = false;
        staticLighting->bakeStale = staticLighting->participateInBake;
        staticLighting->lightmapScaleU = 1.0f;
        staticLighting->lightmapScaleV = 1.0f;
        staticLighting->lightmapOffsetU = 0.0f;
        staticLighting->lightmapOffsetV = 0.0f;
    }

    bool mapBakeDataChanged = false;
    if (mapDocument)
    {
        for (MapBrush& brush : mapDocument->GetAsset().brushes)
        {
            if (entities && !clearSelectedMapFaces)
                break;
            if (clearSelectedMapFaces && brush.brushId != mapSelection.brushId)
                continue;

            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                if (clearSelectedMapFaces && mapSelection.faceIndex >= 0 &&
                    faceIndex != static_cast<size_t>(mapSelection.faceIndex))
                {
                    continue;
                }

                MapFaceBakeData& bakedLighting = brush.faces[faceIndex].bakedLighting;
                if (bakedLighting.lightmapTexturePath.empty() && bakedLighting.lightmapSidecarPath.empty() &&
                    bakedLighting.bakeSignature.empty() && !bakedLighting.bakeValid && !bakedLighting.bakeStale)
                {
                    continue;
                }

                if (!bakedLighting.lightmapSidecarPath.empty())
                {
                    std::error_code error;
                    std::filesystem::remove(ResolveProjectAssetPath(bakedLighting.lightmapSidecarPath), error);
                }

                bakedLighting.lightmapTexturePath.clear();
                bakedLighting.lightmapSidecarPath.clear();
                bakedLighting.bakeSignature.clear();
                bakedLighting.bakeValid = false;
                bakedLighting.bakeStale = brush.bakedLighting.participateInBake && brush.bakedLighting.receiveBakedLighting &&
                                          bakedLighting.useBakedLighting;
                bakedLighting.lightmapScaleU = 1.0f;
                bakedLighting.lightmapScaleV = 1.0f;
                bakedLighting.lightmapOffsetU = 0.0f;
                bakedLighting.lightmapOffsetV = 0.0f;
                mapBakeDataChanged = true;
            }
        }
    }

    if (!entities)
    {
        std::error_code error;
        std::filesystem::remove_all(GetOutputDirectory(scenePath), error);
    }

    if (mapDocument && mapBakeDataChanged)
        mapDocument->MarkDirty(false);

    m_LastSummary.success = true;
    m_LastSummary.outputFolder = GetOutputDirectory(scenePath).string();
    return true;
}

bool LightmapBaker::Bake(World& world, const std::vector<Entity>* selectedEntities, const std::string& scenePath,
                         MapDocument* mapDocument)
{
    m_LastSummary = {};
    const auto& settings = LightmapBakerSettings::Get();
    const std::string sceneSignature = BuildSceneBakeSignature(world, scenePath, mapDocument);
    const std::filesystem::path outputDirectory = GetOutputDirectory(scenePath);
    std::filesystem::create_directories(outputDirectory);
    const MapBakeSelection mapSelection = BuildMapBakeSelection(mapDocument, selectedEntities);
    const bool bakeSelectedMapFaces = selectedEntities && mapSelection.active;

    std::vector<Entity> targetEntities;
    std::vector<Entity> shadowCasterEntities;
    world.EachEntity(
        [&](Entity entity)
        {
            auto* staticLighting = world.GetComponent<StaticLightingComponent>(entity);
            if (!staticLighting || !staticLighting->participateInBake || !HasRenderableGeometry(world, entity))
                return;

            if (selectedEntities && std::find(selectedEntities->begin(), selectedEntities->end(), entity) == selectedEntities->end())
            {
                if (staticLighting->castBakedShadows)
                    shadowCasterEntities.push_back(entity);
                return;
            }

            targetEntities.push_back(entity);
            if (staticLighting->castBakedShadows)
                shadowCasterEntities.push_back(entity);
        });

    std::unordered_map<uint32, Mat4> worldMatrixCache;
    std::vector<BakeObject> objects;
    std::vector<BakeTriangle> shadowTriangles;
    std::vector<BakedDirectionalLight> directionalLights;
    std::vector<BakedPointLight> pointLights;
    std::vector<BakedSpotLight> spotLights;
    objects.reserve(targetEntities.size());

    world.Each<TransformComponent>(
        [&](Entity entity, TransformComponent& transform)
        {
            if (auto* dirLight = world.GetComponent<DirectionalLightComponent>(entity))
            {
                if (dirLight->lightingMode == LightingMode::Baked)
                    directionalLights.push_back(
                        {ComputeDirectionalLightDirection(transform), dirLight->color, dirLight->intensity, dirLight->castShadows});
            }

            if (auto* pointLight = world.GetComponent<PointLightComponent>(entity))
            {
                if (pointLight->lightingMode == LightingMode::Baked)
                {
                    BakedPointLight baked;
                    baked.position = transform.position;
                    baked.color = pointLight->color;
                    baked.intensity = pointLight->intensity;
                    baked.range = pointLight->range;
                    baked.constantAttenuation = pointLight->constantAttenuation;
                    baked.linearAttenuation = pointLight->linearAttenuation;
                    baked.quadraticAttenuation = pointLight->quadraticAttenuation;
                    baked.castShadows = pointLight->castShadows;
                    pointLights.push_back(baked);
                }
            }

            if (auto* spotLight = world.GetComponent<SpotLightComponent>(entity))
            {
                if (spotLight->lightingMode == LightingMode::Baked)
                {
                    BakedSpotLight baked;
                    baked.position = transform.position;
                    baked.direction = ComputeDirectionalLightDirection(transform);
                    baked.color = spotLight->color;
                    baked.intensity = spotLight->intensity;
                    baked.range = spotLight->range;
                    baked.innerCos = std::cos(spotLight->innerConeAngle * 3.14159265f / 180.0f);
                    baked.outerCos = std::cos(spotLight->outerConeAngle * 3.14159265f / 180.0f);
                    baked.constantAttenuation = spotLight->constantAttenuation;
                    baked.linearAttenuation = spotLight->linearAttenuation;
                    baked.quadraticAttenuation = spotLight->quadraticAttenuation;
                    baked.castShadows = spotLight->castShadows;
                    spotLights.push_back(baked);
                }
            }
        });

    if (directionalLights.empty() && pointLights.empty() && spotLights.empty())
    {
        m_LastSummary.error = "No baked lights are enabled. Set a light's Lighting Mode to Baked first.";
        return false;
    }

    size_t targetMapFaceCount = 0;
    size_t skippedMapFaceCount = 0;
    if (mapDocument)
    {
        for (const MapBrush& brush : mapDocument->GetAsset().brushes)
        {
            if (!brush.bakedLighting.participateInBake)
                continue;
            if (selectedEntities && !bakeSelectedMapFaces)
                break;
            if (bakeSelectedMapFaces && brush.brushId != mapSelection.brushId)
                continue;

            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                if (bakeSelectedMapFaces && mapSelection.faceIndex >= 0 &&
                    faceIndex != static_cast<size_t>(mapSelection.faceIndex))
                {
                    continue;
                }

                std::vector<BakeTriangle> triangles;
                if (!ExtractMapFaceTriangles(brush, static_cast<uint32>(faceIndex), triangles))
                {
                    ++skippedMapFaceCount;
                    continue;
                }

                BakeObject object;
                object.source = BakeObjectSource::MapFace;
                object.brushId = brush.brushId;
                object.faceIndex = static_cast<uint32>(faceIndex);
                object.ownerId = MakeMapFaceOwnerId(brush.brushId, object.faceIndex);
                object.surfaceNormal = ComputeMapFaceNormal(brush, brush.faces[faceIndex]);
                object.boundaryPositions.reserve(brush.faces[faceIndex].vertexIndices.size());
                object.boundaryUvs.reserve(brush.faces[faceIndex].vertexIndices.size());
                const MapFaceLightmapBasis basis = ComputeMapFaceLightmapBasis(brush, brush.faces[faceIndex]);
                for (uint32 vertexIndex : brush.faces[faceIndex].vertexIndices)
                {
                    const Vec3& boundaryPosition = brush.vertices[vertexIndex];
                    object.boundaryPositions.push_back(boundaryPosition);
                    object.boundaryUvs.push_back(ComputeMapFaceLightmapUV(basis, boundaryPosition));
                }
                object.triangles = std::move(triangles);
                const float maxExtent = ComputeMaxExtent(object.triangles);
                object.tileSize =
                    static_cast<int>(std::ceil(maxExtent * settings.texelsPerUnit * brush.bakedLighting.resolutionScale));
                object.tileSize = std::clamp(object.tileSize, 32, std::max(32, settings.atlasSize - (settings.padding * 2)));
                objects.push_back(std::move(object));
                ++targetMapFaceCount;
            }
        }
    }

    if (targetEntities.empty() && targetMapFaceCount == 0)
    {
        m_LastSummary.error = selectedEntities ? "No selected static entities or brush faces are configured for baking."
                                               : "No static entities or brush faces are configured for baking.";
        return false;
    }

    for (Entity entity : targetEntities)
    {
        std::vector<BakeTriangle> triangles;
        if (!ExtractTriangles(world, entity, worldMatrixCache, triangles))
            continue;

        BakeObject object;
        object.source = BakeObjectSource::Entity;
        object.entity = entity;
        object.ownerId = MakeEntityOwnerId(entity);
        object.triangles = std::move(triangles);
        const auto* staticLighting = world.GetComponent<StaticLightingComponent>(entity);
        const float maxExtent = ComputeMaxExtent(object.triangles);
        object.tileSize = static_cast<int>(std::ceil(maxExtent * settings.texelsPerUnit * staticLighting->resolutionScale));
        object.tileSize = std::clamp(object.tileSize, 32, std::max(32, settings.atlasSize - (settings.padding * 2)));

        objects.push_back(std::move(object));
    }

    for (Entity entity : shadowCasterEntities)
    {
        std::vector<BakeTriangle> triangles;
        if (!ExtractTriangles(world, entity, worldMatrixCache, triangles))
            continue;
        shadowTriangles.insert(shadowTriangles.end(), triangles.begin(), triangles.end());
    }

    if (mapDocument)
    {
        for (const MapBrush& brush : mapDocument->GetAsset().brushes)
        {
            if (!brush.bakedLighting.participateInBake || !brush.bakedLighting.castBakedShadows)
                continue;

            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                std::vector<BakeTriangle> triangles;
                if (!ExtractMapFaceTriangles(brush, static_cast<uint32>(faceIndex), triangles))
                    continue;
                shadowTriangles.insert(shadowTriangles.end(), triangles.begin(), triangles.end());
            }
        }
    }

    if (objects.empty())
    {
        m_LastSummary.error = "None of the target entities or brush faces produced usable geometry for baking.";
        return false;
    }

    std::sort(objects.begin(), objects.end(),
              [](const BakeObject& left, const BakeObject& right) { return left.tileSize > right.tileSize; });

    std::vector<AtlasImage> atlases;
    int cursorX = settings.padding;
    int cursorY = settings.padding;
    int rowHeight = 0;
    int atlasIndex = 0;
    atlases.push_back({settings.atlasSize, settings.atlasSize, std::vector<uint8_t>(static_cast<size_t>(settings.atlasSize) *
                                                                                     settings.atlasSize * 3u, 0)});

    for (BakeObject& object : objects)
    {
        if (cursorX + object.tileSize + settings.padding > settings.atlasSize)
        {
            cursorX = settings.padding;
            cursorY += rowHeight + settings.padding;
            rowHeight = 0;
        }

        if (cursorY + object.tileSize + settings.padding > settings.atlasSize)
        {
            atlases.push_back({settings.atlasSize, settings.atlasSize,
                               std::vector<uint8_t>(static_cast<size_t>(settings.atlasSize) * settings.atlasSize * 3u, 0)});
            ++atlasIndex;
            cursorX = settings.padding;
            cursorY = settings.padding;
            rowHeight = 0;
        }

        object.atlasIndex = atlasIndex;
        object.tileX = cursorX;
        object.tileY = cursorY;
        object.atlasScaleU = static_cast<float>(object.tileSize) / static_cast<float>(settings.atlasSize);
        object.atlasScaleV = static_cast<float>(object.tileSize) / static_cast<float>(settings.atlasSize);
        object.atlasOffsetU = static_cast<float>(object.tileX) / static_cast<float>(settings.atlasSize);
        object.atlasOffsetV = static_cast<float>(object.tileY) / static_cast<float>(settings.atlasSize);

        cursorX += object.tileSize + settings.padding;
        rowHeight = std::max(rowHeight, object.tileSize);
    }

    std::vector<std::vector<PixelCoverage>> coverageMaps;
    coverageMaps.reserve(atlases.size());
    for (const AtlasImage& atlas : atlases)
        coverageMaps.push_back(std::vector<PixelCoverage>(static_cast<size_t>(atlas.width * atlas.height)));

    auto bakeObjectRange = [&](size_t startIndex, size_t endIndex)
    {
        for (size_t objectIndex = startIndex; objectIndex < endIndex; ++objectIndex)
        {
            BakeObject& object = objects[objectIndex];
            AtlasImage& atlas = atlases[object.atlasIndex];
            std::vector<PixelCoverage>& coverage = coverageMaps[object.atlasIndex];

            for (const BakeTriangle& triangle : object.triangles)
            {
                const float area = Vec2::Cross(triangle.uvs[1] - triangle.uvs[0], triangle.uvs[2] - triangle.uvs[0]);
                if (std::abs(area) < 1e-6f)
                    continue;

                const auto toTileX = [&](float uv) { return object.tileX + (uv * static_cast<float>(object.tileSize - 1)); };
                const auto toTileY = [&](float uv) { return object.tileY + ((1.0f - uv) * static_cast<float>(object.tileSize - 1)); };

                const float minX = std::floor(std::min({toTileX(triangle.uvs[0].x), toTileX(triangle.uvs[1].x), toTileX(triangle.uvs[2].x)}));
                const float maxX = std::ceil(std::max({toTileX(triangle.uvs[0].x), toTileX(triangle.uvs[1].x), toTileX(triangle.uvs[2].x)}));
                const float minY = std::floor(std::min({toTileY(triangle.uvs[0].y), toTileY(triangle.uvs[1].y), toTileY(triangle.uvs[2].y)}));
                const float maxY = std::ceil(std::max({toTileY(triangle.uvs[0].y), toTileY(triangle.uvs[1].y), toTileY(triangle.uvs[2].y)}));

                const int beginX = std::clamp(static_cast<int>(minX), object.tileX, object.tileX + object.tileSize - 1);
                const int endX = std::clamp(static_cast<int>(maxX), object.tileX, object.tileX + object.tileSize - 1);
                const int beginY = std::clamp(static_cast<int>(minY), object.tileY, object.tileY + object.tileSize - 1);
                const int endY = std::clamp(static_cast<int>(maxY), object.tileY, object.tileY + object.tileSize - 1);

                for (int y = beginY; y <= endY; ++y)
                {
                    for (int x = beginX; x <= endX; ++x)
                    {
                        const float normalizedU =
                            static_cast<float>(x - object.tileX) / std::max(1.0f, static_cast<float>(object.tileSize - 1));
                        const float normalizedV =
                            1.0f - (static_cast<float>(y - object.tileY) / std::max(1.0f, static_cast<float>(object.tileSize - 1)));
                        const Vec2 sampleUv(normalizedU, normalizedV);

                        const float invArea = 1.0f / area;
                        const float w0 = Vec2::Cross(triangle.uvs[1] - sampleUv, triangle.uvs[2] - sampleUv) * invArea;
                        const float w1 = Vec2::Cross(triangle.uvs[2] - sampleUv, triangle.uvs[0] - sampleUv) * invArea;
                        const float w2 = 1.0f - w0 - w1;
                        if (std::min({w0, w1, w2}) < -0.001f)
                            continue;

                        const Vec3 worldPos =
                            (triangle.positions[0] * w0) + (triangle.positions[1] * w1) + (triangle.positions[2] * w2);
                        const Vec3 worldNormal =
                            ((triangle.normals[0] * w0) + (triangle.normals[1] * w1) + (triangle.normals[2] * w2)).Normalized();
                        const PixelColor lighting =
                            EvaluateLighting(worldPos, worldNormal, object.ownerId, directionalLights, pointLights, spotLights,
                                             shadowTriangles);

                        const size_t pixelIndex = static_cast<size_t>((y * atlas.width) + x);
                        atlas.pixels[pixelIndex * 3 + 0] = static_cast<uint8_t>(Saturate(lighting.r) * 255.0f);
                        atlas.pixels[pixelIndex * 3 + 1] = static_cast<uint8_t>(Saturate(lighting.g) * 255.0f);
                        atlas.pixels[pixelIndex * 3 + 2] = static_cast<uint8_t>(Saturate(lighting.b) * 255.0f);
                        coverage[pixelIndex].written = true;
                    }
                }
            }
        }
    };

    if (JobSystem::Get().IsInitialized() && objects.size() > 1)
    {
        const uint32 workerCount = std::max<uint32>(1, JobSystem::Get().GetWorkerCount());
        const size_t batchCount = std::min<size_t>(objects.size(), workerCount);
        JobCounter counter(static_cast<int32>(batchCount));
        size_t begin = 0;
        for (size_t batch = 0; batch < batchCount; ++batch)
        {
            const size_t remaining = objects.size() - begin;
            const size_t batchSize = remaining / (batchCount - batch);
            const size_t end = begin + batchSize;
            JobSystem::Get().Schedule(Job::CreateLambda([&, begin, end]() { bakeObjectRange(begin, end); }, &counter));
            begin = end;
        }
        JobSystem::Get().WaitForCounter(&counter);
    }
    else
    {
        bakeObjectRange(0, objects.size());
    }

    std::unordered_map<uint64_t, size_t> objectIndexByOwnerId;
    objectIndexByOwnerId.reserve(objects.size());
    std::unordered_map<SharedEdgeKey, std::vector<SharedEdgeEntry>, SharedEdgeKeyHasher> sharedEdges;
    for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
    {
        objectIndexByOwnerId.emplace(objects[objectIndex].ownerId, objectIndex);
        const BakeObject& object = objects[objectIndex];
        if (object.source != BakeObjectSource::MapFace || object.boundaryPositions.size() < 2)
            continue;

        for (size_t edgeIndex = 0; edgeIndex < object.boundaryPositions.size(); ++edgeIndex)
        {
            const size_t next = (edgeIndex + 1) % object.boundaryPositions.size();
            SharedEdgeEntry entry;
            entry.ownerId = object.ownerId;
            entry.worldA = object.boundaryPositions[edgeIndex];
            entry.worldB = object.boundaryPositions[next];
            entry.normal = object.surfaceNormal;
            sharedEdges[MakeSharedEdgeKey(entry.worldA, entry.worldB)].push_back(entry);
        }
    }

    for (const auto& [edgeKey, entries] : sharedEdges)
    {
        if (entries.size() < 2)
            continue;

        for (size_t leftIndex = 0; leftIndex + 1 < entries.size(); ++leftIndex)
        {
            for (size_t rightIndex = leftIndex + 1; rightIndex < entries.size(); ++rightIndex)
            {
                const SharedEdgeEntry& left = entries[leftIndex];
                const SharedEdgeEntry& right = entries[rightIndex];
                if (left.ownerId == right.ownerId)
                    continue;
                if (Vec3::Dot(left.normal.Normalized(), right.normal.Normalized()) < 0.995f)
                    continue;

                const auto leftObjectIt = objectIndexByOwnerId.find(left.ownerId);
                const auto rightObjectIt = objectIndexByOwnerId.find(right.ownerId);
                if (leftObjectIt == objectIndexByOwnerId.end() || rightObjectIt == objectIndexByOwnerId.end())
                    continue;

                BakeObject& leftObject = objects[leftObjectIt->second];
                BakeObject& rightObject = objects[rightObjectIt->second];
                Vec2 leftUvA;
                Vec2 leftUvB;
                Vec2 rightUvA;
                Vec2 rightUvB;
                if (!FindBoundaryEdgeUvs(leftObject, left.worldA, left.worldB, leftUvA, leftUvB) ||
                    !FindBoundaryEdgeUvs(rightObject, left.worldA, left.worldB, rightUvA, rightUvB))
                {
                    continue;
                }

                const Vec3 edgeMidpoint = (left.worldA + left.worldB) * 0.5f;
                const PixelColor edgeColorA =
                    EvaluateLighting(left.worldA, left.normal, left.ownerId, directionalLights, pointLights, spotLights, shadowTriangles);
                const PixelColor edgeColorMid =
                    EvaluateLighting(edgeMidpoint, left.normal, left.ownerId, directionalLights, pointLights, spotLights, shadowTriangles);
                const PixelColor edgeColorB =
                    EvaluateLighting(left.worldB, left.normal, left.ownerId, directionalLights, pointLights, spotLights, shadowTriangles);

                BlendEdgeBand(atlases[leftObject.atlasIndex], coverageMaps[leftObject.atlasIndex], leftObject, leftUvA, leftUvB,
                              edgeColorA, edgeColorMid, edgeColorB);
                BlendEdgeBand(atlases[rightObject.atlasIndex], coverageMaps[rightObject.atlasIndex], rightObject, rightUvA, rightUvB,
                              edgeColorA, edgeColorMid, edgeColorB);
            }
        }
    }

    for (const BakeObject& object : objects)
        DilateTile(atlases[object.atlasIndex], coverageMaps[object.atlasIndex], object, settings.dilationMargin);

    std::vector<std::string> atlasAssetPaths;
    atlasAssetPaths.reserve(atlases.size());
    for (size_t atlasIdx = 0; atlasIdx < atlases.size(); ++atlasIdx)
    {
        const std::filesystem::path atlasPath = outputDirectory / ("lightmap_" + std::to_string(atlasIdx) + ".bmp");
        if (!WriteBmp(atlasPath, atlases[atlasIdx]))
        {
            m_LastSummary.error = "Failed to write baked lightmap atlas to disk.";
            return false;
        }

        atlasAssetPaths.push_back((std::filesystem::path("Lightmaps") / BuildSceneName(scenePath) /
                                   ("lightmap_" + std::to_string(atlasIdx) + ".bmp"))
                                      .generic_string());
    }

    std::ofstream manifest(outputDirectory / "manifest.txt", std::ios::trunc);
    if (manifest.is_open())
    {
        manifest << "scene=" << BuildSceneName(scenePath) << '\n';
        manifest << "signature=" << sceneSignature << '\n';
        manifest << "atlases=" << atlases.size() << '\n';
        manifest << "texelsPerUnit=" << settings.texelsPerUnit << '\n';
        manifest << "atlasSize=" << settings.atlasSize << '\n';
    }

    bool mapBakeDataChanged = false;
    for (const BakeObject& object : objects)
    {
        if (object.source == BakeObjectSource::Entity)
        {
            auto* staticLighting = world.GetComponent<StaticLightingComponent>(object.entity);
            if (!staticLighting)
                continue;

            const std::string assetStem = MakeEntityAssetStem(world, object.entity);
            const std::filesystem::path sidecarPath = outputDirectory / (assetStem + ".lightmesh.txt");
            std::ofstream sidecar(sidecarPath, std::ios::trunc);
            if (sidecar.is_open())
            {
                sidecar << "entity=" << object.entity.GetIndex() << '\n';
                sidecar << "atlas=" << object.atlasIndex << '\n';
                sidecar << "uvSource=primary\n";
                sidecar << "scaleU=" << object.atlasScaleU << '\n';
                sidecar << "scaleV=" << object.atlasScaleV << '\n';
                sidecar << "offsetU=" << object.atlasOffsetU << '\n';
                sidecar << "offsetV=" << object.atlasOffsetV << '\n';
            }

            staticLighting->lightmapTexturePath = atlasAssetPaths[object.atlasIndex];
            staticLighting->lightmapSidecarPath =
                (std::filesystem::path("Lightmaps") / BuildSceneName(scenePath) / (assetStem + ".lightmesh.txt"))
                    .generic_string();
            staticLighting->lightmapScaleU = object.atlasScaleU;
            staticLighting->lightmapScaleV = object.atlasScaleV;
            staticLighting->lightmapOffsetU = object.atlasOffsetU;
            staticLighting->lightmapOffsetV = object.atlasOffsetV;
            staticLighting->bakeSignature = sceneSignature;
            staticLighting->bakeValid = true;
            staticLighting->bakeStale = false;
            continue;
        }

        if (!mapDocument)
            continue;

        MapBrush* brush = mapDocument->GetAsset().FindBrush(object.brushId);
        if (!brush || object.faceIndex >= brush->faces.size())
            continue;

        MapFace& face = brush->faces[object.faceIndex];
        const std::string assetStem = "Brush_" + std::to_string(object.brushId) + "_Face_" + std::to_string(object.faceIndex);
        const std::filesystem::path sidecarPath = outputDirectory / (assetStem + ".lightface.txt");
        std::ofstream sidecar(sidecarPath, std::ios::trunc);
        if (sidecar.is_open())
        {
            sidecar << "brush=" << object.brushId << '\n';
            sidecar << "face=" << object.faceIndex << '\n';
            sidecar << "atlas=" << object.atlasIndex << '\n';
            sidecar << "uvSource=face_lightmap\n";
            sidecar << "scaleU=" << object.atlasScaleU << '\n';
            sidecar << "scaleV=" << object.atlasScaleV << '\n';
            sidecar << "offsetU=" << object.atlasOffsetU << '\n';
            sidecar << "offsetV=" << object.atlasOffsetV << '\n';
        }

        face.bakedLighting.lightmapTexturePath = atlasAssetPaths[object.atlasIndex];
        face.bakedLighting.lightmapSidecarPath =
            (std::filesystem::path("Lightmaps") / BuildSceneName(scenePath) / (assetStem + ".lightface.txt"))
                .generic_string();
        face.bakedLighting.lightmapScaleU = object.atlasScaleU;
        face.bakedLighting.lightmapScaleV = object.atlasScaleV;
        face.bakedLighting.lightmapOffsetU = object.atlasOffsetU;
        face.bakedLighting.lightmapOffsetV = object.atlasOffsetV;
        face.bakedLighting.bakeSignature = sceneSignature;
        face.bakedLighting.bakeValid = true;
        face.bakedLighting.bakeStale = false;
        mapBakeDataChanged = true;
    }

    if (mapDocument && mapBakeDataChanged)
        mapDocument->MarkDirty(false);

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeBuffer[64] = {};
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    m_LastSummary.success = true;
    m_LastSummary.atlasCount = static_cast<int>(atlases.size());
    m_LastSummary.bakedEntityCount = static_cast<int>(objects.size());
    m_LastSummary.estimatedBytes = static_cast<size_t>(atlases.size()) * settings.atlasSize * settings.atlasSize * 3u;
    m_LastSummary.outputFolder = outputDirectory.string();
    m_LastSummary.lastBakeTimestamp = timeBuffer;
    const size_t requestedObjectCount = targetEntities.size() + targetMapFaceCount;
    if (objects.size() != requestedObjectCount || skippedMapFaceCount > 0)
        m_LastSummary.warning = "Some selected entities or brush faces were skipped because no supported bake geometry could be generated.";

    return true;
}

} // namespace Dot
