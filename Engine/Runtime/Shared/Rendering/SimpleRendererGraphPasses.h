// =============================================================================
// Dot Engine - Simple Renderer Graph Pass Executors
// =============================================================================

#pragma once

#include <Core/Math/Mat4.h>
#include <Core/Math/Vec3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Dot
{

class Camera;
struct MaterialData;
struct PrimitiveMesh;
class RHISwapChain;
class SimpleRenderer;

struct SSAOGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer, RHISwapChain* swapChain, const Camera& camera);
};

struct SSAOBlurGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer, RHISwapChain* swapChain);
};

struct HZBGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer, RHISwapChain* swapChain);
};

struct SSAODebugCompositeExecutor
{
    static void Execute(SimpleRenderer& renderer, RHISwapChain* swapChain);
};

struct DirectionalShadowGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer, const Camera& camera,
                        const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters);
};

struct LocalShadowGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer,
                        const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters);
};

struct RenderGraphReflectionProbe
{
    std::string cubemapPath;
    Vec3 position = Vec3::Zero();
    Vec3 tint = Vec3(1.0f, 1.0f, 1.0f);
    Vec3 boxExtents = Vec3::Zero();
    float radius = 0.0f;
    float intensity = 1.0f;
    float falloff = 0.25f;
    float rotation = 0.0f;
    float blendWeight = 0.0f;
};

struct RenderGraphQueuedDraw
{
    Mat4 worldMatrix = Mat4::Identity();
    const PrimitiveMesh* mesh = nullptr;
    std::size_t materialIndex = 0;
    RenderGraphReflectionProbe reflectionProbes[2];
    uint32_t reflectionProbeCount = 0;
    bool hasReflectionProbe = false;
    bool overrideLodThresholds = false;
    float lod1ScreenHeight = 0.0f;
    float lod2ScreenHeight = 0.0f;
};

struct RenderGraphMapPreviewDraw
{
    Mat4 worldMatrix = Mat4::Identity();
    const PrimitiveMesh* mesh = nullptr;
    const MaterialData* material = nullptr;
    RenderGraphReflectionProbe reflectionProbes[2];
    uint32_t reflectionProbeCount = 0;
    bool hasReflectionProbe = false;
};

struct MainSceneGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer, RHISwapChain* swapChain, const Camera& camera,
                        const std::vector<RenderGraphQueuedDraw>& drawQueue,
                        const std::vector<RenderGraphMapPreviewDraw>& mapPreviewDraws,
                        const std::vector<MaterialData>& resolvedMaterials, bool antiAliasingEnabled,
                        bool deferFXAAResolve, int* renderCallCounter = nullptr);
};

struct ViewmodelGraphPassExecutor
{
    static void Execute(SimpleRenderer& renderer, RHISwapChain* swapChain, const Camera& viewmodelCamera,
                        const std::vector<RenderGraphQueuedDraw>& drawQueue,
                        const std::vector<MaterialData>& resolvedMaterials, bool antiAliasingEnabled,
                        int* renderCallCounter = nullptr);
};

} // namespace Dot
