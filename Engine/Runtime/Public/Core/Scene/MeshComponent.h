// =============================================================================
// Dot Engine - Mesh Component
// =============================================================================
// Component for referencing external mesh assets (.obj, .fbx, .gltf).
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

/// Mesh component - references an external mesh file
struct MeshComponent
{
    std::string meshPath;      // Relative path to mesh file (e.g., "Models/cube.obj")
    int32_t submeshIndex = -1; // -1 = render all submeshes, >= 0 = specific submesh only
    bool isLoaded = false;     // Whether the mesh has been successfully loaded
    bool castShadow = true;    // Whether this mesh casts shadows (disable for large static geometry)

    MeshComponent() = default;
    explicit MeshComponent(const std::string& path, int32_t subIdx = -1) : meshPath(path), submeshIndex(subIdx) {}
};

} // namespace Dot
