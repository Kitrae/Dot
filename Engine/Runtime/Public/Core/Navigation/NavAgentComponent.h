#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec3.h"

namespace Dot
{

/// NavAgentComponent - default runtime navigation settings for an entity.
/// These values are consumed by the navigation system when scripts call MoveTo
/// without explicit per-request overrides.
struct DOT_CORE_API NavAgentComponent
{
    float moveSpeed = 4.0f;
    float stoppingDistance = 0.1f;
    Vec3 projectionExtent = Vec3(2.0f, 4.0f, 2.0f);
};

} // namespace Dot
