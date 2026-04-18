// =============================================================================
// Dot Engine - FBX Mesh Loader
// =============================================================================
// FBX file parser using ufbx for loading 3D meshes and textures.
// Supports multi-material meshes with per-material submeshes.
// =============================================================================

#pragma once

#include "../../../ThirdParty/ufbx/ufbx.h"
#include "PrimitiveMeshes.h"

#include <array>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

/// Extract embedded texture from FBX and save to disk
inline std::string ExtractEmbeddedTexture(ufbx_texture* tex)
{
    if (!tex || tex->content.size == 0 || !tex->content.data)
        return "";

    std::string texFilename = std::filesystem::path(tex->filename.data).filename().string();
    if (texFilename.empty())
        texFilename = "embedded_texture.png";

    std::filesystem::path exePath = std::filesystem::current_path();
    std::filesystem::path texDir = exePath / "Assets" / "Textures";
    if (!std::filesystem::exists(texDir))
        std::filesystem::create_directories(texDir);

    std::filesystem::path texFullPath = texDir / texFilename;

    if (!std::filesystem::exists(texFullPath))
    {
        std::ofstream outFile(texFullPath, std::ios::binary);
        if (outFile.is_open())
        {
            outFile.write(static_cast<const char*>(tex->content.data), tex->content.size);
            outFile.close();
            printf("Extracted embedded texture: %s (%zu bytes)\n", texFilename.c_str(), tex->content.size);
        }
    }

    return "Textures/" + texFilename;
}

/// Get texture path from material (embedded or external)
inline std::string GetMaterialTexturePath(ufbx_material* mat)
{
    ufbx_texture* tex = nullptr;

    if (mat->pbr.base_color.texture)
        tex = mat->pbr.base_color.texture;
    else if (mat->fbx.diffuse_color.texture)
        tex = mat->fbx.diffuse_color.texture;

    if (!tex)
        return "";

    // Check for embedded content
    if (tex->content.size > 0 && tex->content.data)
        return ExtractEmbeddedTexture(tex);
    else
        return tex->filename.data;
}

/// Load an FBX file and convert to MeshData format with per-material submeshes
/// @param filepath Full path to FBX file
/// @param outMesh Output mesh data with vertices, indices, and submeshes
/// @param outColormapPath Legacy: path to first colormap (for backward compatibility)
/// @param meshBaseName Base name for generated materials (e.g., "sponza")

/// Get the number of unique materials (submeshes) in an FBX file
/// Returns 0 on error, or 1+ on success
inline size_t GetFbxSubmeshCount(const std::string& filepath)
{
    ufbx_load_opts opts = {0};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filepath.c_str(), &opts, &error);

    if (!scene)
        return 0;

    // Count unique materials
    std::set<ufbx_material*> uniqueMats;
    for (size_t nodeIdx = 0; nodeIdx < scene->nodes.count; ++nodeIdx)
    {
        ufbx_node* node = scene->nodes.data[nodeIdx];
        if (!node->mesh)
            continue;

        for (size_t matIdx = 0; matIdx < node->mesh->materials.count; ++matIdx)
        {
            uniqueMats.insert(node->mesh->materials.data[matIdx]);
        }
    }

    size_t count = uniqueMats.empty() ? 1 : uniqueMats.size();
    ufbx_free_scene(scene);
    return count;
}

