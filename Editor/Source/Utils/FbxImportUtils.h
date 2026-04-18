#pragma once

#include "Core/ECS/Entity.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Dot
{

class World;

void ImportFbxMaterialsForAsset(const std::string& relativeMeshPath);
void CopyFbxSidecarDirectoriesIfPresent(const std::filesystem::path& sourceFbxPath,
                                        const std::filesystem::path& destDir);
size_t GetFbxSubmeshCountForAsset(const std::string& relativeMeshPath);
std::vector<std::string> GetFbxSubmeshMaterialPaths(const std::string& relativeMeshPath);
void AutoAssignImportedMeshMaterial(World& world, Entity entity, const std::vector<std::string>& materialPaths,
                                    int32_t submeshIndex);
void ClearExplicitMeshMaterialOverride(World& world, Entity entity);

} // namespace Dot
