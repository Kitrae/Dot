#pragma once

#include <Core/Map/MapTypes.h>
#include <Core/ECS/Entity.h>
#include <Core/Rendering/ISceneRenderer.h>
#include <Core/Scene/SceneSettingsAsset.h>

#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace Dot
{

class Camera;
struct CameraComponent;
class SimpleRenderer;
struct MaterialData;
struct PrimitiveMesh;
class World;

class RuntimeSceneRenderer final : public ISceneRenderer
{
public:
    RuntimeSceneRenderer();
    ~RuntimeSceneRenderer() override;

    bool Initialize(RHIDevice* device) override;
    void Shutdown() override;
    void Render(const World& world, const SceneRenderView& view, RHISwapChain* swapChain) override;
    void SetShaderRootPath(const std::filesystem::path& path);

    void SetSceneSettings(const SceneSettingsAsset& settings);
    void ClearMap();
    void SetCompiledMap(const MapAsset& asset, const MapCompiledData& compiledData);

private:
    struct MapRenderPart
    {
        std::unique_ptr<PrimitiveMesh> mesh;
        std::unique_ptr<MaterialData> material;
    };

    MaterialData BuildMaterialData(const std::string& materialPath) const;
    void RenderLayer(const World& world, RHISwapChain* swapChain, const Camera& camera, uint32 renderMask);
    void RenderMap(RHISwapChain* swapChain, const Camera& camera);
    void RenderShadows(const World& world, RHISwapChain* swapChain, const Camera& camera, uint32 renderMask);
    bool BuildActiveCamera(const World& world, RHISwapChain* swapChain, Camera& outCamera, CameraComponent& outComponent,
                           Entity& outEntity) const;

    std::unique_ptr<SimpleRenderer> m_Renderer;
    std::filesystem::path m_ShaderRootPath;
    SceneSettingsAsset m_SceneSettings;
    std::vector<MapRenderPart> m_MapParts;
    std::string m_LastMapSignature;
};

} // namespace Dot
