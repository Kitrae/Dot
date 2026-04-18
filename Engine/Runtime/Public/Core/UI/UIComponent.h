#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec2.h"

#include <string>

namespace Dot
{

/// World-space UI component attached to scene entities.
struct UIComponent
{
    std::string assetPath;
    std::string uiAssetPath;
    Vec2 drawSize{480.0f, 270.0f};
    float drawWidth = 480.0f;
    float drawHeight = 270.0f;
    float pixelsPerUnit = 100.0f;
    bool billboard = true;
    bool interactionEnabled = true;
    uint32 renderLayer = 0u;
    bool visible = true;
    bool isLoaded = false;
};

} // namespace Dot
