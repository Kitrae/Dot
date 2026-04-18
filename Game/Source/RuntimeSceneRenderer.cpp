#include "RuntimeSceneRenderer.h"

#include <Core/Assets/AssetManager.h>
#include <Core/ECS/World.h>
#include <Core/Material/MaterialLoader.h>
#include <Core/Rendering/ViewSettings.h>
#include <Core/Scene/CameraComponent.h>
#include <Core/Scene/Components.h>
#include <Core/Scene/LightComponent.h>
#include <Core/Scene/MaterialComponent.h>
#include <Core/Scene/MeshComponent.h>

#include "../../Engine/Runtime/Shared/Rendering/Camera.h"
#include "../../Engine/Runtime/Shared/Rendering/PrimitiveMeshes.h"
#include "../../Engine/Runtime/Shared/Rendering/SimpleRenderer.h"

#include <cmath>
#include <unordered_map>

namespace Dot
{

namespace
{

bool MatchesRenderMask(const World& world, Entity entity, uint32 requiredMask)
{
    if (const auto* layer = const_cast<World&>(world).GetComponent<RenderLayerComponent>(entity))
        return (layer->mask & requiredMask) != 0;
    return (RenderLayerMask::World & requiredMask) != 0;
}

bool IsEntityActive(const World& world, Entity entity)
{
    if (const auto* active = const_cast<World&>(world).GetComponent<ActiveComponent>(entity))
        return active->active;
    return true;
}

Vec3 DirectionFromEuler(float pitchDeg, float yawDeg)
{
    const float degToRad = 0.0174532925f;
    const float pitch = pitchDeg * degToRad;
    const float yaw = yawDeg * degToRad;
    return Vec3(std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

MaterialData BuildInlineMaterial(const MaterialComponent& component)
{
    MaterialData material;
    material.colorR = component.baseColor.x;
    material.colorG = component.baseColor.y;
    material.colorB = component.baseColor.z;
    material.metallic = component.metallic;
    material.roughness = component.roughness;
    material.emissiveColorR = component.emissiveColor.x;
    material.emissiveColorG = component.emissiveColor.y;
    material.emissiveColorB = component.emissiveColor.z;
    material.emissiveStrength = component.emissiveStrength;
    return material;
}

} // namespace

RuntimeSceneRenderer::RuntimeSceneRenderer() = default;

RuntimeSceneRenderer::~RuntimeSceneRenderer()
{
    Shutdown();
}

bool RuntimeSceneRenderer::Initialize(RHIDevice* device)
{
    m_Renderer = std::make_unique<SimpleRenderer>();
    if (!m_ShaderRootPath.empty())
        m_Renderer->SetShaderRootPath(m_ShaderRootPath.string(), false);
    return m_Renderer->Initialize(device);
}

void RuntimeSceneRenderer::Shutdown()
{
    if (m_Renderer)
    {
        m_Renderer->Shutdown();
        m_Renderer.reset();
    }
    m_MapParts.clear();
}

void RuntimeSceneRenderer::SetSceneSettings(const SceneSettingsAsset& settings)
{
    m_SceneSettings = settings;
}

void RuntimeSceneRenderer::SetShaderRootPath(const std::filesystem::path& path)
{
    m_ShaderRootPath = path;
}

void RuntimeSceneRenderer::ClearMap()
{
    m_MapParts.clear();
    m_LastMapSignature.clear();
}

MaterialData RuntimeSceneRenderer::BuildMaterialData(const std::string& materialPath) const
{
    MaterialData material;
    if (materialPath.empty())
        return material;

    const LoadedMaterial loaded = MaterialLoader::Load(AssetManager::Get().GetFullPath(materialPath));
    if (!loaded.valid)
        return material;

    material.colorR = loaded.baseColor.x;
    material.colorG = loaded.baseColor.y;
    material.colorB = loaded.baseColor.z;
    material.metallic = loaded.metallic;
    material.roughness = loaded.roughness;
    material.tilingU = loaded.tilingU;
    material.tilingV = loaded.tilingV;
    material.offsetU = loaded.offsetU;
    material.offsetV = loaded.offsetV;
    material.filterMode = loaded.filterMode;
    material.wrapMode = loaded.wrapMode;
    material.pannerSpeedU = loaded.pannerSpeedU;
    material.pannerSpeedV = loaded.pannerSpeedV;
    material.pannerMethod = loaded.pannerMethod;
    material.pannerLink = loaded.pannerLink;
    for (int i = 0; i < 4; ++i)
    {
        material.texturePaths[i] = loaded.texturePaths[i];
        if (loaded.hasTextures[i])
            material.hasTexture = 1.0f;
    }
    return material;
}

void RuntimeSceneRenderer::SetCompiledMap(const MapAsset& asset, const MapCompiledData& compiledData)
{
    ClearMap();
    if (!m_Renderer || compiledData.vertices.empty() || compiledData.indices.empty())
        return;

    struct PendingBatch
    {
        MeshData meshData;
        std::unique_ptr<MaterialData> material;
    };

    std::unordered_map<std::string, PendingBatch> batches;
    batches.reserve(compiledData.submeshes.size());

    auto resolveMaterialPath = [&asset](const MapCompiledSubmesh& submesh) -> std::string
    {
        const MapBrush* brush = asset.FindBrush(submesh.brushId);
        const MapFace* face = (brush && submesh.faceIndex < brush->faces.size()) ? &brush->faces[submesh.faceIndex] : nullptr;
        return face ? face->materialPath : submesh.materialPath;
    };

    for (const MapCompiledSubmesh& submesh : compiledData.submeshes)
    {
        if (submesh.indexCount == 0)
            continue;

        const std::string materialPath = resolveMaterialPath(submesh);
        PendingBatch& batch = batches[materialPath];
        if (!batch.material)
            batch.material = std::make_unique<MaterialData>(BuildMaterialData(materialPath));

        const uint32 baseVertex = static_cast<uint32>(batch.meshData.vertices.size());
        for (uint32 localIndex = 0; localIndex < submesh.indexCount; ++localIndex)
        {
            const uint32 sourceIndex = compiledData.indices[submesh.indexStart + localIndex];
            const MapCompiledVertex& sourceVertex = compiledData.vertices[sourceIndex];

            PrimitiveVertex vertex{};
            vertex.x = sourceVertex.position.x;
            vertex.y = sourceVertex.position.y;
            vertex.z = sourceVertex.position.z;
            vertex.nx = sourceVertex.normal.x;
            vertex.ny = sourceVertex.normal.y;
            vertex.nz = sourceVertex.normal.z;
            vertex.r = 1.0f;
            vertex.g = 1.0f;
            vertex.b = 1.0f;
            vertex.a = 1.0f;
            vertex.u = sourceVertex.u;
            vertex.v = sourceVertex.v;
            vertex.u2 = sourceVertex.u2;
            vertex.v2 = sourceVertex.v2;
            batch.meshData.vertices.push_back(vertex);
            batch.meshData.indices.push_back(baseVertex + localIndex);

            if (batch.meshData.vertices.size() == 1)
            {
                batch.meshData.boundsMinX = batch.meshData.boundsMaxX = vertex.x;
                batch.meshData.boundsMinY = batch.meshData.boundsMaxY = vertex.y;
                batch.meshData.boundsMinZ = batch.meshData.boundsMaxZ = vertex.z;
            }
            else
            {
                batch.meshData.boundsMinX = std::min(batch.meshData.boundsMinX, vertex.x);
                batch.meshData.boundsMinY = std::min(batch.meshData.boundsMinY, vertex.y);
                batch.meshData.boundsMinZ = std::min(batch.meshData.boundsMinZ, vertex.z);
                batch.meshData.boundsMaxX = std::max(batch.meshData.boundsMaxX, vertex.x);
                batch.meshData.boundsMaxY = std::max(batch.meshData.boundsMaxY, vertex.y);
                batch.meshData.boundsMaxZ = std::max(batch.meshData.boundsMaxZ, vertex.z);
            }
        }
    }

    for (auto& [materialPath, batch] : batches)
    {
        MapRenderPart part;
        part.mesh = m_Renderer->CreateRuntimeMesh(batch.meshData);
        if (!part.mesh)
            continue;
        part.material = std::move(batch.material);
        m_MapParts.push_back(std::move(part));
    }
}

bool RuntimeSceneRenderer::BuildActiveCamera(const World& world, RHISwapChain* swapChain, Camera& outCamera,
                                             CameraComponent& outComponent, Entity& outEntity) const
{
    bool found = false;
    const_cast<World&>(world).Each<TransformComponent, CameraComponent>(
        [&](Entity entity, TransformComponent& transform, CameraComponent& camera)
        {
            if (found || !camera.isActive || !IsEntityActive(world, entity))
                return;

            const Vec3 eye = transform.worldMatrix.GetTranslation();
            const Vec3 forward = DirectionFromEuler(transform.rotation.x, transform.rotation.y);
            outCamera.SetPosition(eye.x, eye.y, eye.z);
            outCamera.LookAt(eye.x + forward.x, eye.y + forward.y, eye.z + forward.z);
            outCamera.SetPerspective(camera.fov,
                                     static_cast<float>(swapChain->GetWidth()) / static_cast<float>(swapChain->GetHeight()),
                                     camera.nearPlane, camera.farPlane);
            outComponent = camera;
            outEntity = entity;
            found = true;
        });
    return found;
}

void RuntimeSceneRenderer::RenderShadows(const World& world, RHISwapChain* swapChain, const Camera& camera, uint32 renderMask)
{
    std::vector<std::pair<const float*, const PrimitiveMesh*>> shadowCasters;
    const Mat4 mapWorld = Mat4::Identity();
    std::unordered_map<std::string, std::vector<PrimitiveMesh*>> meshCache;

    auto getMeshes = [this, &meshCache](const std::string& meshPath) -> const std::vector<PrimitiveMesh*>&
    {
        auto it = meshCache.find(meshPath);
        if (it != meshCache.end())
            return it->second;

        auto loaded = m_Renderer->LoadMesh(meshPath);
        auto inserted = meshCache.emplace(meshPath, std::move(loaded));
        return inserted.first->second;
    };

    const_cast<World&>(world).Each<TransformComponent, PrimitiveComponent>(
        [&](Entity entity, TransformComponent& transform, PrimitiveComponent& primitive)
        {
            if (!IsEntityActive(world, entity) || !MatchesRenderMask(world, entity, renderMask))
                return;

            PrimitiveMesh* mesh = m_Renderer->GetPrimitiveMesh(primitive.type);
            if (mesh && mesh->indexCount > 0)
                shadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
        });

    const_cast<World&>(world).Each<TransformComponent, MeshComponent>(
        [&](Entity entity, TransformComponent& transform, MeshComponent& meshComponent)
        {
            if (!IsEntityActive(world, entity) || !MatchesRenderMask(world, entity, renderMask) || !meshComponent.castShadow ||
                meshComponent.meshPath.empty())
            {
                return;
            }

            const auto& meshes = getMeshes(meshComponent.meshPath);
            size_t startIndex = 0;
            size_t endIndex = meshes.size();
            if (meshComponent.submeshIndex >= 0 && static_cast<size_t>(meshComponent.submeshIndex) < meshes.size())
            {
                startIndex = static_cast<size_t>(meshComponent.submeshIndex);
                endIndex = startIndex + 1;
            }

            for (size_t i = startIndex; i < endIndex; ++i)
            {
                PrimitiveMesh* mesh = meshes[i];
                if (mesh && mesh->indexCount > 0)
                    shadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
            }
        });

    for (const MapRenderPart& part : m_MapParts)
    {
        if (part.mesh && part.mesh->indexCount > 0)
            shadowCasters.emplace_back(mapWorld.Data(), part.mesh.get());
    }

    if (!shadowCasters.empty() && ViewSettings::Get().shadowsEnabled)
    {
        m_Renderer->RenderShadowMap(camera, swapChain, shadowCasters);
        m_Renderer->RenderLocalLightShadowMaps(swapChain, shadowCasters);
    }
}

void RuntimeSceneRenderer::RenderLayer(const World& world, RHISwapChain* swapChain, const Camera& camera, uint32 renderMask)
{
    std::unordered_map<std::string, std::vector<PrimitiveMesh*>> meshCache;
    auto getMeshes = [this, &meshCache](const std::string& meshPath) -> const std::vector<PrimitiveMesh*>&
    {
        auto it = meshCache.find(meshPath);
        if (it != meshCache.end())
            return it->second;

        auto loaded = m_Renderer->LoadMesh(meshPath);
        auto inserted = meshCache.emplace(meshPath, std::move(loaded));
        return inserted.first->second;
    };

    const MaterialData defaultMaterial = {};

    const_cast<World&>(world).Each<TransformComponent, PrimitiveComponent>(
        [&](Entity entity, TransformComponent& transform, PrimitiveComponent& primitive)
        {
            if (!IsEntityActive(world, entity) || !MatchesRenderMask(world, entity, renderMask))
                return;

            PrimitiveMesh* mesh = m_Renderer->GetPrimitiveMesh(primitive.type);
            if (!mesh || mesh->indexCount == 0)
                return;

            MaterialData material = defaultMaterial;
            if (const MaterialComponent* component = const_cast<World&>(world).GetComponent<MaterialComponent>(entity))
            {
                material = component->useMaterialFile && !component->materialPath.empty()
                               ? BuildMaterialData(component->materialPath)
                               : BuildInlineMaterial(*component);
            }

            m_Renderer->SetMaterialData(material);
            m_Renderer->RenderMesh(camera, swapChain, transform.worldMatrix.Data(), *mesh);
        });

    const_cast<World&>(world).Each<TransformComponent, MeshComponent>(
        [&](Entity entity, TransformComponent& transform, MeshComponent& meshComponent)
        {
            if (!IsEntityActive(world, entity) || !MatchesRenderMask(world, entity, renderMask) || meshComponent.meshPath.empty())
                return;

            if (const MaterialComponent* component = const_cast<World&>(world).GetComponent<MaterialComponent>(entity))
            {
                const MaterialData material = component->useMaterialFile && !component->materialPath.empty()
                                                  ? BuildMaterialData(component->materialPath)
                                                  : BuildInlineMaterial(*component);
                m_Renderer->SetMaterialData(material);
            }
            else
            {
                m_Renderer->SetMaterialData(defaultMaterial);
            }

            const auto& meshes = getMeshes(meshComponent.meshPath);
            size_t startIndex = 0;
            size_t endIndex = meshes.size();
            if (meshComponent.submeshIndex >= 0 && static_cast<size_t>(meshComponent.submeshIndex) < meshes.size())
            {
                startIndex = static_cast<size_t>(meshComponent.submeshIndex);
                endIndex = startIndex + 1;
            }

            for (size_t i = startIndex; i < endIndex; ++i)
            {
                PrimitiveMesh* mesh = meshes[i];
                if (mesh && mesh->indexCount > 0)
                    m_Renderer->RenderMesh(camera, swapChain, transform.worldMatrix.Data(), *mesh);
            }
        });
}

void RuntimeSceneRenderer::RenderMap(RHISwapChain* swapChain, const Camera& camera)
{
    if (!m_SceneSettings.mapVisible)
        return;

    const Mat4 mapWorld = Mat4::Identity();
    for (const MapRenderPart& part : m_MapParts)
    {
        if (!part.mesh || !part.material)
            continue;
        m_Renderer->SetMaterialData(*part.material);
        m_Renderer->RenderMesh(camera, swapChain, mapWorld.Data(), *part.mesh);
    }
}

void RuntimeSceneRenderer::Render(const World& world, const SceneRenderView&, RHISwapChain* swapChain)
{
    if (!m_Renderer || !swapChain)
        return;

    Camera sceneCamera;
    CameraComponent activeCameraComponent;
    Entity activeCameraEntity = kNullEntity;
    if (!BuildActiveCamera(world, swapChain, sceneCamera, activeCameraComponent, activeCameraEntity))
        return;

    SceneLightData lightData = {};
    bool foundDirectional = false;
    bool foundAmbient = false;
    int pointCount = 0;
    int spotCount = 0;

    const_cast<World&>(world).Each<TransformComponent>(
        [&](Entity entity, TransformComponent& transform)
        {
            if (!IsEntityActive(world, entity))
                return;

            if (const auto* directional = const_cast<World&>(world).GetComponent<DirectionalLightComponent>(entity))
            {
                if (!foundDirectional)
                {
                    const Vec3 forward = DirectionFromEuler(transform.rotation.x, transform.rotation.y);
                    lightData.lightDirX = forward.x;
                    lightData.lightDirY = forward.y;
                    lightData.lightDirZ = forward.z;
                    lightData.lightColorR = directional->color.x;
                    lightData.lightColorG = directional->color.y;
                    lightData.lightColorB = directional->color.z;
                    lightData.lightIntensity = directional->intensity;
                    lightData.shadowEnabled = directional->castShadows;
                    lightData.shadowBias = directional->shadowBias;
                    lightData.shadowDistance = directional->shadowDistance;
                    foundDirectional = true;
                }
            }

            if (const auto* ambient = const_cast<World&>(world).GetComponent<AmbientLightComponent>(entity))
            {
                if (!foundAmbient)
                {
                    lightData.ambientColorR = ambient->color.x;
                    lightData.ambientColorG = ambient->color.y;
                    lightData.ambientColorB = ambient->color.z;
                    lightData.ambientIntensity = ambient->intensity;
                    foundAmbient = true;
                }
            }

            if (const auto* point = const_cast<World&>(world).GetComponent<PointLightComponent>(entity))
            {
                if (pointCount < SceneLightData::MAX_POINT_LIGHTS)
                {
                    const Vec3 position = transform.worldMatrix.GetTranslation();
                    auto& dst = lightData.pointLights[pointCount++];
                    dst.posX = position.x;
                    dst.posY = position.y;
                    dst.posZ = position.z;
                    dst.range = point->range;
                    dst.colorR = point->color.x;
                    dst.colorG = point->color.y;
                    dst.colorB = point->color.z;
                    dst.intensity = point->intensity;
                    dst.shadowEnabled = point->castShadows ? 1.0f : 0.0f;
                    dst.shadowBias = point->shadowBias;
                }
            }

            if (const auto* spot = const_cast<World&>(world).GetComponent<SpotLightComponent>(entity))
            {
                if (spotCount < SceneLightData::MAX_SPOT_LIGHTS)
                {
                    const Vec3 position = transform.worldMatrix.GetTranslation();
                    const Vec3 forward = DirectionFromEuler(transform.rotation.x, transform.rotation.y);
                    auto& dst = lightData.spotLights[spotCount++];
                    dst.posX = position.x;
                    dst.posY = position.y;
                    dst.posZ = position.z;
                    dst.range = spot->range;
                    dst.dirX = forward.x;
                    dst.dirY = forward.y;
                    dst.dirZ = forward.z;
                    dst.innerCos = std::cos(spot->innerConeAngle * 0.0174532925f);
                    dst.outerCos = std::cos(spot->outerConeAngle * 0.0174532925f);
                    dst.colorR = spot->color.x;
                    dst.colorG = spot->color.y;
                    dst.colorB = spot->color.z;
                    dst.intensity = spot->intensity;
                    dst.shadowEnabled = spot->castShadows ? 1.0f : 0.0f;
                    dst.shadowBias = spot->shadowBias;
                }
            }
        });

    lightData.numPointLights = pointCount;
    lightData.numSpotLights = spotCount;

    if (!foundAmbient && m_SceneSettings.ambientEnabled)
    {
        lightData.ambientColorR = m_SceneSettings.ambientColorR;
        lightData.ambientColorG = m_SceneSettings.ambientColorG;
        lightData.ambientColorB = m_SceneSettings.ambientColorB;
        lightData.ambientIntensity = m_SceneSettings.ambientIntensity;
    }

    if (!foundDirectional && m_SceneSettings.sunEnabled)
    {
        const Vec3 forward = DirectionFromEuler(m_SceneSettings.sunRotationX, m_SceneSettings.sunRotationY);
        lightData.lightDirX = forward.x;
        lightData.lightDirY = forward.y;
        lightData.lightDirZ = forward.z;
        lightData.lightColorR = m_SceneSettings.sunColorR;
        lightData.lightColorG = m_SceneSettings.sunColorG;
        lightData.lightColorB = m_SceneSettings.sunColorB;
        lightData.lightIntensity = m_SceneSettings.sunIntensity;
        lightData.shadowEnabled = m_SceneSettings.sunCastShadows;
        lightData.shadowBias = m_SceneSettings.sunShadowBias;
        lightData.shadowDistance = m_SceneSettings.sunShadowDistance;
    }

    m_Renderer->BeginFrame();
    m_Renderer->SetLightData(lightData);

    if (!m_SceneSettings.cubemapPath.empty())
    {
        m_Renderer->RenderSkybox(sceneCamera, swapChain, m_SceneSettings.cubemapPath, m_SceneSettings.wrapMode,
                                 m_SceneSettings.rotation, m_SceneSettings.showMarkers, m_SceneSettings.tintR * 0.5f,
                                 m_SceneSettings.tintG * 0.7f, m_SceneSettings.tintB, m_SceneSettings.tintR * 0.8f,
                                 m_SceneSettings.tintG * 0.8f, m_SceneSettings.tintB * 0.85f);
    }

    RenderShadows(world, swapChain, sceneCamera, activeCameraComponent.renderMask);

    const bool useFxaa = ViewSettings::Get().antiAliasingEnabled;
    if (useFxaa)
        m_Renderer->BeginFXAAPass(swapChain);

    RenderLayer(world, swapChain, sceneCamera, activeCameraComponent.renderMask);
    RenderMap(swapChain, sceneCamera);

    if (activeCameraComponent.enableViewmodelPass)
    {
        Camera viewmodelCamera = sceneCamera;
        viewmodelCamera.SetPerspective(activeCameraComponent.viewmodelFov,
                                       static_cast<float>(swapChain->GetWidth()) / static_cast<float>(swapChain->GetHeight()),
                                       activeCameraComponent.viewmodelNearPlane, activeCameraComponent.farPlane);
        m_Renderer->ClearSceneDepth(swapChain);
        RenderLayer(world, swapChain, viewmodelCamera, activeCameraComponent.viewmodelMask);
    }

    if (useFxaa)
        m_Renderer->EndFXAAPass(swapChain);
}

} // namespace Dot
