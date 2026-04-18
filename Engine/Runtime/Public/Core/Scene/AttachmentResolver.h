// =============================================================================
// Dot Engine - Attachment Resolver
// =============================================================================
// Resolves hierarchy and attachment-based world transforms for a scene world.
// =============================================================================

#pragma once

#include "Core/ECS/World.h"

#include <string>

namespace Dot
{

Entity FindActiveCameraEntity(World& world);
Entity FindAttachmentSocket(World& world, Entity root, const std::string& socketName);
void ResolveSceneTransforms(World& world, Entity activeCameraEntity = kNullEntity);

} // namespace Dot