// Load an FBX file and convert to multiple MeshData objects (one per material)
inline bool LoadFbxFile(const std::string& filepath, std::vector<MeshData>& outMeshes, std::string& /*outColormapPath*/,
                        const std::string& meshBaseName = "")
{
    ufbx_load_opts opts = {0};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filepath.c_str(), &opts, &error);

    if (!scene)
    {
        printf("Failed to load FBX: %s\n", error.description.data);
        return false;
    }

    outMeshes.clear();

    // Collect all scene materials and map them to separate MeshData objects
    std::map<ufbx_material*, size_t> materialToMeshIndex;

    // First pass: collect unique materials and create MeshData entries
    for (size_t nodeIdx = 0; nodeIdx < scene->nodes.count; ++nodeIdx)
    {
        ufbx_node* node = scene->nodes.data[nodeIdx];
        if (!node->mesh)
            continue;

        ufbx_mesh* mesh = node->mesh;
        for (size_t matIdx = 0; matIdx < mesh->materials.count; ++matIdx)
        {
            ufbx_material* mat = mesh->materials.data[matIdx];
            if (materialToMeshIndex.find(mat) == materialToMeshIndex.end())
            {
                size_t meshIdx = outMeshes.size();
                materialToMeshIndex[mat] = meshIdx;

                MeshData newMesh;
                newMesh.boundsMinX = newMesh.boundsMinY = newMesh.boundsMinZ = FLT_MAX;
                newMesh.boundsMaxX = newMesh.boundsMaxY = newMesh.boundsMaxZ = -FLT_MAX;

                // Create one submesh for this mesh (1:1 mapping)
                Submesh sub;
                sub.indexStart = 0;
                sub.indexCount = 0;

                // Generate material path (same naming convention)
                std::string matName = mat->name.data;
                if (matName.empty())
                    matName = "Material" + std::to_string(meshIdx);
                std::replace(matName.begin(), matName.end(), ' ', '_');
                std::replace(matName.begin(), matName.end(), '/', '_');
                std::replace(matName.begin(), matName.end(), '\\', '_');

                std::string baseName = meshBaseName.empty() ? "mesh" : meshBaseName;
                sub.materialPath = "Materials/" + baseName + "_" + matName + ".dotmat";

                // DISABLED: Texture loading causes crashes - leave empty
                // sub.texturePath = GetMaterialTexturePath(mat);
                sub.texturePath = ""; // No texture to prevent crashes

                newMesh.submeshes.push_back(sub);
                outMeshes.push_back(newMesh);
            }
        }
    }

    // If no materials found, create default mesh
    if (outMeshes.empty())
    {
        MeshData defaultMesh;
        defaultMesh.boundsMinX = defaultMesh.boundsMinY = defaultMesh.boundsMinZ = FLT_MAX;
        defaultMesh.boundsMaxX = defaultMesh.boundsMaxY = defaultMesh.boundsMaxZ = -FLT_MAX;

        Submesh defaultSub;
        defaultSub.indexStart = 0;
        defaultSub.indexCount = 0;
        defaultSub.materialPath = "";
        defaultSub.texturePath = "";

        defaultMesh.submeshes.push_back(defaultSub);
        outMeshes.push_back(defaultMesh);
    }

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

    std::vector<std::unordered_map<VertexKey, uint32_t, VertexKeyHash>> vertexToIndexPerMesh(outMeshes.size());
    for (auto& map : vertexToIndexPerMesh)
        map.reserve(65536);

    // Second pass: process geometry, grouping by material (into separate MeshData)
    for (size_t nodeIdx = 0; nodeIdx < scene->nodes.count; ++nodeIdx)
    {
        ufbx_node* node = scene->nodes.data[nodeIdx];
        if (!node->mesh)
            continue;

        ufbx_mesh* mesh = node->mesh;

        for (size_t fIdx = 0; fIdx < mesh->faces.count; ++fIdx)
        {
            ufbx_face face = mesh->faces.data[fIdx];
            uint32_t triCount = face.num_indices > 2 ? face.num_indices - 2 : 0;
            if (triCount == 0)
                continue;

            // Determine which MeshData this face belongs to
            size_t meshIdx = 0;
            if (mesh->face_material.count > fIdx)
            {
                ufbx_material* faceMat = mesh->materials.data[mesh->face_material.data[fIdx]];
                auto it = materialToMeshIndex.find(faceMat);
                if (it != materialToMeshIndex.end())
                    meshIdx = it->second;
            }

            // If we somehow didn't create a mesh for this material (fallback)
            if (meshIdx >= outMeshes.size())
                meshIdx = 0;

            MeshData& targetMesh = outMeshes[meshIdx];
            auto& vertexToIndex = vertexToIndexPerMesh[meshIdx];

            // Triangulate the face
            std::vector<uint32_t> triIndices(triCount * 3);
            ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);

            // Process each triangle vertex
            for (uint32_t index : triIndices)
            {
                PrimitiveVertex vertex;

                // Position
                ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                ufbx_vec3 worldPos = ufbx_transform_position(&node->node_to_world, pos);
                vertex.x = (float)worldPos.x;
                vertex.y = (float)worldPos.y;
                vertex.z = (float)worldPos.z;

                // Normal
                if (mesh->vertex_normal.exists)
                {
                    ufbx_vec3 norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                    ufbx_vec3 worldNorm = ufbx_transform_direction(&node->node_to_world, norm);
                    vertex.nx = (float)worldNorm.x;
                    vertex.ny = (float)worldNorm.y;
                    vertex.nz = (float)worldNorm.z;
                }
                else
                {
                    vertex.nx = 0.0f;
                    vertex.ny = 1.0f;
                    vertex.nz = 0.0f;
                }

                // UV
                if (mesh->vertex_uv.exists)
                {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                    vertex.u = (float)uv.x;
                    vertex.v = 1.0f - (float)uv.y;
                }
                else
                {
                    vertex.u = 0.0f;
                    vertex.v = 0.0f;
                }

                // Color
                vertex.r = vertex.g = vertex.b = vertex.a = 1.0f;

                uint32_t vertexIndex = 0;
                const VertexKey key = MakeVertexKey(vertex);
                auto it = vertexToIndex.find(key);
                if (it != vertexToIndex.end())
                {
                    vertexIndex = it->second;
                }
                else
                {
                    // Bounds
                    targetMesh.boundsMinX = std::min(targetMesh.boundsMinX, vertex.x);
                    targetMesh.boundsMinY = std::min(targetMesh.boundsMinY, vertex.y);
                    targetMesh.boundsMinZ = std::min(targetMesh.boundsMinZ, vertex.z);
                    targetMesh.boundsMaxX = std::max(targetMesh.boundsMaxX, vertex.x);
                    targetMesh.boundsMaxY = std::max(targetMesh.boundsMaxY, vertex.y);
                    targetMesh.boundsMaxZ = std::max(targetMesh.boundsMaxZ, vertex.z);

                    vertexIndex = static_cast<uint32_t>(targetMesh.vertices.size());
                    targetMesh.vertices.push_back(vertex);
                    vertexToIndex.emplace(key, vertexIndex);
                }
                targetMesh.indices.push_back(vertexIndex);
            }
        }
    }

    // Finalize index counts for submeshes
    for (auto& mesh : outMeshes)
    {
        if (!mesh.submeshes.empty())
        {
            mesh.submeshes[0].indexStart = 0;
            mesh.submeshes[0].indexCount = (uint32_t)mesh.indices.size();
        }
    }

    printf("Loaded FBX split into %zu entities\n", outMeshes.size());

    ufbx_free_scene(scene);
    return true;
}

} // namespace Dot
