#pragma once

#include "Core/Core.h"

#include <string>

namespace Dot
{

struct SceneSettingsAsset
{
    std::string cubemapPath;
    int wrapMode = 0;
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;
    float rotation = 0.0f;
    bool showMarkers = false;

    bool ambientEnabled = false;
    float ambientColorR = 0.3f;
    float ambientColorG = 0.35f;
    float ambientColorB = 0.4f;
    float ambientIntensity = 0.3f;

    bool sunEnabled = true;
    float sunRotationX = 45.0f;
    float sunRotationY = 30.0f;
    float sunColorR = 1.0f;
    float sunColorG = 0.95f;
    float sunColorB = 0.9f;
    float sunIntensity = 1.0f;
    bool sunCastShadows = true;
    float sunShadowBias = 0.0005f;
    float sunShadowDistance = 100.0f;

    std::string mapPath;
    bool mapVisible = true;
    bool mapCollisionEnabled = true;
};

} // namespace Dot
