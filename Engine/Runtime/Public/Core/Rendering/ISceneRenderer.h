#pragma once

#include "Core/Core.h"

namespace Dot
{

class World;
class RHIDevice;
class RHISwapChain;

/// Render view data consumed by runtime scene renderers.
struct DOT_CORE_API SceneRenderView
{
    float view[16] = {};
    float projection[16] = {};
    float cameraPosition[3] = {0.0f, 0.0f, 0.0f};
    float _padding = 0.0f;
};

/// Runtime renderer interface. Editor/game frontends provide the concrete renderer.
class DOT_CORE_API ISceneRenderer
{
public:
    virtual ~ISceneRenderer() = default;

    virtual bool Initialize(RHIDevice* device) = 0;
    virtual void Shutdown() = 0;
    virtual void Render(const World& world, const SceneRenderView& view, RHISwapChain* swapChain) = 0;
};

} // namespace Dot
