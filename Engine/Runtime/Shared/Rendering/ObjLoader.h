// =============================================================================
// Dot Engine - OBJ Mesh Loader
// =============================================================================
// Simple Wavefront OBJ file parser for loading 3D meshes.
// =============================================================================

#pragma once

#include "PrimitiveMeshes.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

/// Load a Wavefront OBJ file and convert to MeshData format
inline bool LoadObjFile(const std::filesystem::path& filepath, MeshData& outMesh)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    auto FloatBits = [](float value) -> uint32_t
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(uint32_t));
        return bits;
    };

    using VertexKey = std::array<uint32_t, 12>;
    struct VertexKeyHash
    {
        size_t operator()(const VertexKey& key) const noexcept
        {
            size_t hash = 1469598103934665603ull;
            for (uint32_t value : key)
            {
                hash ^= value;
                hash *= 1099511628211ull;
            }
            return hash;
        }
    };

    auto MakeVertexKey = [&](const PrimitiveVertex& vertex) -> VertexKey
    {
        return VertexKey{FloatBits(vertex.x),  FloatBits(vertex.y),  FloatBits(vertex.z), FloatBits(vertex.nx),
                         FloatBits(vertex.ny), FloatBits(vertex.nz), FloatBits(vertex.r), FloatBits(vertex.g),
                         FloatBits(vertex.b),  FloatBits(vertex.a),  FloatBits(vertex.u), FloatBits(vertex.v)};
    };

    std::vector<float> positions; // x, y, z
    std::vector<float> normals;   // nx, ny, nz
    std::vector<float> texcoords; // u, v (new)
    std::vector<PrimitiveVertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexToIndex;
    vertexToIndex.reserve(65536);

    bool hasTexCoords = false;

    std::string line;
    while (std::getline(file, line))
    {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v")
        {
            // Vertex position
            float x, y, z;
            iss >> x >> y >> z;
            positions.push_back(x);
            positions.push_back(y);
            positions.push_back(z);

            // Track bounds
            if (positions.size() == 3)
            {
                // First vertex - initialize bounds
                outMesh.boundsMinX = outMesh.boundsMaxX = x;
                outMesh.boundsMinY = outMesh.boundsMaxY = y;
                outMesh.boundsMinZ = outMesh.boundsMaxZ = z;
            }
            else
            {
                // Expand bounds
                if (x < outMesh.boundsMinX)
                    outMesh.boundsMinX = x;
                if (x > outMesh.boundsMaxX)
                    outMesh.boundsMaxX = x;
                if (y < outMesh.boundsMinY)
                    outMesh.boundsMinY = y;
                if (y > outMesh.boundsMaxY)
                    outMesh.boundsMaxY = y;
                if (z < outMesh.boundsMinZ)
                    outMesh.boundsMinZ = z;
                if (z > outMesh.boundsMaxZ)
                    outMesh.boundsMaxZ = z;
            }
        }
        else if (prefix == "vt")
        {
            // Texture coordinate
            float u, v;
            iss >> u >> v;
            texcoords.push_back(u);
            texcoords.push_back(v);
            hasTexCoords = true;
        }
        else if (prefix == "vn")
        {
            // Vertex normal
            float nx, ny, nz;
            iss >> nx >> ny >> nz;
            normals.push_back(nx);
            normals.push_back(ny);
            normals.push_back(nz);
        }
        else if (prefix == "f")
        {
            // Face - format: v/vt/vn or v//vn or v
            std::string vertexData;
            std::vector<int> facePositionIndices;
            std::vector<int> faceTexIndices;
            std::vector<int> faceNormalIndices;

            while (iss >> vertexData)
            {
                int posIdx = 0, texIdx = 0, normIdx = 0;

                // Parse vertex/texcoord/normal format
                size_t firstSlash = vertexData.find('/');
                if (firstSlash == std::string::npos)
                {
                    // Just position index
                    posIdx = std::stoi(vertexData);
                }
                else
                {
                    posIdx = std::stoi(vertexData.substr(0, firstSlash));
                    size_t secondSlash = vertexData.find('/', firstSlash + 1);
                    if (secondSlash != std::string::npos)
                    {
                        // v/vt/vn or v//vn
                        std::string texPart = vertexData.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                        if (!texPart.empty())
                            texIdx = std::stoi(texPart);
                        normIdx = std::stoi(vertexData.substr(secondSlash + 1));
                    }
                    else
                    {
                        // v/vt
                        texIdx = std::stoi(vertexData.substr(firstSlash + 1));
                    }
                }

                // OBJ indices are 1-based, convert to 0-based
                facePositionIndices.push_back(posIdx - 1);
                faceTexIndices.push_back(texIdx > 0 ? texIdx - 1 : -1);
                faceNormalIndices.push_back(normIdx > 0 ? normIdx - 1 : -1);
            }

            // Triangulate face (fan triangulation for convex polygons)
            for (size_t i = 1; i + 1 < facePositionIndices.size(); ++i)
            {
                int indices3[3] = {0, static_cast<int>(i), static_cast<int>(i + 1)};

                for (int idx : indices3)
                {
                    int posIdx = facePositionIndices[idx];
                    int texIdx = faceTexIndices[idx];
                    int normIdx = faceNormalIndices[idx];

                    PrimitiveVertex v = {};

                    // Position
                    if (posIdx >= 0 && posIdx * 3 + 2 < static_cast<int>(positions.size()))
                    {
                        v.x = positions[posIdx * 3 + 0];
                        v.y = positions[posIdx * 3 + 1];
                        v.z = positions[posIdx * 3 + 2];
                    }

                    // Normal
                    if (normIdx >= 0 && normIdx * 3 + 2 < static_cast<int>(normals.size()))
                    {
                        v.nx = normals[normIdx * 3 + 0];
                        v.ny = normals[normIdx * 3 + 1];
                        v.nz = normals[normIdx * 3 + 2];
                    }
                    else
                    {
                        // Default normal (up)
                        v.nx = 0.0f;
                        v.ny = 1.0f;
                        v.nz = 0.0f;
                    }

                    // UV
                    if (texIdx >= 0 && texIdx * 2 + 1 < static_cast<int>(texcoords.size()))
                    {
                        v.u = texcoords[texIdx * 2 + 0];
                        v.v = texcoords[texIdx * 2 + 1];
                    }
                    else
                    {
                        // Default UV
                        v.u = 0.0f;
                        v.v = 0.0f;
                    }

                    // Default gray color
                    v.r = 0.7f;
                    v.g = 0.7f;
                    v.b = 0.7f;
                    v.a = 1.0f;

                    const VertexKey key = MakeVertexKey(v);
                    auto it = vertexToIndex.find(key);
                    if (it != vertexToIndex.end())
                    {
                        indices.push_back(it->second);
                    }
                    else
                    {
                        const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(v);
                        vertexToIndex.emplace(key, newIndex);
                        indices.push_back(newIndex);
                    }
                }
            }
        }
    }

    file.close();

    if (vertices.empty())
        return false;

    // Apply improved Triplanar UV mapping if no UVs were found in the file
    if (!hasTexCoords)
    {
        // Calculate mesh bounds for normalization
        float boundsWidth = outMesh.boundsMaxX - outMesh.boundsMinX;
        float boundsHeight = outMesh.boundsMaxY - outMesh.boundsMinY;
        float boundsDepth = outMesh.boundsMaxZ - outMesh.boundsMinZ;

        // Prevent division by zero
        if (boundsWidth < 0.0001f)
            boundsWidth = 1.0f;
        if (boundsHeight < 0.0001f)
            boundsHeight = 1.0f;
        if (boundsDepth < 0.0001f)
            boundsDepth = 1.0f;

        // Calculate center for centering UVs
        float centerX = (outMesh.boundsMinX + outMesh.boundsMaxX) * 0.5f;
        float centerY = (outMesh.boundsMinY + outMesh.boundsMaxY) * 0.5f;
        float centerZ = (outMesh.boundsMinZ + outMesh.boundsMaxZ) * 0.5f;

        // Use the largest dimension for uniform texture scaling
        float maxDim = std::max({boundsWidth, boundsHeight, boundsDepth});
        float texScale = 1.0f / maxDim; // Normalizes to approximately 0-1 range

        for (auto& v : vertices)
        {
            // Center the coordinates
            float lx = v.x - centerX;
            float ly = v.y - centerY;
            float lz = v.z - centerZ;

            float absX = std::abs(v.nx);
            float absY = std::abs(v.ny);
            float absZ = std::abs(v.nz);

            // Triplanar projection based on dominant normal axis
            if (absX >= absY && absX >= absZ)
            {
                // X-facing: project onto YZ plane
                v.u = (lz * texScale) + 0.5f;
                v.v = (ly * texScale) + 0.5f;
                // Flip U for negative X faces to avoid mirroring
                if (v.nx < 0)
                    v.u = 1.0f - v.u;
            }
            else if (absY >= absX && absY >= absZ)
            {
                // Y-facing: project onto XZ plane (floor/ceiling)
                v.u = (lx * texScale) + 0.5f;
                v.v = (lz * texScale) + 0.5f;
                // Flip V for negative Y faces
                if (v.ny < 0)
                    v.v = 1.0f - v.v;
            }
            else
            {
                // Z-facing: project onto XY plane
                v.u = (lx * texScale) + 0.5f;
                v.v = (ly * texScale) + 0.5f;
                // Flip U for positive Z faces to maintain consistent winding
                if (v.nz > 0)
                    v.u = 1.0f - v.u;
            }
        }
    }

    outMesh.vertices = std::move(vertices);
    outMesh.indices = std::move(indices);
    return true;
}

} // namespace Dot
