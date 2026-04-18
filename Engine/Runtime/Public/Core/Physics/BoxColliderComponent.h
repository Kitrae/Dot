#pragma once

#include "Core/Core.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Math/Vec3.h"

namespace Dot
{

struct BoxColliderComponent
{
    Vec3 size = {1.0f, 1.0f, 1.0f};
    Vec3 center = {0.0f, 0.0f, 0.0f};
    bool isTrigger = false;
    uint8 collisionLayer = 0;
    uint32 collisionMask = CollisionLayers::kAllLayersMask;
};

} // namespace Dot
