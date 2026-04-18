// =============================================================================
// Dot Engine - Viewport Panel Implementation
// =============================================================================

#include "ViewportPanel.h"
#include "PanelChrome.h"
#include "MapViewportToolController.h"
#include "ViewportOverlayRenderer.h"
#include "ViewportInteractionController.h"
#include "ViewportSelectionUtils.h"

#include "Core/Assets/AssetManager.h"
#include "Core/Material/MaterialGraph.h"
#include "Core/Material/MaterialLoader.h"
#include "Core/Navigation/NavigationSystem.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/PhysicsSystem.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Scene/AttachmentResolver.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/MaterialComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/PointLightShadowUtils.h"

#include "../Commands/CommandRegistry.h"
#include "../Commands/CreateEntityCommands.h"
#include "../Commands/MapCommands.h"
#include "../Map/MapDocument.h"
#include "../Rendering/FbxLoader.h"
#include "../Rendering/FrustumCulling.h"
#include "../Rendering/ObjLoader.h"
#include "../Rendering/SimpleRendererGraphPasses.h"
#include "../Scene/EditorSceneContext.h"
#include "../Settings/EditorSettings.h"
#include "../Settings/ViewSettings.h"
#include "DebugPanel.h"
#include "Utils/FbxImportUtils.h"

#include <Core/Physics/PhysicsSettings.h>
#include <Core/Scene/MeshComponent.h>
#include <Core/Scene/SkyboxComponent.h>
#include <wincodec.h>
#include <objbase.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <imgui.h>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12Texture.h"

namespace Dot
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr uint32_t kReflectionProbeBakeResolution = 256;
constexpr size_t kReflectionProbeFaceCount = 6;
constexpr const char* kAutoReflectionProbeNamePrefix = "Auto Reflection Probe";
constexpr float kAutoReflectionProbeGridSize = 24.0f;
constexpr float kAutoReflectionProbePadding = 1.5f;
constexpr float kAutoReflectionProbeMinHalfExtent = 4.0f;

struct AutoReflectionProbeCellKey
{
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const AutoReflectionProbeCellKey& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct AutoReflectionProbeCellKeyHash
{
    size_t operator()(const AutoReflectionProbeCellKey& key) const
    {
        const size_t hx = static_cast<size_t>(std::hash<int>{}(key.x));
        const size_t hy = static_cast<size_t>(std::hash<int>{}(key.y));
        const size_t hz = static_cast<size_t>(std::hash<int>{}(key.z));
        return hx ^ (hy << 1) ^ (hz << 2);
    }
};

struct AutoReflectionProbeCluster
{
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
    uint32_t sourceCount = 0;
    bool initialized = false;
};

struct ResolvedSkyboxSettings
{
    bool hasEntityOverride = false;
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

    bool HasVisibleSkybox() const { return showMarkers || !cubemapPath.empty(); }
};

ResolvedSkyboxSettings ResolveSkyboxSettings(const World* world, const SceneSettingsAsset* fallback)
{
    ResolvedSkyboxSettings resolved;
    if (fallback)
    {
        resolved.cubemapPath = fallback->cubemapPath;
        resolved.wrapMode = fallback->wrapMode;
        resolved.tintR = fallback->tintR;
        resolved.tintG = fallback->tintG;
        resolved.tintB = fallback->tintB;
        resolved.rotation = fallback->rotation;
        resolved.showMarkers = fallback->showMarkers;
        resolved.ambientEnabled = fallback->ambientEnabled;
        resolved.ambientColorR = fallback->ambientColorR;
        resolved.ambientColorG = fallback->ambientColorG;
        resolved.ambientColorB = fallback->ambientColorB;
        resolved.ambientIntensity = fallback->ambientIntensity;
        resolved.sunEnabled = fallback->sunEnabled;
        resolved.sunRotationX = fallback->sunRotationX;
        resolved.sunRotationY = fallback->sunRotationY;
        resolved.sunColorR = fallback->sunColorR;
        resolved.sunColorG = fallback->sunColorG;
        resolved.sunColorB = fallback->sunColorB;
        resolved.sunIntensity = fallback->sunIntensity;
        resolved.sunCastShadows = fallback->sunCastShadows;
        resolved.sunShadowBias = fallback->sunShadowBias;
        resolved.sunShadowDistance = fallback->sunShadowDistance;
    }

    if (!world)
        return resolved;

    bool foundSkyboxEntity = false;
    const_cast<World*>(world)->EachEntity(
        [&](Entity entity)
        {
            if (foundSkyboxEntity)
                return;
            const SkyboxComponent* skybox = const_cast<World*>(world)->GetComponent<SkyboxComponent>(entity);
            if (!skybox)
                return;

            resolved.hasEntityOverride = true;
            resolved.cubemapPath = skybox->cubemapPath;
            resolved.wrapMode = static_cast<int>(skybox->wrapMode);
            resolved.tintR = skybox->tintR;
            resolved.tintG = skybox->tintG;
            resolved.tintB = skybox->tintB;
            resolved.rotation = skybox->rotation;
            resolved.showMarkers = skybox->showMarkers;
            resolved.ambientEnabled = skybox->ambientEnabled;
            resolved.ambientColorR = skybox->ambientColorR;
            resolved.ambientColorG = skybox->ambientColorG;
            resolved.ambientColorB = skybox->ambientColorB;
            resolved.ambientIntensity = skybox->ambientIntensity;
            resolved.sunEnabled = skybox->sunEnabled;
            resolved.sunRotationX = skybox->sunRotationX;
            resolved.sunRotationY = skybox->sunRotationY;
            resolved.sunColorR = skybox->sunColorR;
            resolved.sunColorG = skybox->sunColorG;
            resolved.sunColorB = skybox->sunColorB;
            resolved.sunIntensity = skybox->sunIntensity;
            resolved.sunCastShadows = skybox->sunCastShadows;
            resolved.sunShadowBias = skybox->sunShadowBias;
            resolved.sunShadowDistance = skybox->sunShadowDistance;
            foundSkyboxEntity = true;
        });

    return resolved;
}

uint64_t AlignUpUint64(uint64_t value, uint64_t alignment)
{
    if (alignment == 0)
        return value;
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

bool IsAutoReflectionProbeName(const std::string& name)
{
    return name.rfind(kAutoReflectionProbeNamePrefix, 0) == 0;
}

void ExpandAutoReflectionProbeCluster(AutoReflectionProbeCluster& cluster, float minX, float minY, float minZ, float maxX,
                                      float maxY, float maxZ)
{
    if (!cluster.initialized)
    {
        cluster.minX = minX;
        cluster.minY = minY;
        cluster.minZ = minZ;
        cluster.maxX = maxX;
        cluster.maxY = maxY;
        cluster.maxZ = maxZ;
        cluster.initialized = true;
        cluster.sourceCount = 1;
        return;
    }

    cluster.minX = (std::min)(cluster.minX, minX);
    cluster.minY = (std::min)(cluster.minY, minY);
    cluster.minZ = (std::min)(cluster.minZ, minZ);
    cluster.maxX = (std::max)(cluster.maxX, maxX);
    cluster.maxY = (std::max)(cluster.maxY, maxY);
    cluster.maxZ = (std::max)(cluster.maxZ, maxZ);
    ++cluster.sourceCount;
}

std::string SanitizeReflectionProbeCaptureName(const std::string& rawName)
{
    std::string sanitized;
    sanitized.reserve(rawName.size());
    for (char ch : rawName)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch))
        {
            sanitized.push_back(static_cast<char>(std::tolower(uch)));
        }
        else if (ch == '_' || ch == '-')
        {
            sanitized.push_back(ch);
        }
        else if (std::isspace(uch) || ch == '.')
        {
            sanitized.push_back('_');
        }
    }

    while (!sanitized.empty() && sanitized.front() == '_')
        sanitized.erase(sanitized.begin());
    while (!sanitized.empty() && sanitized.back() == '_')
        sanitized.pop_back();

    if (sanitized.empty())
        sanitized = "probe";
    return sanitized;
}

std::string BuildReflectionProbeBakeFolderName(World& world, Entity entity)
{
    std::string baseName = "probe";
    if (NameComponent* name = world.GetComponent<NameComponent>(entity))
    {
        if (!name->name.empty())
            baseName = SanitizeReflectionProbeCaptureName(name->name);
    }

    return baseName + "_" + std::to_string(entity.id);
}

void GetReflectionProbeFaceBasis(size_t faceIndex, Vec3& direction, Vec3& up)
{
    switch (faceIndex)
    {
        case 0:
            direction = Vec3::UnitX();
            up = Vec3::UnitY();
            break;
        case 1:
            direction = -Vec3::UnitX();
            up = Vec3::UnitY();
            break;
        case 2:
            direction = Vec3::UnitY();
            up = -Vec3::UnitZ();
            break;
        case 3:
            direction = -Vec3::UnitY();
            up = Vec3::UnitZ();
            break;
        case 4:
            direction = Vec3::UnitZ();
            up = Vec3::UnitY();
            break;
        case 5:
        default:
            direction = -Vec3::UnitZ();
            up = Vec3::UnitY();
            break;
    }
}

Vec3 RotateVectorYDegrees(const Vec3& value, float angleDegrees)
{
    const float radians = angleDegrees * (3.14159265f / 180.0f);
    const float s = std::sin(radians);
    const float c = std::cos(radians);
    return Vec3(value.x * c - value.z * s, value.y, value.x * s + value.z * c);
}

Vec3 ResolveReflectionProbeBoxExtents(const Vec3& boxExtents, float radius)
{
    if (boxExtents.x > 0.001f && boxExtents.y > 0.001f && boxExtents.z > 0.001f)
        return boxExtents;

    const float safeRadius = std::max(radius, 0.001f);
    return Vec3(safeRadius, safeRadius, safeRadius);
}

const char* GetReflectionProbeFaceFileName(size_t faceIndex)
{
    static constexpr const char* kFaceFiles[kReflectionProbeFaceCount] = {"px.png", "nx.png", "py.png",
                                                                          "ny.png", "pz.png", "nz.png"};
    return faceIndex < kReflectionProbeFaceCount ? kFaceFiles[faceIndex] : "px.png";
}

bool WriteRgbaPngFile(const std::filesystem::path& path, const uint8_t* rgbaData, uint32_t width, uint32_t height,
                      uint32_t rowStride)
{
    if (!rgbaData || width == 0 || height == 0)
        return false;

    HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
        return false;

    bool success = false;
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory)
        {
            if (shouldUninitialize)
                CoUninitialize();
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            if (shouldUninitialize)
                CoUninitialize();
            return false;
        }

        std::vector<uint8_t> bgraData(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t* srcRow = rgbaData + static_cast<size_t>(y) * rowStride;
            uint8_t* dstRow = bgraData.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
            for (uint32_t x = 0; x < width; ++x)
            {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }

        ComPtr<IWICStream> stream;
        hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr) && stream)
            hr = stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);

        ComPtr<IWICBitmapEncoder> encoder;
        if (SUCCEEDED(hr))
            hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (SUCCEEDED(hr) && encoder)
            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        if (SUCCEEDED(hr))
            hr = encoder->CreateNewFrame(&frame, &properties);
        if (SUCCEEDED(hr) && frame)
            hr = frame->Initialize(properties.Get());
        if (SUCCEEDED(hr))
            hr = frame->SetSize(width, height);

        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr))
            hr = frame->SetPixelFormat(&pixelFormat);
        if (SUCCEEDED(hr) && pixelFormat == GUID_WICPixelFormat32bppBGRA)
            hr = frame->WritePixels(height, width * 4u, static_cast<UINT>(bgraData.size()), bgraData.data());
        if (SUCCEEDED(hr))
            hr = frame->Commit();
        if (SUCCEEDED(hr))
            hr = encoder->Commit();

        success = SUCCEEDED(hr);
    }

    if (shouldUninitialize)
        CoUninitialize();
    return success;
}

struct PhysicsContactDebugPoint
{
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
};

Vec3 GetWorldPosition(const TransformComponent& transform)
{
    return transform.worldMatrix.GetTranslation();
}

Vec3 GetWorldScale(const TransformComponent& transform)
{
    return transform.worldMatrix.GetScale();
}

uint64_t HashCombine64(uint64_t seed, uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

std::filesystem::path FindEditorShaderRootPath()
{
    std::filesystem::path searchPath = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i)
    {
        const std::filesystem::path candidate = searchPath / "Editor" / "Shaders";
        if (std::filesystem::exists(candidate))
            return candidate.lexically_normal();

        if (!searchPath.has_parent_path())
            break;
        searchPath = searchPath.parent_path();
    }

    return {};
}

uint64_t HashString64(const std::string& value)
{
    return std::hash<std::string>{}(value);
}

uint64_t HashFloat64(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(uint32_t));
    return static_cast<uint64_t>(bits);
}

uint64_t HashMat4(const Mat4& matrix)
{
    uint64_t hash = 1469598103934665603ull;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
            hash = HashCombine64(hash, HashFloat64(matrix.columns[column][row]));
    }
    return hash;
}

uint64_t MakeHlodCellKey(int32_t cellX, int32_t cellY, int32_t cellZ)
{
    uint64_t hash = 1469598103934665603ull;
    hash = HashCombine64(hash, static_cast<uint64_t>(static_cast<uint32_t>(cellX)));
    hash = HashCombine64(hash, static_cast<uint64_t>(static_cast<uint32_t>(cellY)));
    hash = HashCombine64(hash, static_cast<uint64_t>(static_cast<uint32_t>(cellZ)));
    return hash;
}

Vec3 GetWorldForward(const TransformComponent& transform)
{
    return transform.worldMatrix.TransformDirection(Vec3::UnitZ()).Normalized();
}

float SnapScalar(float value, float step)
{
    if (step <= 0.0f)
        return value;
    return std::round(value / step) * step;
}

Vec3 SnapVector(const Vec3& value, float step)
{
    return Vec3(SnapScalar(value.x, step), SnapScalar(value.y, step), SnapScalar(value.z, step));
}

uint32 GetEntityRenderMask(World& world, Entity entity)
{
    if (RenderLayerComponent* renderLayer = world.GetComponent<RenderLayerComponent>(entity))
        return renderLayer->mask;
    return RenderLayerMask::World;
}

bool MatchesRenderMask(World& world, Entity entity, uint32 mask)
{
    return mask == 0xFFFFFFFFu || (GetEntityRenderMask(world, entity) & mask) != 0;
}

float GetWorldMaxScale(const TransformComponent& transform)
{
    const Vec3 scale = GetWorldScale(transform);
    return (std::max)({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 0.001f});
}

float EstimateProjectedScreenHeightForDiagnostics(const Camera& camera, const PrimitiveMesh& mesh,
                                                  const TransformComponent& transform)
{
    float camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    camera.GetPosition(camX, camY, camZ);

    const Vec3 worldPosition = GetWorldPosition(transform);
    const float dx = worldPosition.x - camX;
    const float dy = worldPosition.y - camY;
    const float dz = worldPosition.z - camZ;
    const float distance = (std::max)(std::sqrt(dx * dx + dy * dy + dz * dz), camera.GetNearZ());
    const float tanHalfFov = std::tan(camera.GetFOV() * 0.5f);
    if (tanHalfFov <= 0.0001f)
        return 1.0f;

    const float objectRadius = (std::max)(mesh.GetMaxExtent() * 0.5f * GetWorldMaxScale(transform), 0.001f);
    return std::clamp((objectRadius / distance) / tanHalfFov * 2.0f, 0.0f, 16.0f);
}

bool HasDistinctMeshLodForDiagnostics(const PrimitiveMesh& mesh, uint32_t lodLevel)
{
    if (lodLevel == 0 || lodLevel >= PrimitiveMesh::kLodCount)
        return false;

    return mesh.lodVertexBuffers[lodLevel] && mesh.lodIndexBuffers[lodLevel] &&
           (mesh.lodVertexBuffers[lodLevel] != mesh.vertexBuffer || mesh.lodIndexBuffers[lodLevel] != mesh.indexBuffer ||
            mesh.lodIndexCounts[lodLevel] != mesh.indexCount);
}

uint32_t ChooseMeshLodLevelForDiagnostics(const Camera& camera, const PrimitiveMesh& mesh,
                                          const TransformComponent& transform, float lod1Threshold,
                                          float lod2Threshold)
{
    const float projectedHeight = EstimateProjectedScreenHeightForDiagnostics(camera, mesh, transform);
    if (projectedHeight < lod2Threshold && HasDistinctMeshLodForDiagnostics(mesh, 2))
        return 2;
    if (projectedHeight < lod1Threshold && HasDistinctMeshLodForDiagnostics(mesh, 1))
        return 1;
    return 0;
}

void ComputeWorldBoundsForDiagnostics(const TransformComponent& transform, const PrimitiveMesh& mesh, float& minX,
                                      float& minY, float& minZ, float& maxX, float& maxY, float& maxZ)
{
    const Vec3 localCorners[8] = {
        Vec3(mesh.boundsMinX, mesh.boundsMinY, mesh.boundsMinZ),
        Vec3(mesh.boundsMaxX, mesh.boundsMinY, mesh.boundsMinZ),
        Vec3(mesh.boundsMinX, mesh.boundsMaxY, mesh.boundsMinZ),
        Vec3(mesh.boundsMaxX, mesh.boundsMaxY, mesh.boundsMinZ),
        Vec3(mesh.boundsMinX, mesh.boundsMinY, mesh.boundsMaxZ),
        Vec3(mesh.boundsMaxX, mesh.boundsMinY, mesh.boundsMaxZ),
        Vec3(mesh.boundsMinX, mesh.boundsMaxY, mesh.boundsMaxZ),
        Vec3(mesh.boundsMaxX, mesh.boundsMaxY, mesh.boundsMaxZ),
    };

    Vec3 worldCorner = transform.worldMatrix.TransformPoint(localCorners[0]);
    minX = maxX = worldCorner.x;
    minY = maxY = worldCorner.y;
    minZ = maxZ = worldCorner.z;

    for (int i = 1; i < 8; ++i)
    {
        worldCorner = transform.worldMatrix.TransformPoint(localCorners[i]);
        minX = (std::min)(minX, worldCorner.x);
        minY = (std::min)(minY, worldCorner.y);
        minZ = (std::min)(minZ, worldCorner.z);
        maxX = (std::max)(maxX, worldCorner.x);
        maxY = (std::max)(maxY, worldCorner.y);
        maxZ = (std::max)(maxZ, worldCorner.z);
    }
}

bool ClosestRayParameterToAxis(const Ray& ray, const Vec3& axisOrigin, const Vec3& axisDirection, float& outAxisT,
                               float* outDistance = nullptr)
{
    const Vec3 dir = axisDirection.Normalized();
    if (dir.LengthSquared() <= 1e-8f)
        return false;

    const Vec3 diff = ray.origin - axisOrigin;
    const float a = Vec3::Dot(ray.direction, ray.direction);
    const float b = Vec3::Dot(ray.direction, dir);
    const float c = Vec3::Dot(dir, dir);
    const float d = Vec3::Dot(ray.direction, diff);
    const float e = Vec3::Dot(dir, diff);
    const float denom = (a * c) - (b * b);

    float rayT = 0.0f;
    float axisT = 0.0f;
    if (std::abs(denom) > 1e-6f)
    {
        rayT = ((b * e) - (c * d)) / denom;
        axisT = ((a * e) - (b * d)) / denom;
    }
    else
    {
        axisT = e / c;
    }

    if (rayT < 0.0f)
    {
        rayT = 0.0f;
        axisT = e / c;
    }

    const Vec3 pointOnRay = ray.origin + (ray.direction * rayT);
    const Vec3 pointOnAxis = axisOrigin + (dir * axisT);
    if (outDistance)
        *outDistance = Vec3::Distance(pointOnRay, pointOnAxis);
    outAxisT = axisT;
    return true;
}

const char* GetMapDragCommandName(const MapSelection& selection)
{
    if (selection.vertexIndex >= 0)
        return "Move Vertex";
    if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0)
        return "Move Edge";
    if (selection.faceIndex >= 0)
        return "Move Face";
    return "Move Brush";
}

const char* GetMapSelectionKindLabel(const MapSelection& selection, size_t count)
{
    if (selection.vertexIndex >= 0)
        return count == 1 ? "vertex" : "vertices";
    if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0)
        return count == 1 ? "edge" : "edges";
    if (selection.faceIndex >= 0)
        return count == 1 ? "face" : "faces";
    return count == 1 ? "brush" : "brushes";
}

bool ProjectWorldPointToScreen(const Camera& camera, const Vec3& point, float viewportX, float viewportY, float screenWidth,
                               float screenHeight, ImVec2& outScreenPosition)
{
    const float* viewProjection = camera.GetViewProjectionMatrix();
    const float clipX = viewProjection[0] * point.x + viewProjection[4] * point.y + viewProjection[8] * point.z +
                        viewProjection[12];
    const float clipY = viewProjection[1] * point.x + viewProjection[5] * point.y + viewProjection[9] * point.z +
                        viewProjection[13];
    const float clipW = viewProjection[3] * point.x + viewProjection[7] * point.y + viewProjection[11] * point.z +
                        viewProjection[15];

    if (clipW <= 0.0001f)
        return false;

    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;
    outScreenPosition.x = viewportX + ((ndcX * 0.5f + 0.5f) * screenWidth);
    outScreenPosition.y = viewportY + ((1.0f - (ndcY * 0.5f + 0.5f)) * screenHeight);
    return true;
}

bool ExpandScreenBoundsForVertices(const Camera& camera, const std::vector<Vec3>& vertices, const std::vector<uint32>* indices,
                                   float viewportX, float viewportY, float screenWidth, float screenHeight,
                                   ImVec2& outMin, ImVec2& outMax)
{
    bool projectedAny = false;
    outMin = ImVec2(screenWidth, screenHeight);
    outMax = ImVec2(0.0f, 0.0f);

    auto projectVertex = [&](const Vec3& point)
    {
        ImVec2 screenPoint;
        if (!ProjectWorldPointToScreen(camera, point, viewportX, viewportY, screenWidth, screenHeight, screenPoint))
            return;

        projectedAny = true;
        outMin.x = std::min(outMin.x, screenPoint.x);
        outMin.y = std::min(outMin.y, screenPoint.y);
        outMax.x = std::max(outMax.x, screenPoint.x);
        outMax.y = std::max(outMax.y, screenPoint.y);
    };

    if (indices)
    {
        for (uint32 index : *indices)
        {
            if (index < vertices.size())
                projectVertex(vertices[index]);
        }
    }
    else
    {
        for (const Vec3& vertex : vertices)
            projectVertex(vertex);
    }

    return projectedAny;
}

bool RectanglesOverlap(const ImVec2& minA, const ImVec2& maxA, const ImVec2& minB, const ImVec2& maxB)
{
    return minA.x <= maxB.x && maxA.x >= minB.x && minA.y <= maxB.y && maxA.y >= minB.y;
}

bool ScreenPointToCameraPlaneWorld(const Camera& camera, float screenX, float screenY, float viewportX, float viewportY,
                                   float viewportWidth, float viewportHeight, float planeDepth, Vec3& outPoint)
{
    if (viewportWidth <= 1.0f || viewportHeight <= 1.0f)
        return false;

    const float normalizedX = ((screenX - viewportX) / viewportWidth) * 2.0f - 1.0f;
    const float normalizedY = 1.0f - (((screenY - viewportY) / viewportHeight) * 2.0f);

    float originX, originY, originZ;
    float rightX, rightY, rightZ;
    float upX, upY, upZ;
    float forwardX, forwardY, forwardZ;
    camera.GetPosition(originX, originY, originZ);
    camera.GetRight(rightX, rightY, rightZ);
    camera.GetUp(upX, upY, upZ);
    camera.GetForward(forwardX, forwardY, forwardZ);

    const float tanHalfVerticalFov = std::tan(camera.GetFOV() * 0.5f);
    const float tanHalfHorizontalFov = tanHalfVerticalFov * camera.GetAspect();
    const float cameraPlaneX = normalizedX * tanHalfHorizontalFov * planeDepth;
    const float cameraPlaneY = normalizedY * tanHalfVerticalFov * planeDepth;

    outPoint.x = originX + forwardX * planeDepth + rightX * cameraPlaneX + upX * cameraPlaneY;
    outPoint.y = originY + forwardY * planeDepth + rightY * cameraPlaneX + upY * cameraPlaneY;
    outPoint.z = originZ + forwardZ * planeDepth + rightZ * cameraPlaneX + upZ * cameraPlaneY;
    return true;
}

std::vector<MapSelection> CollectMapSelectionsInScreenRect(const Camera& camera, const MapDocument& document, const ImVec2& start,
                                                           const ImVec2& end, float viewportX, float viewportY,
                                                           float screenWidth, float screenHeight)
{
    std::vector<MapSelection> selections;
    const ImVec2 rectMin(std::min(start.x, end.x), std::min(start.y, end.y));
    const ImVec2 rectMax(std::max(start.x, end.x), std::max(start.y, end.y));
    const MapSelectionMode selectionMode = document.GetSelectionMode();
    const MapAsset& asset = document.GetAsset();
    auto selectionAlreadyQueued = [&](const MapSelection& candidate)
    {
        return std::find_if(selections.begin(), selections.end(),
                            [&](const MapSelection& queued)
                            {
                                return queued.brushId == candidate.brushId && queued.faceIndex == candidate.faceIndex &&
                                       queued.vertexIndex == candidate.vertexIndex &&
                                       queued.edgeVertexA == candidate.edgeVertexA &&
                                       queued.edgeVertexB == candidate.edgeVertexB;
                            }) != selections.end();
    };

    if (selectionMode == MapSelectionMode::Brush)
    {
        selections.reserve(asset.brushes.size());
        for (const MapBrush& brush : asset.brushes)
        {
            if (document.IsBrushHidden(brush.brushId) || document.IsBrushLocked(brush.brushId))
                continue;
            ImVec2 boundsMin;
            ImVec2 boundsMax;
            if (!ExpandScreenBoundsForVertices(camera, brush.vertices, nullptr, viewportX, viewportY, screenWidth,
                                               screenHeight, boundsMin, boundsMax))
                continue;
            if (!RectanglesOverlap(rectMin, rectMax, boundsMin, boundsMax))
                continue;

            MapSelection selection;
            selection.brushId = brush.brushId;
            selections.push_back(selection);
        }
    }
    else if (selectionMode == MapSelectionMode::Face)
    {
        for (const MapBrush& brush : asset.brushes)
        {
            if (document.IsBrushHidden(brush.brushId) || document.IsBrushLocked(brush.brushId))
                continue;
            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                const MapFace& face = brush.faces[faceIndex];
                ImVec2 boundsMin;
                ImVec2 boundsMax;
                if (!ExpandScreenBoundsForVertices(camera, brush.vertices, &face.vertexIndices, viewportX, viewportY,
                                                   screenWidth, screenHeight, boundsMin, boundsMax))
                {
                    continue;
                }
                if (!RectanglesOverlap(rectMin, rectMax, boundsMin, boundsMax))
                    continue;

                MapSelection selection;
                selection.brushId = brush.brushId;
                selection.faceIndex = static_cast<int>(faceIndex);
                selections.push_back(selection);
            }
        }
    }
    else if (selectionMode == MapSelectionMode::Edge)
    {
        for (const MapBrush& brush : asset.brushes)
        {
            if (document.IsBrushHidden(brush.brushId) || document.IsBrushLocked(brush.brushId))
                continue;
            for (const MapFace& face : brush.faces)
            {
                if (face.vertexIndices.size() < 2)
                    continue;

                for (size_t edgeIndex = 0; edgeIndex < face.vertexIndices.size(); ++edgeIndex)
                {
                    const uint32 rawA = face.vertexIndices[edgeIndex];
                    const uint32 rawB = face.vertexIndices[(edgeIndex + 1) % face.vertexIndices.size()];
                    if (rawA >= brush.vertices.size() || rawB >= brush.vertices.size())
                        continue;

                    ImVec2 screenA;
                    ImVec2 screenB;
                    if (!ProjectWorldPointToScreen(camera, brush.vertices[rawA], viewportX, viewportY, screenWidth,
                                                   screenHeight, screenA) ||
                        !ProjectWorldPointToScreen(camera, brush.vertices[rawB], viewportX, viewportY, screenWidth,
                                                   screenHeight, screenB))
                    {
                        continue;
                    }

                    const ImVec2 edgeMin(std::min(screenA.x, screenB.x), std::min(screenA.y, screenB.y));
                    const ImVec2 edgeMax(std::max(screenA.x, screenB.x), std::max(screenA.y, screenB.y));
                    if (!RectanglesOverlap(rectMin, rectMax, edgeMin, edgeMax))
                        continue;

                    MapSelection selection;
                    selection.brushId = brush.brushId;
                    selection.edgeVertexA = static_cast<int>(std::min(rawA, rawB));
                    selection.edgeVertexB = static_cast<int>(std::max(rawA, rawB));
                    if (!selectionAlreadyQueued(selection))
                        selections.push_back(selection);
                }
            }
        }
    }
    else if (selectionMode == MapSelectionMode::Vertex)
    {
        for (const MapBrush& brush : asset.brushes)
        {
            if (document.IsBrushHidden(brush.brushId) || document.IsBrushLocked(brush.brushId))
                continue;
            for (size_t vertexIndex = 0; vertexIndex < brush.vertices.size(); ++vertexIndex)
            {
                ImVec2 screenPoint;
                if (!ProjectWorldPointToScreen(camera, brush.vertices[vertexIndex], viewportX, viewportY, screenWidth,
                                               screenHeight, screenPoint))
                {
                    continue;
                }

                if (screenPoint.x < rectMin.x || screenPoint.x > rectMax.x || screenPoint.y < rectMin.y ||
                    screenPoint.y > rectMax.y)
                {
                    continue;
                }

                MapSelection selection;
                selection.brushId = brush.brushId;
                selection.vertexIndex = static_cast<int>(vertexIndex);
                selections.push_back(selection);
            }
        }
    }

    return selections;
}

std::vector<Entity> CollectEntitiesInScreenRect(const Camera& camera, World& world, const ImVec2& start, const ImVec2& end,
                                                float viewportX, float viewportY, float screenWidth, float screenHeight)
{
    std::vector<Entity> entities;
    const ImVec2 rectMin(std::min(start.x, end.x), std::min(start.y, end.y));
    const ImVec2 rectMax(std::max(start.x, end.x), std::max(start.y, end.y));

    world.Each<TransformComponent>(
        [&](Entity entity, TransformComponent& transform)
        {
            const Vec3 worldPosition = GetWorldPosition(transform);
            const Vec3 worldScale = GetWorldScale(transform);
            const Vec3 halfExtents(std::max(0.5f, std::abs(worldScale.x) * 0.5f),
                                   std::max(0.5f, std::abs(worldScale.y) * 0.5f),
                                   std::max(0.5f, std::abs(worldScale.z) * 0.5f));

            const std::vector<Vec3> boundsCorners = {
                Vec3(worldPosition.x - halfExtents.x, worldPosition.y - halfExtents.y, worldPosition.z - halfExtents.z),
                Vec3(worldPosition.x + halfExtents.x, worldPosition.y - halfExtents.y, worldPosition.z - halfExtents.z),
                Vec3(worldPosition.x - halfExtents.x, worldPosition.y + halfExtents.y, worldPosition.z - halfExtents.z),
                Vec3(worldPosition.x + halfExtents.x, worldPosition.y + halfExtents.y, worldPosition.z - halfExtents.z),
                Vec3(worldPosition.x - halfExtents.x, worldPosition.y - halfExtents.y, worldPosition.z + halfExtents.z),
                Vec3(worldPosition.x + halfExtents.x, worldPosition.y - halfExtents.y, worldPosition.z + halfExtents.z),
                Vec3(worldPosition.x - halfExtents.x, worldPosition.y + halfExtents.y, worldPosition.z + halfExtents.z),
                Vec3(worldPosition.x + halfExtents.x, worldPosition.y + halfExtents.y, worldPosition.z + halfExtents.z),
            };

            ImVec2 boundsMin;
            ImVec2 boundsMax;
            if (!ExpandScreenBoundsForVertices(camera, boundsCorners, nullptr, viewportX, viewportY, screenWidth,
                                               screenHeight, boundsMin, boundsMax))
            {
                return;
            }

            if (RectanglesOverlap(rectMin, rectMax, boundsMin, boundsMax))
                entities.push_back(entity);
        });

    return entities;
}

} // namespace

bool ViewportPanel::Initialize(RHIDevice* device)
{
    // Create the ECS World
    m_World = std::make_unique<World>();

    // Store device reference for Frame Graph execution
    m_Device = device;
    AssetManager::Get().SetDevice(device);

    m_Renderer = std::make_unique<SimpleRenderer>();
    if (const std::filesystem::path shaderRoot = FindEditorShaderRootPath(); !shaderRoot.empty())
        m_Renderer->SetShaderRootPath(shaderRoot.string(), false);
    if (!m_Renderer->Initialize(device))
        return false;
    m_Renderer->SetHZBEnabled(false);

    m_GizmoRenderer = std::make_unique<GizmoRenderer>();
    if (!m_GizmoRenderer->Initialize(device))
        return false;

    m_OverlayGizmoRenderer = std::make_unique<GizmoRenderer>(true);
    if (!m_OverlayGizmoRenderer->Initialize(device))
        return false;

    m_GridRenderer = std::make_unique<GridRenderer>();
    if (!m_GridRenderer->Initialize(device))
        return false;

    // Setup camera
    m_Camera.SetPosition(2.5f, 2.0f, 2.5f);
    m_Camera.LookAt(0.0f, 0.0f, 0.0f);
    m_Camera.SetPerspective(60.0f, 16.0f / 9.0f, 0.001f, 1000.0f);

    // No default entity - user creates entities via hierarchy panel
    m_SelectedEntity = kNullEntity;
    m_SelectedEntities.clear();

    // Initialize translate gizmo
    m_TranslateGizmo.SetPosition(0, 0, 0);
    m_TranslateGizmo.SetSize(1.0f);

    m_Initialized = true;
    return true;
}

void ViewportPanel::ResetWorld()
{
    // Clear selection first
    m_SelectedEntity = kNullEntity;
    m_SelectedEntities.clear();
    m_ResolvedMaterialPathCache.clear();
    m_MaterialRuntimeCache.clear();

    // Create a fresh new world (completely avoids buggy Clear())
    m_World = std::make_unique<World>();
}

MapDocument* ViewportPanel::GetActiveMapRenderDocument() const
{
    if (m_MapEditingEnabled && m_MapDocument)
        return m_MapDocument;
    if (m_SceneMapDocument)
        return m_SceneMapDocument;
    return m_MapDocument;
}

void ViewportPanel::RebuildMapPreviewIfNeeded()
{
    MapDocument* renderDocument = GetActiveMapRenderDocument();
    if (!renderDocument || !m_Renderer)
    {
        m_MapPreviewParts.clear();
        m_MapPreviewRevision = 0;
        m_MapPreviewSourceDocument = nullptr;
        return;
    }

    if (m_MapPreviewSourceDocument == renderDocument && m_MapPreviewRevision == renderDocument->GetRevision())
        return;

    m_MapPreviewParts.clear();
    m_MapPreviewSourceDocument = renderDocument;
    m_MapPreviewRevision = renderDocument->GetRevision();

    const MapCompiledData& compiled = renderDocument->GetCompiledData();
    if (compiled.vertices.empty() || compiled.indices.empty())
        return;

    const MapAsset& mapAsset = renderDocument->GetAsset();
    std::unordered_map<std::string, MaterialData> materialDataCache;
    materialDataCache.reserve(compiled.submeshes.size());

    auto resolveMaterialPath = [&mapAsset](const MapCompiledSubmesh& submesh) -> std::string
    {
        const MapBrush* brush = mapAsset.FindBrush(submesh.brushId);
        const MapFace* face = (brush && submesh.faceIndex < brush->faces.size()) ? &brush->faces[submesh.faceIndex] : nullptr;
        return face ? face->materialPath : submesh.materialPath;
    };

    auto buildMaterialData = [&materialDataCache](const std::string& materialPath) -> MaterialData
    {
        MaterialData material;
        if (materialPath.empty())
            return material;

        const auto cacheIt = materialDataCache.find(materialPath);
        if (cacheIt != materialDataCache.end())
            return cacheIt->second;

        const std::string fullPath = AssetManager::Get().GetFullPath(materialPath);
        LoadedMaterial loaded = MaterialLoader::Load(fullPath);
        if (!loaded.valid)
            return material;

        material.colorR = loaded.baseColor.x;
        material.colorG = loaded.baseColor.y;
        material.colorB = loaded.baseColor.z;
        material.metallic = loaded.metallic;
        material.roughness = loaded.roughness;
        material.ambientOcclusion = loaded.ambientOcclusion;
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
            material.textureSampleTypes[i] = loaded.textureSampleTypes[i];
            if (loaded.hasTextures[i])
                material.hasTexture = 1.0f;
        }
        material.albedoTextureSlot = loaded.albedoTextureSlot;
        material.normalTextureSlot = loaded.normalTextureSlot;
        material.ormTextureSlot = loaded.ormTextureSlot;
        materialDataCache.emplace(materialPath, material);
        return material;
    };

    struct PendingMapPreviewBatch
    {
        MeshData meshData;
        MaterialData material;
    };

    std::unordered_map<std::string, PendingMapPreviewBatch> batches;
    batches.reserve(compiled.submeshes.size());

    for (const MapCompiledSubmesh& submesh : compiled.submeshes)
    {
        if (submesh.indexCount == 0)
            continue;
        if (renderDocument->IsBrushHidden(submesh.brushId))
            continue;

        const std::string materialPath = resolveMaterialPath(submesh);
        PendingMapPreviewBatch& batch = batches[materialPath];
        if (batch.meshData.vertices.empty())
            batch.material = buildMaterialData(materialPath);

        const uint32_t baseVertex = static_cast<uint32_t>(batch.meshData.vertices.size());

        bool boundsInitialized = false;
        for (uint32_t localIndex = 0; localIndex < submesh.indexCount; ++localIndex)
        {
            const uint32_t sourceIndex = compiled.indices[submesh.indexStart + localIndex];
            const MapCompiledVertex& sourceVertex = compiled.vertices[sourceIndex];

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

            if (!boundsInitialized)
            {
                if (batch.meshData.vertices.size() == 1)
                {
                    batch.meshData.boundsMinX = batch.meshData.boundsMaxX = vertex.x;
                    batch.meshData.boundsMinY = batch.meshData.boundsMaxY = vertex.y;
                    batch.meshData.boundsMinZ = batch.meshData.boundsMaxZ = vertex.z;
                }
                boundsInitialized = true;
            }

            if (boundsInitialized)
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
        MapPreviewPart part;
        part.material = batch.material;
        part.mesh = m_Renderer->CreateRuntimeMesh(batch.meshData);
        if (part.mesh)
            m_MapPreviewParts.push_back(std::move(part));
    }
}

void ViewportPanel::SetPlayMode(bool playMode)
{
    m_PlayMode = playMode;
}

void ViewportPanel::SetSelectedEntity(Entity entity)
{
    SetSelectedEntities(entity.IsValid() ? std::vector<Entity>{entity} : std::vector<Entity>{}, entity);
}

void ViewportPanel::SetSelectedEntities(const std::vector<Entity>& entities, Entity primaryEntity)
{
    if (m_SceneContext)
        SyncSelectionFromContext();

    m_SelectedEntities.clear();
    m_SelectedEntities.reserve(entities.size());

    for (Entity entity : entities)
    {
        if (!entity.IsValid() || !m_World || !m_World->IsAlive(entity))
            continue;
        if (std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end())
            continue;
        m_SelectedEntities.push_back(entity);
    }

    if (primaryEntity.IsValid() &&
        std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), primaryEntity) != m_SelectedEntities.end())
    {
        m_SelectedEntity = primaryEntity;
    }
    else
    {
        m_SelectedEntity = m_SelectedEntities.empty() ? kNullEntity : m_SelectedEntities.back();
    }

    UpdateEntityGizmoPlacement();
    PublishSelectionToContext();
}

std::vector<Entity> ViewportPanel::GetValidSelectedEntities() const
{
    std::vector<Entity> entities;
    entities.reserve(m_SelectedEntities.size());

    for (Entity entity : m_SelectedEntities)
    {
        if (entity.IsValid() && m_World && m_World->IsAlive(entity) && m_World->GetComponent<TransformComponent>(entity))
            entities.push_back(entity);
    }

    if (entities.empty() && m_SelectedEntity.IsValid() && m_World && m_World->IsAlive(m_SelectedEntity) &&
        m_World->GetComponent<TransformComponent>(m_SelectedEntity))
    {
        entities.push_back(m_SelectedEntity);
    }

    return entities;
}

Vec3 ViewportPanel::ComputeSelectionPivot() const
{
    const std::vector<Entity> entities = GetValidSelectedEntities();
    if (entities.empty())
        return Vec3::Zero();

    Vec3 pivot = Vec3::Zero();
    int count = 0;
    for (Entity entity : entities)
    {
        const auto* transform = m_World->GetComponent<TransformComponent>(entity);
        if (!transform)
            continue;
        pivot += GetWorldPosition(*transform);
        ++count;
    }

    return count > 0 ? (pivot / static_cast<float>(count)) : Vec3::Zero();
}

void ViewportPanel::SyncSelectionFromContext()
{
    if (!m_SceneContext)
        return;

    const EditorSelectionState& selectionState = m_SceneContext->GetEntitySelection();
    m_SelectedEntity = selectionState.GetPrimaryEntity();
    m_SelectedEntities = selectionState.GetEntities();
}

void ViewportPanel::PublishSelectionToContext()
{
    if (!m_SceneContext)
        return;
    m_SceneContext->GetEntitySelection().SetSelection(m_World.get(), m_SelectedEntities, m_SelectedEntity);
}

void ViewportPanel::UpdateEntityGizmoPlacement()
{
    const std::vector<Entity> entities = GetValidSelectedEntities();
    if (entities.empty())
        return;

    const Vec3 pivot = ComputeSelectionPivot();
    m_TranslateGizmo.SetPosition(pivot.x, pivot.y, pivot.z);
    m_RotateGizmo.SetPosition(pivot.x, pivot.y, pivot.z);
    m_ScaleGizmo.SetPosition(pivot.x, pivot.y, pivot.z);

    float gizmoSize = 1.0f;
    for (Entity entity : entities)
    {
        const auto* transform = m_World->GetComponent<TransformComponent>(entity);
        if (!transform)
            continue;

        const Vec3 worldPosition = GetWorldPosition(*transform);
        const Vec3 worldScale = GetWorldScale(*transform);
        const float distanceFromPivot = Vec3::Distance(worldPosition, pivot);
        const float entityScale = std::max({worldScale.x, worldScale.y, worldScale.z});
        gizmoSize = std::max(gizmoSize, std::max(distanceFromPivot, entityScale));
    }

    if (entities.size() == 1)
    {
        auto* meshComp = m_World->GetComponent<MeshComponent>(entities.front());
        float meshExtent = 1.0f;
        if (meshComp && !meshComp->meshPath.empty() && m_Renderer)
        {
            std::vector<PrimitiveMesh*> meshes = m_Renderer->LoadMesh(meshComp->meshPath);
            float maxExtent = 0.0f;
            for (auto* mesh : meshes)
            {
                if (mesh)
                    maxExtent = std::max(maxExtent, mesh->GetMaxExtent());
            }
            if (maxExtent > 0.0f)
                meshExtent = std::max(1.0f, maxExtent);
        }
        gizmoSize = std::max(gizmoSize, meshExtent);
    }

    m_TranslateGizmo.SetSize(gizmoSize);
    m_RotateGizmo.SetSize(gizmoSize);
    m_ScaleGizmo.SetSize(gizmoSize);
}

Gizmo* ViewportPanel::GetActiveGizmo()
{
    switch (m_GizmoMode)
    {
        case GizmoMode::Translate:
            return &m_TranslateGizmo;
        case GizmoMode::Rotate:
            return &m_RotateGizmo;
        case GizmoMode::Scale:
            return &m_ScaleGizmo;
        default:
            return &m_TranslateGizmo;
    }
}

Gizmo* ViewportPanel::GetMapGizmo()
{
    return &m_TranslateGizmo;
}

bool ViewportPanel::HasMapSelection() const
{
    return m_MapDocument && !m_MapDocument->GetSelections().empty();
}

void ViewportPanel::UpdateMapGizmoPlacement()
{
    if (!HasMapSelection() || m_MapClipToolActive || m_MapExtrudeToolActive || m_MapHollowPreviewActive)
        return;

    const Vec3 pivot = m_MapDocument->GetSelectionPivot();
    Gizmo* gizmo = GetMapGizmo();
    gizmo->SetPosition(pivot.x, pivot.y, pivot.z);
    gizmo->SetSize(1.0f);
    gizmo->SetLocalSpace(false);
    gizmo->SetRotation(0.0f, 0.0f, 0.0f);
    m_GizmoMode = GizmoMode::Translate;
}

void ViewportPanel::HandleMapKeyboardShortcuts()
{
    if (!m_MapDocument)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || m_IsOrbiting || m_IsPanning)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_W))
        m_GizmoMode = GizmoMode::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_1))
        m_MapDocument->SetSelectionMode(MapSelectionMode::Brush);
    if (ImGui::IsKeyPressed(ImGuiKey_2))
        m_MapDocument->SetSelectionMode(MapSelectionMode::Face);
    if (ImGui::IsKeyPressed(ImGuiKey_3))
        m_MapDocument->SetSelectionMode(MapSelectionMode::Edge);
    if (ImGui::IsKeyPressed(ImGuiKey_4))
        m_MapDocument->SetSelectionMode(MapSelectionMode::Vertex);

    if (ImGui::IsKeyPressed(ImGuiKey_A) && io.KeyShift)
    {
        const MapAsset beforeAsset = m_MapDocument->GetAsset();
        const std::vector<MapSelection> beforeSelections = m_MapDocument->GetSelections();
        const MapSelection beforeSelection = m_MapDocument->GetSelection();
        const std::unordered_set<uint32> beforeHiddenBrushIds = m_MapDocument->GetHiddenBrushIds();
        const std::unordered_set<uint32> beforeLockedBrushIds = m_MapDocument->GetLockedBrushIds();
        const bool beforeDirty = m_MapDocument->IsDirty();
        m_MapDocument->CreateBoxBrush(Vec3::Zero(), Vec3(0.5f, 0.5f, 0.5f));
        CommandRegistry::Get().PushCommand(std::make_unique<MapSnapshotCommand>(
            m_MapDocument, "Create Box Brush", beforeAsset, beforeSelections, beforeSelection, beforeHiddenBrushIds,
            beforeLockedBrushIds, beforeDirty, m_MapDocument->GetAsset(), m_MapDocument->GetSelections(),
            m_MapDocument->GetSelection(), m_MapDocument->GetHiddenBrushIds(), m_MapDocument->GetLockedBrushIds(),
            m_MapDocument->IsDirty()));
        UpdateMapGizmoPlacement();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_D) && io.KeyCtrl && HasMapSelection())
    {
        const MapAsset beforeAsset = m_MapDocument->GetAsset();
        const std::vector<MapSelection> beforeSelections = m_MapDocument->GetSelections();
        const MapSelection beforeSelection = m_MapDocument->GetSelection();
        const std::unordered_set<uint32> beforeHiddenBrushIds = m_MapDocument->GetHiddenBrushIds();
        const std::unordered_set<uint32> beforeLockedBrushIds = m_MapDocument->GetLockedBrushIds();
        const bool beforeDirty = m_MapDocument->IsDirty();
        if (m_MapDocument->DuplicateSelectedBrush())
        {
            CommandRegistry::Get().PushCommand(std::make_unique<MapSnapshotCommand>(
                m_MapDocument, "Duplicate Brush", beforeAsset, beforeSelections, beforeSelection, beforeHiddenBrushIds,
                beforeLockedBrushIds, beforeDirty, m_MapDocument->GetAsset(), m_MapDocument->GetSelections(),
                m_MapDocument->GetSelection(), m_MapDocument->GetHiddenBrushIds(), m_MapDocument->GetLockedBrushIds(),
                m_MapDocument->IsDirty()));
            UpdateMapGizmoPlacement();
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && HasMapSelection())
    {
        const MapAsset beforeAsset = m_MapDocument->GetAsset();
        const std::vector<MapSelection> beforeSelections = m_MapDocument->GetSelections();
        const MapSelection beforeSelection = m_MapDocument->GetSelection();
        const std::unordered_set<uint32> beforeHiddenBrushIds = m_MapDocument->GetHiddenBrushIds();
        const std::unordered_set<uint32> beforeLockedBrushIds = m_MapDocument->GetLockedBrushIds();
        const bool beforeDirty = m_MapDocument->IsDirty();
        if (m_MapDocument->DeleteSelectedBrush())
        {
            CommandRegistry::Get().PushCommand(std::make_unique<MapSnapshotCommand>(
                m_MapDocument, "Delete Brush", beforeAsset, beforeSelections, beforeSelection, beforeHiddenBrushIds,
                beforeLockedBrushIds, beforeDirty, m_MapDocument->GetAsset(), m_MapDocument->GetSelections(),
                m_MapDocument->GetSelection(), m_MapDocument->GetHiddenBrushIds(), m_MapDocument->GetLockedBrushIds(),
                m_MapDocument->IsDirty()));
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F) && HasMapSelection())
    {
        const Vec3 pivot = m_MapDocument->GetSelectionPivot();
        m_Camera.Focus(pivot.x, pivot.y, pivot.z, 5.0f);
    }
}

bool ViewportPanel::BeginMapGizmoDrag(const Ray& ray)
{
    if (!HasMapSelection() || m_MapClipToolActive || m_MapExtrudeToolActive || m_MapHollowPreviewActive)
        return false;

    UpdateMapGizmoPlacement();
    Gizmo* gizmo = GetMapGizmo();
    const GizmoHit hit = gizmo->HitTest(ray, m_Camera);
    if (!hit.hit)
        return false;

    gizmo->BeginDrag(hit.axis, ray, m_Camera);
    m_MouseDragging = true;
    m_MapDragStartAsset = m_MapDocument->GetAsset();
    m_MapDragStartSelections = m_MapDocument->GetSelections();
    m_MapDragStartSelection = m_MapDocument->GetSelection();
    m_MapDragStartHiddenBrushIds = m_MapDocument->GetHiddenBrushIds();
    m_MapDragStartLockedBrushIds = m_MapDocument->GetLockedBrushIds();
    m_MapDragStartDirty = m_MapDocument->IsDirty();
    m_MapDragStartRevision = m_MapDocument->GetRevision();
    m_MapDragAccumulatedDelta = Vec3::Zero();
    m_MapDragAppliedDelta = Vec3::Zero();
    m_MapDragAccumulatedFaceDistance = 0.0f;
    m_MapDragAppliedFaceDistance = 0.0f;
    return true;
}

bool ViewportPanel::UpdateMapGizmoDrag(const Ray& ray)
{
    if (!m_MouseDragging || !m_MapDocument)
        return false;

    float deltaX = 0.0f;
    float deltaY = 0.0f;
    float deltaZ = 0.0f;
    Gizmo* gizmo = GetMapGizmo();
    if (!gizmo->UpdateDrag(ray, m_Camera, deltaX, deltaY, deltaZ))
        return false;

    const Vec3 rawDelta(deltaX, deltaY, deltaZ);
    bool changed = false;
    const MapSelection selection = m_MapDocument->GetSelection();
    if (selection.vertexIndex >= 0)
    {
        m_MapDragAccumulatedDelta += rawDelta;
        const Vec3 targetDelta =
            m_MapTranslationSnapEnabled ? SnapVector(m_MapDragAccumulatedDelta, m_MapTranslationSnapStep) : m_MapDragAccumulatedDelta;
        const Vec3 stepDelta = targetDelta - m_MapDragAppliedDelta;
        if (!stepDelta.ApproxEqual(Vec3::Zero(), 0.0001f))
        {
            changed = m_MapDocument->TranslateSelectedVertex(stepDelta, false);
            if (changed)
                m_MapDragAppliedDelta = targetDelta;
        }
    }
    else if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0)
    {
        m_MapDragAccumulatedDelta += rawDelta;
        const Vec3 targetDelta =
            m_MapTranslationSnapEnabled ? SnapVector(m_MapDragAccumulatedDelta, m_MapTranslationSnapStep) : m_MapDragAccumulatedDelta;
        const Vec3 stepDelta = targetDelta - m_MapDragAppliedDelta;
        if (!stepDelta.ApproxEqual(Vec3::Zero(), 0.0001f))
        {
            changed = m_MapDocument->TranslateSelectedEdge(stepDelta, false);
            if (changed)
                m_MapDragAppliedDelta = targetDelta;
        }
    }
    else if (selection.faceIndex >= 0)
    {
        const MapBrush* brush = m_MapDocument->GetSelectedBrush();
        const MapFace* face = m_MapDocument->GetSelectedFace();
        if (brush && face)
        {
            const Vec3 faceNormal = ComputeMapFaceNormal(*brush, *face);
            m_MapDragAccumulatedFaceDistance += Vec3::Dot(rawDelta, faceNormal);
            const float targetDistance = m_MapTranslationSnapEnabled
                                             ? SnapScalar(m_MapDragAccumulatedFaceDistance, m_MapTranslationSnapStep)
                                             : m_MapDragAccumulatedFaceDistance;
            const float stepDistance = targetDistance - m_MapDragAppliedFaceDistance;
            if (std::abs(stepDistance) > 0.0001f)
            {
                changed = m_MapDocument->TranslateSelectedFace(stepDistance, false);
                if (changed)
                    m_MapDragAppliedFaceDistance = targetDistance;
            }
        }
    }
    else if (selection.brushId != 0)
    {
        m_MapDragAccumulatedDelta += rawDelta;
        const Vec3 targetDelta =
            m_MapTranslationSnapEnabled ? SnapVector(m_MapDragAccumulatedDelta, m_MapTranslationSnapStep) : m_MapDragAccumulatedDelta;
        const Vec3 stepDelta = targetDelta - m_MapDragAppliedDelta;
        if (!stepDelta.ApproxEqual(Vec3::Zero(), 0.0001f))
        {
            changed = m_MapDocument->TranslateSelectedBrush(stepDelta, false);
            if (changed)
                m_MapDragAppliedDelta = targetDelta;
        }
    }

    if (changed)
        UpdateMapGizmoPlacement();
    return changed;
}

void ViewportPanel::EndMapGizmoDrag()
{
    if (!m_MapDocument)
        return;

    Gizmo* gizmo = GetMapGizmo();
    gizmo->EndDrag();
    m_MouseDragging = false;

    if (m_MapDocument->GetRevision() == m_MapDragStartRevision)
        return;

    m_MapDocument->RebuildCompiledData(true);

    CommandRegistry::Get().PushCommand(std::make_unique<MapSnapshotCommand>(
        m_MapDocument, GetMapDragCommandName(m_MapDragStartSelection), m_MapDragStartAsset, m_MapDragStartSelections,
        m_MapDragStartSelection, m_MapDragStartHiddenBrushIds, m_MapDragStartLockedBrushIds,
        m_MapDragStartDirty, m_MapDocument->GetAsset(), m_MapDocument->GetSelections(), m_MapDocument->GetSelection(),
        m_MapDocument->GetHiddenBrushIds(), m_MapDocument->GetLockedBrushIds(), m_MapDocument->IsDirty()));
}

void ViewportPanel::ProcessInput()
{
    SyncSelectionFromContext();
    // Process input before rendering to ensure transforms are up-to-date
    HandleMouseInput();
}

void ViewportPanel::RequestReflectionProbeBake(Entity entity)
{
    if (!entity.IsValid())
        return;

    if (entity == m_PendingReflectionProbeBakeEntity || entity == m_ReflectionProbeBakeReadbackEntity)
        return;

    if (std::find(m_QueuedReflectionProbeBakes.begin(), m_QueuedReflectionProbeBakes.end(), entity) !=
        m_QueuedReflectionProbeBakes.end())
    {
        return;
    }

    m_QueuedReflectionProbeBakes.push_back(entity);
}

uint32_t ViewportPanel::ClearAutomaticReflectionProbes()
{
    if (!m_World)
        return 0;

    std::vector<Entity> probesToDestroy;
    m_World->Each<ReflectionProbeComponent, NameComponent>(
        [&probesToDestroy](Entity entity, ReflectionProbeComponent&, NameComponent& name)
        {
            if (IsAutoReflectionProbeName(name.name))
                probesToDestroy.push_back(entity);
        });

    for (Entity entity : probesToDestroy)
    {
        if (m_PendingReflectionProbeBakeEntity == entity)
            m_PendingReflectionProbeBakeEntity = kNullEntity;
        if (m_ReflectionProbeBakeReadbackEntity == entity)
            m_ReflectionProbeBakeReadbackEntity = kNullEntity;

        m_QueuedReflectionProbeBakes.erase(
            std::remove(m_QueuedReflectionProbeBakes.begin(), m_QueuedReflectionProbeBakes.end(), entity),
            m_QueuedReflectionProbeBakes.end());

        m_World->DestroyEntity(entity);
    }

    if (!probesToDestroy.empty() && m_SceneContext)
    {
        m_SceneContext->SetSceneDirty(true);
        m_SceneContext->SetForceSceneDirtyCheck(true);
    }

    return static_cast<uint32_t>(probesToDestroy.size());
}

ReflectionProbeAutoGenerateResult ViewportPanel::RegenerateAutomaticReflectionProbes()
{
    ReflectionProbeAutoGenerateResult result;
    result.removedProbeCount = ClearAutomaticReflectionProbes();

    if (!m_World || !m_Renderer)
        return result;

    World& world = *m_World;
    std::unordered_map<AutoReflectionProbeCellKey, AutoReflectionProbeCluster, AutoReflectionProbeCellKeyHash> clusters;
    clusters.reserve(64);

    auto addBoundsToCluster = [&clusters, &result](float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
    {
        const float sizeX = maxX - minX;
        const float sizeY = maxY - minY;
        const float sizeZ = maxZ - minZ;
        if (sizeX <= 0.001f || sizeY <= 0.001f || sizeZ <= 0.001f)
            return;

        const float centerX = (minX + maxX) * 0.5f;
        const float centerY = (minY + maxY) * 0.5f;
        const float centerZ = (minZ + maxZ) * 0.5f;
        const AutoReflectionProbeCellKey key = {
            static_cast<int>(std::floor(centerX / kAutoReflectionProbeGridSize)),
            static_cast<int>(std::floor(centerY / kAutoReflectionProbeGridSize)),
            static_cast<int>(std::floor(centerZ / kAutoReflectionProbeGridSize)),
        };
        ExpandAutoReflectionProbeCluster(clusters[key], minX, minY, minZ, maxX, maxY, maxZ);
        ++result.sourceBoundsCount;
    };

    std::unordered_map<std::string, std::vector<PrimitiveMesh*>> frameMeshCache;
    frameMeshCache.reserve(128);
    auto getMeshesForPath = [this, &frameMeshCache](const std::string& meshPath) -> const std::vector<PrimitiveMesh*>&
    {
        auto it = frameMeshCache.find(meshPath);
        if (it != frameMeshCache.end())
            return it->second;

        auto inserted = frameMeshCache.emplace(meshPath, m_Renderer->LoadMesh(meshPath));
        return inserted.first->second;
    };

    world.Each<TransformComponent, PrimitiveComponent>(
        [this, &world, &addBoundsToCluster](Entity entity, TransformComponent& transform, PrimitiveComponent& primitive)
        {
            if (!MatchesRenderMask(world, entity, RenderLayerMask::World))
                return;

            ResolveSceneTransforms(world, entity);
            PrimitiveMesh* mesh = m_Renderer->GetPrimitiveMesh(primitive.type);
            if (!mesh || mesh->indexCount == 0)
                return;

            float minX = 0.0f;
            float minY = 0.0f;
            float minZ = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            float maxZ = 0.0f;
            ComputeWorldBoundsForDiagnostics(transform, *mesh, minX, minY, minZ, maxX, maxY, maxZ);
            addBoundsToCluster(minX, minY, minZ, maxX, maxY, maxZ);
        });

    world.Each<TransformComponent, MeshComponent>(
        [&world, &getMeshesForPath, &addBoundsToCluster](Entity entity, TransformComponent& transform, MeshComponent& meshComp)
        {
            if (!MatchesRenderMask(world, entity, RenderLayerMask::World) || meshComp.meshPath.empty())
                return;

            ResolveSceneTransforms(world, entity);
            const std::vector<PrimitiveMesh*>& meshes = getMeshesForPath(meshComp.meshPath);
            if (meshes.empty())
                return;

            size_t startIdx = 0;
            size_t endIdx = meshes.size();
            if (meshComp.submeshIndex >= 0 && static_cast<size_t>(meshComp.submeshIndex) < meshes.size())
            {
                startIdx = static_cast<size_t>(meshComp.submeshIndex);
                endIdx = startIdx + 1;
            }

            bool initialized = false;
            float minX = 0.0f;
            float minY = 0.0f;
            float minZ = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            float maxZ = 0.0f;
            for (size_t meshIndex = startIdx; meshIndex < endIdx; ++meshIndex)
            {
                PrimitiveMesh* mesh = meshes[meshIndex];
                if (!mesh || mesh->indexCount == 0)
                    continue;

                float meshMinX = 0.0f;
                float meshMinY = 0.0f;
                float meshMinZ = 0.0f;
                float meshMaxX = 0.0f;
                float meshMaxY = 0.0f;
                float meshMaxZ = 0.0f;
                ComputeWorldBoundsForDiagnostics(transform, *mesh, meshMinX, meshMinY, meshMinZ, meshMaxX, meshMaxY,
                                                 meshMaxZ);
                if (!initialized)
                {
                    minX = meshMinX;
                    minY = meshMinY;
                    minZ = meshMinZ;
                    maxX = meshMaxX;
                    maxY = meshMaxY;
                    maxZ = meshMaxZ;
                    initialized = true;
                    continue;
                }

                minX = (std::min)(minX, meshMinX);
                minY = (std::min)(minY, meshMinY);
                minZ = (std::min)(minZ, meshMinZ);
                maxX = (std::max)(maxX, meshMaxX);
                maxY = (std::max)(maxY, meshMaxY);
                maxZ = (std::max)(maxZ, meshMaxZ);
            }

            if (initialized)
                addBoundsToCluster(minX, minY, minZ, maxX, maxY, maxZ);
        });

    for (const MapPreviewPart& part : m_MapPreviewParts)
    {
        if (!part.mesh || part.mesh->indexCount == 0)
            continue;

        TransformComponent identityTransform;
        identityTransform.worldMatrix = Mat4::Identity();

        float minX = 0.0f;
        float minY = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        float maxZ = 0.0f;
        ComputeWorldBoundsForDiagnostics(identityTransform, *part.mesh, minX, minY, minZ, maxX, maxY, maxZ);
        addBoundsToCluster(minX, minY, minZ, maxX, maxY, maxZ);
    }

    std::vector<std::pair<AutoReflectionProbeCellKey, AutoReflectionProbeCluster>> sortedClusters;
    sortedClusters.reserve(clusters.size());
    for (const auto& entry : clusters)
    {
        if (entry.second.initialized)
            sortedClusters.push_back(entry);
    }

    std::sort(sortedClusters.begin(), sortedClusters.end(),
              [](const auto& a, const auto& b)
              {
                  if (a.first.z != b.first.z)
                      return a.first.z < b.first.z;
                  if (a.first.y != b.first.y)
                      return a.first.y < b.first.y;
                  return a.first.x < b.first.x;
              });

    uint32_t probeIndex = 1;
    for (const auto& entry : sortedClusters)
    {
        const AutoReflectionProbeCluster& cluster = entry.second;
        const float centerX = (cluster.minX + cluster.maxX) * 0.5f;
        const float centerY = (cluster.minY + cluster.maxY) * 0.5f;
        const float centerZ = (cluster.minZ + cluster.maxZ) * 0.5f;
        const float halfX = (cluster.maxX - cluster.minX) * 0.5f + kAutoReflectionProbePadding;
        const float halfY = (cluster.maxY - cluster.minY) * 0.5f + kAutoReflectionProbePadding;
        const float halfZ = (cluster.maxZ - cluster.minZ) * 0.5f + kAutoReflectionProbePadding;

        Entity probeEntity = world.CreateEntity();
        world.AddComponent<NameComponent>(probeEntity).name =
            std::string(kAutoReflectionProbeNamePrefix) + " " + std::to_string(probeIndex++);

        auto& transform = world.AddComponent<TransformComponent>(probeEntity);
        transform.position = {centerX, centerY, centerZ};
        transform.rotation = {0.0f, 0.0f, 0.0f};
        transform.scale = {1.0f, 1.0f, 1.0f};
        world.AddComponent<HierarchyComponent>(probeEntity);

        auto& probe = world.AddComponent<ReflectionProbeComponent>(probeEntity);
        probe.sourceMode = ReflectionProbeSourceMode::AutoSceneSkybox;
        probe.intensity = 1.0f;
        probe.falloff = 0.22f;
        probe.radius = (std::max)({halfX, halfY, halfZ, kAutoReflectionProbeMinHalfExtent});
        probe.boxExtents = {(std::max)(halfX, kAutoReflectionProbeMinHalfExtent),
                            (std::max)(halfY, kAutoReflectionProbeMinHalfExtent),
                            (std::max)(halfZ, kAutoReflectionProbeMinHalfExtent)};
        probe.enabled = true;

        RequestReflectionProbeBake(probeEntity);
        ++result.createdProbeCount;
        ++result.queuedBakeCount;
    }

    if ((result.removedProbeCount > 0 || result.createdProbeCount > 0) && m_SceneContext)
    {
        m_SceneContext->SetSceneDirty(true);
        m_SceneContext->SetForceSceneDirtyCheck(true);
    }

    return result;
}

void ViewportPanel::FinalizePendingReflectionProbeBake()
{
    if (!m_ReflectionProbeBakeReadbackBuffer || m_ReflectionProbeBakeRelativePath.empty() || m_ReflectionProbeBakeResolution == 0)
        return;

    auto clearBakeState = [this]()
    {
        m_ReflectionProbeBakeReadbackBuffer.reset();
        m_ReflectionProbeBakeReadbackEntity = kNullEntity;
        m_ReflectionProbeBakeRelativePath.clear();
        m_ReflectionProbeBakeResolution = 0;
        m_ReflectionProbeBakeRowPitch = 0;
        m_ReflectionProbeBakeFaceOffsets.fill(0ull);
    };

    void* mappedData = m_ReflectionProbeBakeReadbackBuffer->Map();
    if (!mappedData)
    {
        clearBakeState();
        return;
    }

    const uint8_t* baseBytes = static_cast<const uint8_t*>(mappedData);
    const std::filesystem::path assetRoot = AssetManager::Get().GetRootPath();
    bool wroteAllFaces = !assetRoot.empty();
    for (size_t faceIndex = 0; faceIndex < kReflectionProbeFaceCount && wroteAllFaces; ++faceIndex)
    {
        const std::filesystem::path facePath =
            assetRoot / m_ReflectionProbeBakeRelativePath / GetReflectionProbeFaceFileName(faceIndex);
        const uint8_t* faceBytes = baseBytes + m_ReflectionProbeBakeFaceOffsets[faceIndex];
        wroteAllFaces = WriteRgbaPngFile(facePath, faceBytes, m_ReflectionProbeBakeResolution,
                                         m_ReflectionProbeBakeResolution, m_ReflectionProbeBakeRowPitch);
    }
    m_ReflectionProbeBakeReadbackBuffer->Unmap();

    if (wroteAllFaces && m_World && m_World->IsAlive(m_ReflectionProbeBakeReadbackEntity))
    {
        if (ReflectionProbeComponent* probe = m_World->GetComponent<ReflectionProbeComponent>(m_ReflectionProbeBakeReadbackEntity))
        {
            probe->sourceMode = ReflectionProbeSourceMode::ManualCubemap;
            probe->cubemapPath = m_ReflectionProbeBakeRelativePath;
            if (m_SceneContext)
            {
                m_SceneContext->SetSceneDirty(true);
                m_SceneContext->SetForceSceneDirtyCheck(true);
            }
        }
    }

    clearBakeState();
}

bool ViewportPanel::EnsureReflectionProbeBakeResources(uint32 resolution)
{
    if (resolution == 0 || !m_Device)
        return false;

    if (m_ReflectionProbeBakeColorTarget && m_ReflectionProbeBakeDepthTarget &&
        m_ReflectionProbeBakeColorTarget->GetWidth() == resolution &&
        m_ReflectionProbeBakeColorTarget->GetHeight() == resolution)
    {
        return true;
    }

    RHITextureDesc colorDesc = {};
    colorDesc.width = resolution;
    colorDesc.height = resolution;
    colorDesc.format = RHIFormat::R8G8B8A8_UNORM;
    colorDesc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
    colorDesc.debugName = "ReflectionProbeBakeColor";

    RHITextureDesc depthDesc = {};
    depthDesc.width = resolution;
    depthDesc.height = resolution;
    depthDesc.format = RHIFormat::D32_FLOAT;
    depthDesc.usage = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "ReflectionProbeBakeDepth";

    m_ReflectionProbeBakeColorTarget = m_Device->CreateTexture(colorDesc);
    m_ReflectionProbeBakeDepthTarget = m_Device->CreateTexture(depthDesc);
    return m_ReflectionProbeBakeColorTarget != nullptr && m_ReflectionProbeBakeDepthTarget != nullptr;
}

void ViewportPanel::ExecutePendingReflectionProbeBake(RHISwapChain* swapChain)
{
    (void)swapChain;
    if (!m_PendingReflectionProbeBakeEntity.IsValid() || !m_World || !m_Renderer || !m_Device)
        return;
    if (m_ReflectionProbeBakeReadbackBuffer)
        return;

    World& world = *m_World;
    if (!world.IsAlive(m_PendingReflectionProbeBakeEntity))
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    ResolveSceneTransforms(world, m_PendingReflectionProbeBakeEntity);

    TransformComponent* probeTransform = world.GetComponent<TransformComponent>(m_PendingReflectionProbeBakeEntity);
    ReflectionProbeComponent* probe = world.GetComponent<ReflectionProbeComponent>(m_PendingReflectionProbeBakeEntity);
    if (!probeTransform || !probe)
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    constexpr uint32_t captureResolution = kReflectionProbeBakeResolution;
    if (!EnsureReflectionProbeBakeResources(captureResolution))
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    auto* d3dDevice = static_cast<D3D12Device*>(m_Device);
    auto* d3dColorTexture = dynamic_cast<D3D12Texture*>(m_ReflectionProbeBakeColorTarget.get());
    auto* d3dReadbackTexture = d3dColorTexture ? d3dColorTexture->GetResource() : nullptr;
    if (!d3dDevice || !d3dColorTexture || !d3dReadbackTexture)
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    const D3D12_RESOURCE_DESC colorDesc = d3dReadbackTexture->GetDesc();
    d3dDevice->GetDevice()->GetCopyableFootprints(&colorDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes);
    (void)numRows;
    (void)rowSizeBytes;
    if (footprint.Footprint.RowPitch == 0 || totalBytes == 0)
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    std::array<uint64_t, kReflectionProbeFaceCount> faceOffsets = {};
    uint64_t readbackSize = 0;
    for (size_t faceIndex = 0; faceIndex < kReflectionProbeFaceCount; ++faceIndex)
    {
        readbackSize = AlignUpUint64(readbackSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        faceOffsets[faceIndex] = readbackSize;
        readbackSize += totalBytes;
    }

    RHIBufferDesc readbackDesc = {};
    readbackDesc.size = static_cast<usize>(readbackSize);
    readbackDesc.usage = RHIBufferUsage::Storage;
    readbackDesc.memory = RHIMemoryUsage::GPU_To_CPU;
    readbackDesc.debugName = "ReflectionProbeBakeReadback";
    RHIBufferPtr readbackBuffer = m_Device->CreateBuffer(readbackDesc);
    auto* d3dReadbackBuffer = readbackBuffer ? dynamic_cast<D3D12Buffer*>(readbackBuffer.get()) : nullptr;
    if (!readbackBuffer || !d3dReadbackBuffer || !d3dReadbackBuffer->GetResource())
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    const Vec3 probePosition = GetWorldPosition(*probeTransform);
    const float probeRotationDegrees = probeTransform->rotation.y;
    Camera captureCamera;
    captureCamera.SetPerspective(90.0f, 1.0f, 0.05f, std::max(probe->radius * 2.0f, 64.0f));
    captureCamera.SetPosition(probePosition.x, probePosition.y, probePosition.z);

    const SceneSettingsAsset* sceneSettings = m_SceneContext ? &m_SceneContext->GetSceneSettings() : nullptr;
    const ResolvedSkyboxSettings resolvedSkybox = ResolveSkyboxSettings(&world, sceneSettings);
    const Mat4 mapWorld = Mat4::Identity();
    const std::filesystem::path assetRoot = AssetManager::Get().GetRootPath();
    if (assetRoot.empty())
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    std::unordered_map<std::string, std::vector<PrimitiveMesh*>> frameMeshCache;
    frameMeshCache.reserve(128);
    auto getMeshesForPath = [this, &frameMeshCache](const std::string& meshPath) -> const std::vector<PrimitiveMesh*>&
    {
        auto it = frameMeshCache.find(meshPath);
        if (it != frameMeshCache.end())
            return it->second;

        auto loaded = m_Renderer->LoadMesh(meshPath);
        auto inserted = frameMeshCache.emplace(meshPath, std::move(loaded));
        return inserted.first->second;
    };

    auto resolveMaterialDataFromPath = [this](const std::string& materialPath, MaterialData& matData) -> bool
    {
        if (materialPath.empty())
            return false;

        std::string fullPath;
        auto resolvedPathIt = m_ResolvedMaterialPathCache.find(materialPath);
        if (resolvedPathIt != m_ResolvedMaterialPathCache.end())
        {
            fullPath = resolvedPathIt->second;
        }
        else
        {
            fullPath = materialPath;
            const std::string candidate = AssetManager::Get().GetFullPath(materialPath);
            if (!candidate.empty())
                fullPath = candidate;
            else
            {
                std::filesystem::path assetsPath = std::filesystem::path(AssetManager::Get().GetRootPath()) / materialPath;
                if (std::filesystem::exists(assetsPath))
                    fullPath = assetsPath.string();
            }
            m_ResolvedMaterialPathCache[materialPath] = fullPath;
        }

        auto runtimeIt = m_MaterialRuntimeCache.find(fullPath);
        if (runtimeIt == m_MaterialRuntimeCache.end())
        {
            CachedMaterialRuntime cached;
            cached.loaded = MaterialLoader::Load(fullPath);
            if (cached.loaded.valid)
            {
                MaterialGraph graph;
                if (graph.LoadFromFile(fullPath))
                {
                    std::string hlsl = graph.GenerateHLSL();
                    if (!hlsl.empty())
                        cached.shaderId = m_Renderer->RegisterMaterialShader(hlsl);
                }
            }

            runtimeIt = m_MaterialRuntimeCache.emplace(fullPath, std::move(cached)).first;
        }

        const CachedMaterialRuntime& cached = runtimeIt->second;
        if (!cached.loaded.valid)
            return false;

        matData.colorR = cached.loaded.baseColor.x;
        matData.colorG = cached.loaded.baseColor.y;
        matData.colorB = cached.loaded.baseColor.z;
        matData.metallic = cached.loaded.metallic;
        matData.roughness = cached.loaded.roughness;
        matData.ambientOcclusion = cached.loaded.ambientOcclusion;

        for (int i = 0; i < 4; ++i)
        {
            if (cached.loaded.hasTextures[i] && !cached.loaded.texturePaths[i].empty())
                matData.texturePaths[i] = cached.loaded.texturePaths[i];
            matData.textureSampleTypes[i] = cached.loaded.textureSampleTypes[i];
        }

        matData.albedoTextureSlot = cached.loaded.albedoTextureSlot;
        matData.normalTextureSlot = cached.loaded.normalTextureSlot;
        matData.ormTextureSlot = cached.loaded.ormTextureSlot;
        matData.tilingU = cached.loaded.tilingU;
        matData.tilingV = cached.loaded.tilingV;
        matData.offsetU = cached.loaded.offsetU;
        matData.offsetV = cached.loaded.offsetV;
        matData.filterMode = cached.loaded.filterMode;
        matData.wrapMode = cached.loaded.wrapMode;
        matData.pannerSpeedU = cached.loaded.pannerSpeedU;
        matData.pannerSpeedV = cached.loaded.pannerSpeedV;
        matData.pannerMethod = cached.loaded.pannerMethod;
        matData.pannerLink = cached.loaded.pannerLink;
        matData.materialShaderId = cached.shaderId;
        return true;
    };

    auto buildMaterialData = [&resolveMaterialDataFromPath](const MaterialComponent* material,
                                                            const PrimitiveMesh* mesh) -> MaterialData
    {
        MaterialData matData;
        matData.emissiveColorR = 0.0f;
        matData.emissiveColorG = 0.0f;
        matData.emissiveColorB = 0.0f;
        matData.emissiveStrength = 0.0f;

        if (material && material->useMaterialFile && !material->materialPath.empty())
        {
            if (resolveMaterialDataFromPath(material->materialPath, matData))
                return matData;
        }

        if (material)
        {
            matData.colorR = material->baseColor.x;
            matData.colorG = material->baseColor.y;
            matData.colorB = material->baseColor.z;
            matData.metallic = material->metallic;
            matData.roughness = material->roughness;
            matData.emissiveColorR = material->emissiveColor.x;
            matData.emissiveColorG = material->emissiveColor.y;
            matData.emissiveColorB = material->emissiveColor.z;
            matData.emissiveStrength = material->emissiveStrength;
            return matData;
        }

        if (mesh)
        {
            for (const Submesh& submesh : mesh->submeshes)
            {
                if (resolveMaterialDataFromPath(submesh.materialPath, matData))
                    break;
            }
        }

        return matData;
    };

    const std::string bakeFolderName = BuildReflectionProbeBakeFolderName(world, m_PendingReflectionProbeBakeEntity);
    const std::string relativeBakePath = (std::filesystem::path("Cubemaps") / "Baked" / bakeFolderName).generic_string();

    ViewSettings& viewSettings = ViewSettings::Get();
    const DebugVisMode previousDebugMode = viewSettings.debugVisMode;
    viewSettings.debugVisMode = DebugVisMode::Lit;
    m_Renderer->SetSceneCaptureMode(true);
    m_Renderer->ClearReflectionProbeData();

    bool captureSucceeded = true;
    auto failCapture = [&]()
    {
        captureSucceeded = false;
    };

    ID3D12GraphicsCommandList* commandList = d3dDevice->GetCommandList();
    if (!commandList)
    {
        viewSettings.debugVisMode = previousDebugMode;
        m_Renderer->SetSceneCaptureMode(false);
        m_Renderer->ClearReflectionProbeData();
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    for (size_t faceIndex = 0; faceIndex < kReflectionProbeFaceCount && captureSucceeded; ++faceIndex)
    {
        Vec3 faceDirection = Vec3::UnitZ();
        Vec3 faceUp = Vec3::UnitY();
        GetReflectionProbeFaceBasis(faceIndex, faceDirection, faceUp);
        faceDirection = RotateVectorYDegrees(faceDirection, probeRotationDegrees);
        faceUp = RotateVectorYDegrees(faceUp, probeRotationDegrees);
        captureCamera.LookAtWithUp(probePosition.x + faceDirection.x, probePosition.y + faceDirection.y,
                                   probePosition.z + faceDirection.z, faceUp.x, faceUp.y, faceUp.z);

        m_Device->TransitionTexture(m_ReflectionProbeBakeColorTarget, RHIResourceState::Unknown, RHIResourceState::RenderTarget);
        m_Device->TransitionTexture(m_ReflectionProbeBakeDepthTarget, RHIResourceState::Unknown, RHIResourceState::DepthWrite);

        if (!m_Renderer->BeginExternalRenderTarget(m_ReflectionProbeBakeColorTarget, m_ReflectionProbeBakeDepthTarget, 0.0f,
                                                   0.0f, 0.0f, 1.0f))
        {
            failCapture();
            break;
        }

        if (resolvedSkybox.HasVisibleSkybox())
        {
            m_Renderer->RenderSkybox(captureCamera, nullptr, resolvedSkybox.cubemapPath, resolvedSkybox.wrapMode,
                                     resolvedSkybox.rotation, resolvedSkybox.showMarkers, resolvedSkybox.tintR * 0.5f,
                                     resolvedSkybox.tintG * 0.7f, resolvedSkybox.tintB * 1.0f,
                                     resolvedSkybox.tintR * 0.8f, resolvedSkybox.tintG * 0.8f,
                                     resolvedSkybox.tintB * 0.85f);
        }

        world.Each<TransformComponent, PrimitiveComponent>(
            [this, &world, &captureCamera, &buildMaterialData](Entity entity, TransformComponent& transform,
                                                               PrimitiveComponent& primitive)
            {
                if (!MatchesRenderMask(world, entity, RenderLayerMask::World))
                    return;

                PrimitiveMesh* mesh = m_Renderer->GetPrimitiveMesh(primitive.type);
                if (!mesh || mesh->indexCount == 0)
                    return;

                MaterialComponent* material = world.GetComponent<MaterialComponent>(entity);
                MaterialData matData = buildMaterialData(material, mesh);
                m_Renderer->ClearReflectionProbeData();
                m_Renderer->SetMaterialData(matData);
                m_Renderer->RenderMesh(captureCamera, nullptr, transform.worldMatrix.Data(), *mesh, matData);
            });

        world.Each<TransformComponent, MeshComponent>(
            [this, &world, &captureCamera, &getMeshesForPath, &buildMaterialData](Entity entity,
                                                                                   TransformComponent& transform,
                                                                                   MeshComponent& meshComp)
            {
                if (!MatchesRenderMask(world, entity, RenderLayerMask::World) || meshComp.meshPath.empty())
                    return;

                MaterialComponent* material = world.GetComponent<MaterialComponent>(entity);
                const auto& meshes = getMeshesForPath(meshComp.meshPath);
                size_t startIdx = 0;
                size_t endIdx = meshes.size();
                if (meshComp.submeshIndex >= 0 && static_cast<size_t>(meshComp.submeshIndex) < meshes.size())
                {
                    startIdx = static_cast<size_t>(meshComp.submeshIndex);
                    endIdx = startIdx + 1;
                }

                for (size_t meshIndex = startIdx; meshIndex < endIdx; ++meshIndex)
                {
                    PrimitiveMesh* mesh = meshes[meshIndex];
                    if (!mesh || mesh->indexCount == 0)
                        continue;

                    MaterialData matData = buildMaterialData(material, mesh);
                    m_Renderer->ClearReflectionProbeData();
                    m_Renderer->SetMaterialData(matData);
                    m_Renderer->RenderMesh(captureCamera, nullptr, transform.worldMatrix.Data(), *mesh, matData);
                }
            });

        for (const MapPreviewPart& part : m_MapPreviewParts)
        {
            if (!part.mesh || part.mesh->indexCount == 0)
                continue;

            m_Renderer->ClearReflectionProbeData();
            m_Renderer->SetMaterialData(part.material);
            m_Renderer->RenderMesh(captureCamera, nullptr, mapWorld.Data(), *part.mesh, part.material);
        }

        m_Renderer->EndExternalRenderTarget();

        m_Device->TransitionTexture(m_ReflectionProbeBakeColorTarget, RHIResourceState::Unknown, RHIResourceState::CopySource);
        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = d3dColorTexture->GetResource();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = d3dReadbackBuffer->GetResource();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint = footprint;
        dstLocation.PlacedFootprint.Offset = faceOffsets[faceIndex];
        commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
    }

    viewSettings.debugVisMode = previousDebugMode;
    m_Renderer->SetSceneCaptureMode(false);
    m_Renderer->ClearReflectionProbeData();

    if (!captureSucceeded)
    {
        m_PendingReflectionProbeBakeEntity = kNullEntity;
        return;
    }

    m_ReflectionProbeBakeReadbackBuffer = readbackBuffer;
    m_ReflectionProbeBakeReadbackEntity = m_PendingReflectionProbeBakeEntity;
    m_ReflectionProbeBakeRelativePath = relativeBakePath;
    m_ReflectionProbeBakeResolution = captureResolution;
    m_ReflectionProbeBakeRowPitch = footprint.Footprint.RowPitch;
    m_ReflectionProbeBakeFaceOffsets = faceOffsets;
    m_PendingReflectionProbeBakeEntity = kNullEntity;
}

void ViewportPanel::RenderScene(RHISwapChain* swapChain)
{
    if (!m_Initialized || !m_Renderer || !swapChain || !m_Device)
        return;

    FinalizePendingReflectionProbeBake();
    if (!m_ReflectionProbeBakeReadbackBuffer && !m_PendingReflectionProbeBakeEntity.IsValid())
    {
        while (!m_QueuedReflectionProbeBakes.empty())
        {
            const Entity nextProbe = m_QueuedReflectionProbeBakes.front();
            m_QueuedReflectionProbeBakes.pop_front();
            if (m_World && m_World->IsAlive(nextProbe))
            {
                m_PendingReflectionProbeBakeEntity = nextProbe;
                break;
            }
        }
    }
    SyncSelectionFromContext();
    RebuildMapPreviewIfNeeded();

    if (m_NavigationSystem && !m_PlayMode)
    {
        m_NavigationSystem->EnsureNavMeshUpToDate();
    }

    // Update camera aspect ratio
    float aspect = static_cast<float>(swapChain->GetWidth()) / static_cast<float>(swapChain->GetHeight());
    m_Camera.SetAspectRatio(aspect);

    // Reset frame graph for this frame
    m_FrameGraph.Reset();

    // Capture state for lambda
    bool playMode = m_PlayMode;
    World* world = m_World.get();
    SimpleRenderer* renderer = m_Renderer.get();
    const std::vector<MapPreviewPart>* mapPreviewParts = &m_MapPreviewParts;
    GizmoRenderer* gizmoRenderer = m_GizmoRenderer.get();
    GizmoRenderer* overlayGizmoRenderer = m_OverlayGizmoRenderer.get();
    GridRenderer* gridRenderer = m_GridRenderer.get();
    Entity selectedEntity = m_SelectedEntity;
    TransformSpace transformSpace = m_TransformSpace;
    Gizmo* activeGizmo = GetActiveGizmo();
    const auto& editorSettings = EditorSettings::Get();
    const auto& physicsSettings = PhysicsSettings::Get();
    const bool showSelectionGizmo = editorSettings.showSelectionGizmo;
    const bool showLightGizmos = editorSettings.showLightGizmos;
    const bool showCameraFrustums = editorSettings.showCameraFrustums;
    const bool showAttachmentSockets = editorSettings.showAttachmentSockets;
    const bool showNavMeshGizmo = editorSettings.showNavMeshGizmo;
    const bool showColliderGizmos = physicsSettings.showColliders;
    const bool showContactPoints = physicsSettings.showContactPoints;
    const DebugVisMode debugVisMode = ViewSettings::Get().debugVisMode;
    const std::vector<NavigationDebugLine> navMeshDebugLines =
        (m_NavigationSystem && showNavMeshGizmo && !playMode) ? m_NavigationSystem->GetDebugLines() : std::vector<NavigationDebugLine>{};
    std::vector<PhysicsContactDebugPoint> physicsContactPoints;
    auto* resolvedMaterialPathCache = &m_ResolvedMaterialPathCache;
    auto* materialRuntimeCache = &m_MaterialRuntimeCache;
    if (m_PhysicsSystem && showContactPoints)
    {
        for (const CollisionPair& pair : m_PhysicsSystem->GetCollisionPairs())
        {
            for (int contactIndex = 0; contactIndex < pair.manifold.numContacts; ++contactIndex)
            {
                const ContactPoint& contact = pair.manifold.contacts[contactIndex];
                physicsContactPoints.push_back({contact.point, contact.normal});
            }
        }
    }
    const bool mapEditingEnabled = m_MapEditingEnabled;
    const bool mapClipToolActive = m_MapClipToolActive;
    const bool mapExtrudeToolActive = m_MapExtrudeToolActive;
    const bool mapHollowPreviewActive = m_MapHollowPreviewActive;
    const float mapClipPreviewOffset = m_MapClipPreviewOffset;
    const bool mapClipPreviewFlipPlane = m_MapClipPreviewFlipPlane;
    const float mapExtrudePreviewDistance = m_MapExtrudePreviewDistance;
    const float mapHollowPreviewThickness = m_MapHollowPreviewThickness;
    const bool mapSelectionValid = HasMapSelection();
    Gizmo* mapGizmo = GetMapGizmo();
    Vec3 mapGizmoPivot = Vec3::Zero();
    if (mapEditingEnabled && mapSelectionValid && !mapClipToolActive && !mapExtrudeToolActive && !mapHollowPreviewActive)
    {
        mapGizmoPivot = m_MapDocument->GetSelectionPivot();
        mapGizmo->SetPosition(mapGizmoPivot.x, mapGizmoPivot.y, mapGizmoPivot.z);
        mapGizmo->SetSize(1.0f);
        mapGizmo->SetLocalSpace(false);
        mapGizmo->SetRotation(0.0f, 0.0f, 0.0f);
    }

    Entity activeCameraEntity = kNullEntity;
    CameraComponent* activeSceneCamera = nullptr;
    const SceneSettingsAsset* sceneSettings = m_SceneContext ? &m_SceneContext->GetSceneSettings() : nullptr;
    const ResolvedSkyboxSettings resolvedSkybox =
        world ? ResolveSkyboxSettings(world, sceneSettings) : ResolveSkyboxSettings(nullptr, sceneSettings);
    if (world)
    {
        activeCameraEntity = FindActiveCameraEntity(*world);
        ResolveSceneTransforms(*world, activeCameraEntity);
        if (activeCameraEntity.IsValid())
            activeSceneCamera = world->GetComponent<CameraComponent>(activeCameraEntity);
    }

    // In play mode, update m_PlayCamera from the resolved active scene camera transform
    if (playMode && world)
    {
        m_PlayCamera = m_Camera; // Default to editor camera
        if (activeCameraEntity.IsValid())
        {
            if (TransformComponent* transform = world->GetComponent<TransformComponent>(activeCameraEntity))
            {
                const CameraComponent* activeCamera = world->GetComponent<CameraComponent>(activeCameraEntity);
                if (activeCamera)
                {
                    const Vec3 cameraPosition = GetWorldPosition(*transform);
                    const Vec3 cameraForward = GetWorldForward(*transform);
                    m_PlayCamera.SetPerspective(activeCamera->fov, aspect, activeCamera->nearPlane, activeCamera->farPlane);
                    m_PlayCamera.SetPosition(cameraPosition.x, cameraPosition.y, cameraPosition.z);
                    m_PlayCamera.LookAt(cameraPosition.x + cameraForward.x, cameraPosition.y + cameraForward.y,
                                        cameraPosition.z + cameraForward.z);
                }
            }
        }
    }

    // Use appropriate camera for rendering
    Camera* camera = playMode ? &m_PlayCamera : &m_Camera;
    const uint32 sceneRenderMask = (playMode && activeSceneCamera) ? activeSceneCamera->renderMask : 0xFFFFFFFFu;
    const bool enableViewmodelPass =
        playMode && activeSceneCamera && activeSceneCamera->enableViewmodelPass && activeSceneCamera->viewmodelMask != 0;
    const uint32 viewmodelRenderMask = enableViewmodelPass ? activeSceneCamera->viewmodelMask : 0u;
    Camera viewmodelCamera = *camera;
    if (enableViewmodelPass)
    {
        viewmodelCamera.SetPerspective(activeSceneCamera->viewmodelFov, aspect, activeSceneCamera->viewmodelNearPlane,
                                       activeSceneCamera->farPlane);
    }
    float camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    camera->GetPosition(camX, camY, camZ);

    // Render full screen (as requested)
    // We do NOT constrain viewport to panel size anymore

    // NOTE: Grid and editor gizmos are rendered INSIDE the frame graph passes (GizmoPass)
    // to avoid them affecting SSAO and other depth-based post-processing effects.
    // See GizmoPass below for grid rendering.

    const auto& viewSettings = ViewSettings::Get();
    const bool renderShadows = viewSettings.shadowsEnabled;
    const bool renderSSAO =
        viewSettings.ssaoEnabled || viewSettings.debugVisMode == DebugVisMode::AmbientOcclusionOnly;
    const bool showSSAODebugFullscreen = viewSettings.debugVisMode == DebugVisMode::AmbientOcclusionOnly;
    const bool frustumCullingEnabled = viewSettings.frustumCullingEnabled;
    const bool antiAliasingEnabled = viewSettings.antiAliasingEnabled;
    renderer->SetHZBEnabled(viewSettings.hzbEnabled);
    renderer->SetForwardPlusEnabled(viewSettings.forwardPlusEnabled);
    renderer->SetAntiAliasingEnabled(antiAliasingEnabled);

    SSAOSettings ssaoSettings = renderer->GetSSAOSettings();
    ssaoSettings.enabled = renderSSAO;
    ssaoSettings.radius = viewSettings.ssaoRadius;
    ssaoSettings.intensity = viewSettings.ssaoIntensity;
    ssaoSettings.sampleCount = viewSettings.ssaoSampleCount;
    ssaoSettings.halfResolution = viewSettings.ssaoHalfResolution;
    ssaoSettings.blurDepthThreshold = viewSettings.ssaoBlurDepthThreshold;
    renderer->SetSSAOSettings(ssaoSettings);

    if (renderSSAO)
        renderer->CreateSSAOResources(swapChain, swapChain->GetWidth(), swapChain->GetHeight());
    if (renderer->IsHZBEnabled())
        renderer->CreateHZBResources(swapChain->GetWidth(), swapChain->GetHeight());

    const uint32 frameWidth = swapChain->GetWidth();
    const uint32 frameHeight = swapChain->GetHeight();
    FrameGraphTextureHandle backBufferHandle = m_FrameGraph.ImportTexture(
        FrameGraphResourceDesc::ImportedTexture(
            "SwapChainColor", frameWidth, frameHeight, RHIFormat::R8G8B8A8_UNORM,
            RHITextureUsage::RenderTarget | RHITextureUsage::Sampled, RHIResourceState::Present));
    FrameGraphTextureHandle sceneDepthHandle = m_FrameGraph.ImportTexture(
        FrameGraphResourceDesc::ImportedTexture(
            "SceneDepth", frameWidth, frameHeight, RHIFormat::D32_FLOAT,
            RHITextureUsage::DepthStencil | RHITextureUsage::Sampled, RHIResourceState::DepthWrite));

    FrameGraphTextureHandle shadowMapHandle;
    FrameGraphTextureHandle localShadowMapHandle;
    FrameGraphTextureHandle ssaoRawHandle;
    FrameGraphTextureHandle ssaoBlurHandle;
    FrameGraphTextureHandle hzbHandle;
    FrameGraphTextureHandle sceneColorHandle;
    FrameGraphTextureHandle resolvedSceneDepthHandle = sceneDepthHandle;
    FrameGraphTextureHandle finalBackBufferHandle = backBufferHandle;
    FrameGraphTextureHandle presentHandle;

    if (renderShadows)
    {
        if (RHITexturePtr directionalShadow = renderer->GetDirectionalShadowGraphTexture())
        {
            shadowMapHandle = m_FrameGraph.ImportTexture(
                FrameGraphResourceDesc::ImportedTexture(
                    "DirectionalShadowMap", 2048, 2048, RHIFormat::D32_FLOAT,
                    RHITextureUsage::DepthStencil | RHITextureUsage::Sampled, RHIResourceState::ShaderResource),
                directionalShadow);
        }
        if (RHITexturePtr localShadow = renderer->GetLocalShadowGraphTexture())
        {
            localShadowMapHandle = m_FrameGraph.ImportTexture(
                FrameGraphResourceDesc::ImportedTexture(
                    "LocalShadowMapArray", 1024, 1024, RHIFormat::D32_FLOAT,
                    RHITextureUsage::DepthStencil | RHITextureUsage::Sampled, RHIResourceState::ShaderResource),
                localShadow);
        }
    }

    if (renderSSAO)
    {
        const uint32_t ssaoWidth = std::max(renderer->GetSSAOBufferWidth(), 1u);
        const uint32_t ssaoHeight = std::max(renderer->GetSSAOBufferHeight(), 1u);
        if (RHITexturePtr ssaoOcc = renderer->GetSSAOOcclusionGraphTexture())
        {
            ssaoRawHandle = m_FrameGraph.ImportTexture(
                FrameGraphResourceDesc::ImportedTexture(
                    "SSAO.Raw", ssaoWidth, ssaoHeight, RHIFormat::R8_UNORM,
                    RHITextureUsage::RenderTarget | RHITextureUsage::Sampled, RHIResourceState::ShaderResource),
                ssaoOcc);
        }
        if (RHITexturePtr ssaoBlur = renderer->GetSSAOBlurredGraphTexture())
        {
            ssaoBlurHandle = m_FrameGraph.ImportTexture(
                FrameGraphResourceDesc::ImportedTexture(
                    "SSAO.Blurred", ssaoWidth, ssaoHeight, RHIFormat::R8_UNORM,
                    RHITextureUsage::RenderTarget | RHITextureUsage::Sampled, RHIResourceState::ShaderResource),
                ssaoBlur);
        }
    }

    if (renderer->IsHZBEnabled())
    {
        if (RHITexturePtr hzbTexture = renderer->GetHZBGraphTexture())
        {
            hzbHandle = m_FrameGraph.ImportTexture(
                FrameGraphResourceDesc::ImportedTexture(
                    "SceneHZB", frameWidth, frameHeight, RHIFormat::R32_FLOAT,
                    RHITextureUsage::Storage | RHITextureUsage::Sampled, RHIResourceState::ShaderResource),
                hzbTexture);
        }
    }

    m_FrameGraph.AddPass(
        "ShadowPass",
        [renderShadows, &shadowMapHandle, &localShadowMapHandle](FrameGraphPassBuilder& builder)
        {
            if (!renderShadows)
                return;

            if (shadowMapHandle.IsValid())
                shadowMapHandle = builder.Write(shadowMapHandle, FrameGraphResourceUsage::DepthStencilWrite);
            if (localShadowMapHandle.IsValid())
                localShadowMapHandle = builder.Write(localShadowMapHandle, FrameGraphResourceUsage::DepthStencilWrite);
        },
        [renderer, world, camera, mapPreviewParts, sceneRenderMask](RHIDevice&, const FrameGraphPass&)
        {
            if (!renderer || !world || !camera)
                return;

            const Mat4 mapWorld = Mat4::Identity();
            std::vector<std::pair<const float*, const PrimitiveMesh*>> shadowCasters;
            std::vector<std::pair<const float*, const PrimitiveMesh*>> localShadowCasters;
            shadowCasters.reserve(256);
            localShadowCasters.reserve(256);

            std::unordered_map<std::string, std::vector<PrimitiveMesh*>> frameMeshCache;
            frameMeshCache.reserve(128);
            auto getMeshesForPath = [renderer, &frameMeshCache](const std::string& meshPath) -> const std::vector<PrimitiveMesh*>&
            {
                auto it = frameMeshCache.find(meshPath);
                if (it != frameMeshCache.end())
                    return it->second;

                auto loaded = renderer->LoadMesh(meshPath);
                auto inserted = frameMeshCache.emplace(meshPath, std::move(loaded));
                return inserted.first->second;
            };

            world->Each<TransformComponent, PrimitiveComponent>(
                [world, renderer, sceneRenderMask, &shadowCasters, &localShadowCasters](Entity entity,
                                                                                        TransformComponent& transform,
                                                                                        PrimitiveComponent& prim)
                {
                    if (!MatchesRenderMask(*world, entity, sceneRenderMask))
                        return;

                    PrimitiveMesh* mesh = renderer->GetPrimitiveMesh(prim.type);
                    if (!mesh || mesh->indexCount == 0)
                        return;

                    shadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
                    localShadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
                });

            world->Each<TransformComponent, MeshComponent>(
                [world, sceneRenderMask, &getMeshesForPath, &shadowCasters, &localShadowCasters](Entity entity,
                                                                                                  TransformComponent& transform,
                                                                                                  MeshComponent& meshComp)
                {
                    if (!MatchesRenderMask(*world, entity, sceneRenderMask) || !meshComp.castShadow ||
                        meshComp.meshPath.empty())
                    {
                        return;
                    }

                    const auto& meshes = getMeshesForPath(meshComp.meshPath);
                    size_t startIdx = 0;
                    size_t endIdx = meshes.size();
                    if (meshComp.submeshIndex >= 0 && static_cast<size_t>(meshComp.submeshIndex) < meshes.size())
                    {
                        startIdx = static_cast<size_t>(meshComp.submeshIndex);
                        endIdx = startIdx + 1;
                    }

                    for (size_t i = startIdx; i < endIdx; ++i)
                    {
                        PrimitiveMesh* mesh = meshes[i];
                        if (!mesh || mesh->indexCount == 0)
                            continue;

                        shadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
                        localShadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
                    }
                });

            if (mapPreviewParts)
            {
                for (const MapPreviewPart& part : *mapPreviewParts)
                {
                    PrimitiveMesh* mesh = part.mesh.get();
                    if (!mesh || mesh->indexCount == 0)
                        continue;

                    shadowCasters.emplace_back(mapWorld.Data(), mesh);
                    localShadowCasters.emplace_back(mapWorld.Data(), mesh);
                }
            }

            if (!shadowCasters.empty())
                DirectionalShadowGraphPassExecutor::Execute(*renderer, *camera, shadowCasters);
            if (!localShadowCasters.empty())
                LocalShadowGraphPassExecutor::Execute(*renderer, localShadowCasters);
        });

    // These screen-space passes still execute through the legacy combined scene path below.
    // Running them here as standalone graph passes before the real depth prepass exists causes
    // resource churn and visible flicker/corruption in play mode.
    const bool useStandaloneGraphScreenSpacePasses = false;
    if (useStandaloneGraphScreenSpacePasses)
    {
        m_FrameGraph.AddPass(
            "DepthPrepassPass",
            [renderSSAO, renderer, &sceneDepthHandle, &resolvedSceneDepthHandle](FrameGraphPassBuilder& builder)
            {
                if (!(renderSSAO || renderer->IsHZBEnabled()))
                    return;

                resolvedSceneDepthHandle = builder.Write(sceneDepthHandle, FrameGraphResourceUsage::DepthStencilWrite);
            },
            [](RHIDevice&, const FrameGraphPass&) {});

        m_FrameGraph.AddPass(
            "SSAOPass",
            [renderSSAO, &resolvedSceneDepthHandle, &ssaoRawHandle](FrameGraphPassBuilder& builder)
            {
                if (!renderSSAO)
                    return;

                if (resolvedSceneDepthHandle.IsValid())
                    builder.Read(resolvedSceneDepthHandle, FrameGraphResourceUsage::DepthStencilRead);
                if (ssaoRawHandle.IsValid())
                    ssaoRawHandle = builder.Write(ssaoRawHandle, FrameGraphResourceUsage::ColorAttachment);
            },
            [](RHIDevice&, const FrameGraphPass&) {});

        m_FrameGraph.AddPass(
            "SSAOBlurPass",
            [renderSSAO, &ssaoRawHandle, &ssaoBlurHandle](FrameGraphPassBuilder& builder)
            {
                if (!renderSSAO)
                    return;

                if (ssaoRawHandle.IsValid())
                    builder.Read(ssaoRawHandle, FrameGraphResourceUsage::ShaderRead);
                if (ssaoBlurHandle.IsValid())
                    ssaoBlurHandle = builder.Write(ssaoBlurHandle, FrameGraphResourceUsage::ColorAttachment);
            },
            [](RHIDevice&, const FrameGraphPass&) {});

        m_FrameGraph.AddPass(
            "HZBPass",
            [renderer, &resolvedSceneDepthHandle, &hzbHandle](FrameGraphPassBuilder& builder)
            {
                if (!renderer->IsHZBEnabled())
                    return;

                if (resolvedSceneDepthHandle.IsValid())
                    builder.Read(resolvedSceneDepthHandle, FrameGraphResourceUsage::DepthStencilRead);
                if (hzbHandle.IsValid())
                    hzbHandle = builder.Write(hzbHandle, FrameGraphResourceUsage::Storage);
            },
            [](RHIDevice&, const FrameGraphPass&) {});
    }

    // Collect scene lighting data - up to 16 of each type
    SceneLightData lightData = {};
    const float deg2rad = 3.14159265f / 180.0f;
    bool foundDirLight = false;
    bool foundAmbientLight = false;
    int pointLightCount = 0;
    int spotLightCount = 0;
    std::vector<PointLightShadowCandidate> pointLightShadowCandidates;
    pointLightShadowCandidates.reserve(SceneLightData::MAX_POINT_LIGHTS);
    std::vector<SpotLightShadowCandidate> spotLightShadowCandidates;
    spotLightShadowCandidates.reserve(SceneLightData::MAX_SPOT_LIGHTS);
    struct SceneReflectionProbeCandidate
    {
        std::string cubemapPath;
        Vec3 position = Vec3::Zero();
        Vec3 tint = Vec3(1.0f, 1.0f, 1.0f);
        Vec3 boxExtents = Vec3::Zero();
        float radius = 0.0f;
        float intensity = 1.0f;
        float falloff = 0.25f;
        float rotation = 0.0f;
    };
    std::vector<SceneReflectionProbeCandidate> reflectionProbeCandidates;
    reflectionProbeCandidates.reserve(32);
    auto resolveReflectionProbeCubemapPath =
        [&resolvedSkybox](const ReflectionProbeComponent& probe) -> std::string
    {
        const bool usesSceneSkybox =
            probe.sourceMode == ReflectionProbeSourceMode::AutoSceneSkybox || probe.cubemapPath.empty();
        if (usesSceneSkybox && !resolvedSkybox.cubemapPath.empty())
            return resolvedSkybox.cubemapPath;
        return probe.cubemapPath;
    };
    auto resolveReflectionProbeRotation =
        [&resolvedSkybox](const ReflectionProbeComponent& probe, float entityYRotation) -> float
    {
        const bool usesSceneSkybox =
            probe.sourceMode == ReflectionProbeSourceMode::AutoSceneSkybox || probe.cubemapPath.empty();
        return usesSceneSkybox ? (resolvedSkybox.rotation + entityYRotation) : entityYRotation;
    };

    world->Each<TransformComponent>(
        [&](Entity entity, TransformComponent& transform)
        {
            const Vec3 worldPosition = GetWorldPosition(transform);
            const Vec3 worldForward = GetWorldForward(transform);

            // Directional Light (first one only)
            auto* dirLight = world->GetComponent<DirectionalLightComponent>(entity);
            if (dirLight && !foundDirLight)
            {
                lightData.lightDirX = worldForward.x;
                lightData.lightDirY = worldForward.y;
                lightData.lightDirZ = worldForward.z;

                lightData.lightIntensity = dirLight->intensity;
                lightData.lightColorR = dirLight->color.x;
                lightData.lightColorG = dirLight->color.y;
                lightData.lightColorB = dirLight->color.z;

                // Pass shadow settings
                lightData.shadowEnabled = dirLight->castShadows;
                lightData.shadowBias = dirLight->shadowBias;
                lightData.shadowDistance = dirLight->shadowDistance;

                foundDirLight = true;
            }

            // Point Lights (up to 16)
            auto* pointLight = world->GetComponent<PointLightComponent>(entity);
            if (pointLight && pointLightCount < SceneLightData::MAX_POINT_LIGHTS)
            {
                auto& pl = lightData.pointLights[pointLightCount];
                pl.posX = worldPosition.x;
                pl.posY = worldPosition.y;
                pl.posZ = worldPosition.z;
                pl.range = pointLight->range;
                pl.colorR = pointLight->color.x;
                pl.colorG = pointLight->color.y;
                pl.colorB = pointLight->color.z;
                pl.intensity = pointLight->intensity;
                pl.shadowEnabled = 0.0f;
                pl.shadowBaseSlice = 0.0f;
                pl.shadowBias = pointLight->shadowBias;
                pl._pad = 0.0f;
                pointLightShadowCandidates.push_back({entity.id, pointLightCount, worldPosition, pointLight->range,
                                                      pointLight->castShadows});
                pointLightCount++;
            }

            // Spot Lights (up to 16)
            auto* spotLight = world->GetComponent<SpotLightComponent>(entity);
            if (spotLight && spotLightCount < SceneLightData::MAX_SPOT_LIGHTS)
            {
                auto& sl = lightData.spotLights[spotLightCount];
                sl.posX = worldPosition.x;
                sl.posY = worldPosition.y;
                sl.posZ = worldPosition.z;
                sl.range = spotLight->range;

                sl.dirX = worldForward.x;
                sl.dirY = worldForward.y;
                sl.dirZ = worldForward.z;

                sl.innerCos = std::cos(spotLight->innerConeAngle * deg2rad);
                sl.outerCos = std::cos(spotLight->outerConeAngle * deg2rad);
                sl.colorR = spotLight->color.x;
                sl.colorG = spotLight->color.y;
                sl.colorB = spotLight->color.z;
                sl.intensity = spotLight->intensity;
                sl.shadowEnabled = 0.0f;
                sl.shadowBaseSlice = 0.0f;
                sl.shadowBias = spotLight->shadowBias;
                spotLightShadowCandidates.push_back(
                    {entity.id, spotLightCount, worldPosition, spotLight->castShadows, spotLight->range});
                spotLightCount++;
            }

            auto* ambientLight = world->GetComponent<AmbientLightComponent>(entity);
            if (ambientLight && !foundAmbientLight)
            {
                lightData.ambientColorR = ambientLight->color.x;
                lightData.ambientColorG = ambientLight->color.y;
                lightData.ambientColorB = ambientLight->color.z;
                lightData.ambientIntensity = ambientLight->intensity;
                foundAmbientLight = true;
            }

            auto* reflectionProbe = world->GetComponent<ReflectionProbeComponent>(entity);
            if (reflectionProbe && reflectionProbe->enabled && reflectionProbe->radius > 0.01f)
            {
                const std::string resolvedCubemapPath = resolveReflectionProbeCubemapPath(*reflectionProbe);
                if (resolvedCubemapPath.empty())
                    return;

                SceneReflectionProbeCandidate probe;
                probe.cubemapPath = resolvedCubemapPath;
                probe.position = worldPosition;
                probe.tint = reflectionProbe->tint;
                probe.boxExtents = ResolveReflectionProbeBoxExtents(reflectionProbe->boxExtents, reflectionProbe->radius);
                probe.radius = reflectionProbe->radius;
                probe.intensity = reflectionProbe->intensity;
                probe.falloff = reflectionProbe->falloff;
                probe.rotation = resolveReflectionProbeRotation(*reflectionProbe, transform.rotation.y);
                reflectionProbeCandidates.push_back(std::move(probe));
            }
        });

    lightData.numPointLights = pointLightCount;
    lightData.numSpotLights = spotLightCount;

    for (const SelectedPointLightShadow& selected :
         SelectPointLightsForShadows(pointLightShadowCandidates, Vec3(camX, camY, camZ)))
    {
        if (selected.sourceIndex < 0 || selected.sourceIndex >= lightData.numPointLights)
            continue;

        lightData.pointLights[selected.sourceIndex].shadowEnabled = 1.0f;
        lightData.pointLights[selected.sourceIndex].shadowBaseSlice = static_cast<float>(selected.shadowBaseSlice);
    }

    for (const SelectedSpotLightShadow& selected :
         SelectSpotLightsForShadows(spotLightShadowCandidates, Vec3(camX, camY, camZ)))
    {
        if (selected.sourceIndex < 0 || selected.sourceIndex >= lightData.numSpotLights)
            continue;

        lightData.spotLights[selected.sourceIndex].shadowEnabled = 1.0f;
        lightData.spotLights[selected.sourceIndex].shadowBaseSlice = static_cast<float>(selected.shadowSlice);
    }

    if (!foundAmbientLight && resolvedSkybox.ambientEnabled)
    {
        lightData.ambientColorR = resolvedSkybox.ambientColorR;
        lightData.ambientColorG = resolvedSkybox.ambientColorG;
        lightData.ambientColorB = resolvedSkybox.ambientColorB;
        lightData.ambientIntensity = resolvedSkybox.ambientIntensity;
        foundAmbientLight = true;
    }

    // If no directional light found, check for skybox sun settings
    if (!foundDirLight && resolvedSkybox.sunEnabled)
    {
        float radX = resolvedSkybox.sunRotationX * deg2rad;
        float radY = resolvedSkybox.sunRotationY * deg2rad;

        lightData.lightDirX = std::sin(radY) * std::cos(radX);
        lightData.lightDirY = -std::sin(radX);
        lightData.lightDirZ = std::cos(radY) * std::cos(radX);

        lightData.lightIntensity = resolvedSkybox.sunIntensity;
        lightData.lightColorR = resolvedSkybox.sunColorR;
        lightData.lightColorG = resolvedSkybox.sunColorG;
        lightData.lightColorB = resolvedSkybox.sunColorB;
        lightData.shadowEnabled = resolvedSkybox.sunCastShadows;
        lightData.shadowBias = resolvedSkybox.sunShadowBias;
        lightData.shadowDistance = resolvedSkybox.sunShadowDistance;
        foundDirLight = true;
    }

    if (!renderShadows)
    {
        lightData.shadowEnabled = false;
        for (int i = 0; i < lightData.numPointLights; ++i)
            lightData.pointLights[i].shadowEnabled = 0.0f;
        for (int i = 0; i < lightData.numSpotLights; ++i)
            lightData.spotLights[i].shadowEnabled = 0.0f;
    }

    renderer->SetLightData(lightData);

    struct PreparedViewmodelPassData
    {
        std::vector<RenderGraphQueuedDraw> drawQueue;
        std::vector<MaterialData> resolvedMaterials;
        bool antiAliasingEnabled = false;
        bool valid = false;
    };
    auto preparedViewmodelPass = std::make_shared<PreparedViewmodelPassData>();

    // Add Scene Pass - renders all entities
    m_FrameGraph.AddPass(
        "MainSceneOpaquePass",
        [renderShadows, renderSSAO, renderer, frameWidth, frameHeight, enableViewmodelPass, &shadowMapHandle,
         &localShadowMapHandle, &ssaoBlurHandle, &hzbHandle, &sceneColorHandle, &sceneDepthHandle,
         &resolvedSceneDepthHandle](FrameGraphPassBuilder& builder)
        {
            if (renderShadows)
            {
                if (shadowMapHandle.IsValid())
                    builder.Read(shadowMapHandle, FrameGraphResourceUsage::ShaderRead);
                if (localShadowMapHandle.IsValid())
                    builder.Read(localShadowMapHandle, FrameGraphResourceUsage::ShaderRead);
            }
            if (renderSSAO && ssaoBlurHandle.IsValid())
                builder.Read(ssaoBlurHandle, FrameGraphResourceUsage::ShaderRead);
            if (renderer->IsHZBEnabled() && hzbHandle.IsValid())
                builder.Read(hzbHandle, FrameGraphResourceUsage::ShaderRead);

            sceneColorHandle = builder.CreateTexture(FrameGraphResourceDesc::Texture2D(
                enableViewmodelPass ? "SceneColor.World" : "SceneColor.Final", frameWidth, frameHeight,
                RHIFormat::R8G8B8A8_UNORM, RHITextureUsage::RenderTarget | RHITextureUsage::Sampled));
            resolvedSceneDepthHandle = builder.Write(sceneDepthHandle, FrameGraphResourceUsage::DepthStencilWrite);
        },
        [this, renderer, world, camera, swapChain, mapPreviewParts, camX, camY, camZ, renderShadows, renderSSAO,
         showSSAODebugFullscreen, resolvedMaterialPathCache, materialRuntimeCache, sceneRenderMask,
         enableViewmodelPass, viewmodelRenderMask, viewmodelCamera,
         frustumCullingEnabled, antiAliasingEnabled, preparedViewmodelPass, sceneSettings, debugVisMode,
         &reflectionProbeCandidates,
         selectedEntity = m_SelectedEntity, selectedEntities = m_SelectedEntities,
         resolvedSkybox](RHIDevice& device, const FrameGraphPass& pass)
        {
            (void)device;
            (void)pass;
            const Mat4 mapWorld = Mat4::Identity();

            // Begin FXAA pass if enabled - redirects to intermediate RT
            renderer->BeginFXAAPass(swapChain);

            // Second pass: Render all entities using their world matrix and primitive type
            // First render skybox (if any)
            if (resolvedSkybox.HasVisibleSkybox() && debugVisMode != DebugVisMode::Overdraw)
            {
                renderer->RenderSkybox(*camera, swapChain, resolvedSkybox.cubemapPath, resolvedSkybox.wrapMode,
                                       resolvedSkybox.rotation, resolvedSkybox.showMarkers, resolvedSkybox.tintR * 0.5f,
                                       resolvedSkybox.tintG * 0.7f, resolvedSkybox.tintB * 1.0f,
                                       resolvedSkybox.tintR * 0.8f, resolvedSkybox.tintG * 0.8f,
                                       resolvedSkybox.tintB * 0.85f);
            }

            // ---- Shadow Pass: Collect shadow casters and render shadow map ----
            std::vector<std::pair<const float*, const PrimitiveMesh*>> shadowCasters;
            std::vector<std::pair<const float*, const PrimitiveMesh*>> localShadowCasters;
            std::vector<std::pair<const float*, const PrimitiveMesh*>> aoDepthCasters;
            std::unordered_map<std::string, std::vector<PrimitiveMesh*>> frameMeshCache;
            frameMeshCache.reserve(256);

            auto getMeshesForPath = [renderer, &frameMeshCache](const std::string& meshPath) -> const std::vector<PrimitiveMesh*>&
            {
                auto it = frameMeshCache.find(meshPath);
                if (it != frameMeshCache.end())
                    return it->second;

                auto loaded = renderer->LoadMesh(meshPath);
                auto inserted = frameMeshCache.emplace(meshPath, std::move(loaded));
                return inserted.first->second;
            };

            auto computeWorldBoundsFromMatrix = [](const Mat4& world, const PrimitiveMesh& mesh, float& minX, float& minY,
                                                   float& minZ, float& maxX, float& maxY, float& maxZ)
            {
                const Vec3 corners[8] = {
                    {mesh.boundsMinX, mesh.boundsMinY, mesh.boundsMinZ},
                    {mesh.boundsMaxX, mesh.boundsMinY, mesh.boundsMinZ},
                    {mesh.boundsMinX, mesh.boundsMaxY, mesh.boundsMinZ},
                    {mesh.boundsMaxX, mesh.boundsMaxY, mesh.boundsMinZ},
                    {mesh.boundsMinX, mesh.boundsMinY, mesh.boundsMaxZ},
                    {mesh.boundsMaxX, mesh.boundsMinY, mesh.boundsMaxZ},
                    {mesh.boundsMinX, mesh.boundsMaxY, mesh.boundsMaxZ},
                    {mesh.boundsMaxX, mesh.boundsMaxY, mesh.boundsMaxZ},
                };

                Vec3 worldCorner = world.TransformPoint(corners[0]);
                minX = maxX = worldCorner.x;
                minY = maxY = worldCorner.y;
                minZ = maxZ = worldCorner.z;

                for (size_t cornerIndex = 1; cornerIndex < std::size(corners); ++cornerIndex)
                {
                    worldCorner = world.TransformPoint(corners[cornerIndex]);
                    minX = std::min(minX, worldCorner.x);
                    minY = std::min(minY, worldCorner.y);
                    minZ = std::min(minZ, worldCorner.z);
                    maxX = std::max(maxX, worldCorner.x);
                    maxY = std::max(maxY, worldCorner.y);
                    maxZ = std::max(maxZ, worldCorner.z);
                }
            };
            auto computeWorldBounds = [&computeWorldBoundsFromMatrix](const TransformComponent& transform,
                                                                      const PrimitiveMesh& mesh, float& minX,
                                                                      float& minY, float& minZ, float& maxX,
                                                                      float& maxY, float& maxZ)
            {
                computeWorldBoundsFromMatrix(transform.worldMatrix, mesh, minX, minY, minZ, maxX, maxY, maxZ);
            };

            // AO prepass uses main-camera visibility, not shadow frustum visibility.
            // Include: scene renderables (PrimitiveComponent/MeshComponent) visible to the main camera.
            // Exclude: light-only entities, gizmos, grid, helper overlays, and non-renderable entities.
            // Guardrail: keep frustum culling + duplicate suppression to avoid uncontrolled prepass growth.
            Camera::Frustum mainFrustum = camera->GetFrustum();
            SpatialSplitFrustumCounters frustumCullCounters;
            auto passesMainFrustum =
                [&mainFrustum, &frustumCullCounters, frustumCullingEnabled](float minX, float minY, float minZ,
                                                                            float maxX, float maxY, float maxZ)
            {
                if (!frustumCullingEnabled)
                    return true;

                return TestAABBWithSpatialSplits(mainFrustum, minX, minY, minZ, maxX, maxY, maxZ,
                                                 &frustumCullCounters);
            };
            const float kAOSelectiveMaxDistance = 100.0f;
            const float kShadowSelectiveMaxDistance = 140.0f;
            const float kSmallObjectExtent = 2.0f;
            struct AODepthKey
            {
                const float* worldMatrixPtr;
                const PrimitiveMesh* meshPtr;
                bool operator==(const AODepthKey& rhs) const
                {
                    return worldMatrixPtr == rhs.worldMatrixPtr && meshPtr == rhs.meshPtr;
                }
            };
            struct AODepthKeyHash
            {
                size_t operator()(const AODepthKey& key) const
                {
                    return std::hash<const void*>{}(key.worldMatrixPtr) ^
                           (std::hash<const void*>{}(key.meshPtr) << 1U);
                }
            };
            std::unordered_set<AODepthKey, AODepthKeyHash> aoDepthSet;
            auto TryAppendAODepthCaster =
                [&](TransformComponent& transform, PrimitiveMesh* mesh)
            {
                if (!mesh || mesh->indexCount == 0)
                    return;

                g_DebugStats.aoDepthPrepassCandidates++;

                float minX, minY, minZ, maxX, maxY, maxZ;
                computeWorldBounds(transform, *mesh, minX, minY, minZ, maxX, maxY, maxZ);
                const float cx = (minX + maxX) * 0.5f;
                const float cy = (minY + maxY) * 0.5f;
                const float cz = (minZ + maxZ) * 0.5f;
                const float extent = std::max({maxX - minX, maxY - minY, maxZ - minZ});
                const float dx = cx - camX;
                const float dy = cy - camY;
                const float dz = cz - camZ;
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > kAOSelectiveMaxDistance && extent < kSmallObjectExtent)
                    return;

                if (!mainFrustum.TestAABB(minX, minY, minZ, maxX, maxY, maxZ))
                {
                    g_DebugStats.aoDepthPrepassFrustumCulled++;
                    return;
                }

                const AODepthKey key = {transform.worldMatrix.Data(), mesh};
                if (!aoDepthSet.insert(key).second)
                {
                    g_DebugStats.aoDepthPrepassDuplicatesSkipped++;
                    return;
                }

                aoDepthCasters.emplace_back(transform.worldMatrix.Data(), mesh);
            };

            // Compute shadow frustum for culling
            Camera::Frustum shadowFrustum = renderer->ComputeShadowFrustum(*camera);

            // Collect primitive shadow casters with shadow frustum culling
            world->Each<TransformComponent, PrimitiveComponent>(
                [&shadowCasters, &localShadowCasters, renderer, &shadowFrustum, camX, camY, camZ, kShadowSelectiveMaxDistance,
                 kSmallObjectExtent, &computeWorldBounds, world, sceneRenderMask](Entity entity, TransformComponent& transform,
                                                          PrimitiveComponent& prim)
                {
                    if (!MatchesRenderMask(*world, entity, sceneRenderMask))
                        return;

                    // Get the mesh for this primitive type
                    PrimitiveMesh* mesh = renderer->GetPrimitiveMesh(prim.type);
                    if (mesh && mesh->indexCount > 0)
                    {
                        localShadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);

                        // Shadow frustum cull
                        float minX, minY, minZ, maxX, maxY, maxZ;
                        computeWorldBounds(transform, *mesh, minX, minY, minZ, maxX, maxY, maxZ);
                        const float cx = (minX + maxX) * 0.5f;
                        const float cy = (minY + maxY) * 0.5f;
                        const float cz = (minZ + maxZ) * 0.5f;
                        const float extent = std::max({maxX - minX, maxY - minY, maxZ - minZ});
                        const float dx = cx - camX;
                        const float dy = cy - camY;
                        const float dz = cz - camZ;
                        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (dist > kShadowSelectiveMaxDistance && extent < kSmallObjectExtent)
                            return;

                        if (!shadowFrustum.TestAABB(minX, minY, minZ, maxX, maxY, maxZ))
                            return; // Outside shadow frustum

                        shadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
                    }
                });

            // Collect AO prepass primitives from renderable geometry visible in the main camera frustum.
            world->Each<TransformComponent, PrimitiveComponent>(
                [world, &TryAppendAODepthCaster, renderer, sceneRenderMask](Entity entity, TransformComponent& transform,
                                                           PrimitiveComponent& prim)
                {
                    if (!MatchesRenderMask(*world, entity, sceneRenderMask))
                        return;

                    if (world->GetComponent<DirectionalLightComponent>(entity) ||
                        world->GetComponent<PointLightComponent>(entity) ||
                        world->GetComponent<SpotLightComponent>(entity))
                    {
                        return;
                    }

                    PrimitiveMesh* mesh = renderer->GetPrimitiveMesh(prim.type);
                    TryAppendAODepthCaster(transform, mesh);
                });

            // Collect arbitrary mesh shadow casters with shadow frustum culling
            world->Each<TransformComponent, MeshComponent>(
                [&shadowCasters, &localShadowCasters, renderer, &shadowFrustum, &getMeshesForPath, camX, camY, camZ,
                 kShadowSelectiveMaxDistance, kSmallObjectExtent, &computeWorldBounds, world,
                 sceneRenderMask](Entity entity,
                                                                                        TransformComponent& transform,
                                                                                        MeshComponent& meshComp)
                {
                    if (!MatchesRenderMask(*world, entity, sceneRenderMask))
                        return;

                    // Skip if shadow casting is disabled for this mesh
                    if (!meshComp.castShadow)
                        return;

                    if (!meshComp.meshPath.empty())
                    {
                        const auto& meshes = getMeshesForPath(meshComp.meshPath);

                        // Determine which meshes to include based on submeshIndex
                        size_t startIdx = 0;
                        size_t endIdx = meshes.size();
                        if (meshComp.submeshIndex >= 0 && static_cast<size_t>(meshComp.submeshIndex) < meshes.size())
                        {
                            startIdx = static_cast<size_t>(meshComp.submeshIndex);
                            endIdx = startIdx + 1;
                        }

                        for (size_t i = startIdx; i < endIdx; ++i)
                        {
                            PrimitiveMesh* mesh = meshes[i];
                            if (mesh && mesh->indexCount > 0)
                            {
                                localShadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);

                                // Shadow frustum cull
                                float minX = 0.0f, minY = 0.0f, minZ = 0.0f, maxX = 0.0f, maxY = 0.0f,
                                      maxZ = 0.0f;
                                computeWorldBounds(transform, *mesh, minX, minY, minZ, maxX, maxY, maxZ);
                                const float cx = (minX + maxX) * 0.5f;
                                const float cy = (minY + maxY) * 0.5f;
                                const float cz = (minZ + maxZ) * 0.5f;
                                const float extent = std::max({maxX - minX, maxY - minY, maxZ - minZ});
                                const float dx = cx - camX;
                                const float dy = cy - camY;
                                const float dz = cz - camZ;
                                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                                if (dist > kShadowSelectiveMaxDistance && extent < kSmallObjectExtent)
                                    continue;

                                if (shadowFrustum.TestAABB(minX, minY, minZ, maxX, maxY, maxZ))
                                {
                                    shadowCasters.emplace_back(transform.worldMatrix.Data(), mesh);
                                }
                            }
                        }
                    }
                });

            if (mapPreviewParts)
            {
                for (const MapPreviewPart& part : *mapPreviewParts)
                {
                    PrimitiveMesh* mesh = part.mesh.get();
                    if (!mesh || mesh->indexCount == 0)
                        continue;

                    const float minX = mesh->boundsMinX;
                    const float minY = mesh->boundsMinY;
                    const float minZ = mesh->boundsMinZ;
                    const float maxX = mesh->boundsMaxX;
                    const float maxY = mesh->boundsMaxY;
                    const float maxZ = mesh->boundsMaxZ;
                    const float cx = (minX + maxX) * 0.5f;
                    const float cy = (minY + maxY) * 0.5f;
                    const float cz = (minZ + maxZ) * 0.5f;
                    const float extent = std::max({maxX - minX, maxY - minY, maxZ - minZ});
                    const float dx = cx - camX;
                    const float dy = cy - camY;
                    const float dz = cz - camZ;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > kShadowSelectiveMaxDistance && extent < kSmallObjectExtent)
                        continue;

                    if (shadowFrustum.TestAABB(minX, minY, minZ, maxX, maxY, maxZ))
                        shadowCasters.emplace_back(mapWorld.Data(), mesh);
                    localShadowCasters.emplace_back(mapWorld.Data(), mesh);

                    const AODepthKey key = {mapWorld.Data(), mesh};
                    if (aoDepthSet.insert(key).second)
                        aoDepthCasters.emplace_back(mapWorld.Data(), mesh);
                }
            }

            // Collect AO prepass meshes independently from shadow-caster filtering.
            world->Each<TransformComponent, MeshComponent>(
                [world, &TryAppendAODepthCaster, renderer, &getMeshesForPath, sceneRenderMask](Entity entity,
                                                                               TransformComponent& transform,
                                                                               MeshComponent& meshComp)
                {
                    if (!MatchesRenderMask(*world, entity, sceneRenderMask))
                        return;

                    if (world->GetComponent<DirectionalLightComponent>(entity) ||
                        world->GetComponent<PointLightComponent>(entity) ||
                        world->GetComponent<SpotLightComponent>(entity))
                    {
                        return;
                    }

                    if (meshComp.meshPath.empty())
                        return;

                    const auto& meshes = getMeshesForPath(meshComp.meshPath);
                    size_t startIdx = 0;
                    size_t endIdx = meshes.size();
                    if (meshComp.submeshIndex >= 0 && static_cast<size_t>(meshComp.submeshIndex) < meshes.size())
                    {
                        startIdx = static_cast<size_t>(meshComp.submeshIndex);
                        endIdx = startIdx + 1;
                    }

                    for (size_t i = startIdx; i < endIdx; ++i)
                    {
                        TryAppendAODepthCaster(transform, meshes[i]);
                    }
                });

            const bool needsDepthPrepass = renderSSAO || renderer->IsHZBEnabled();
            if (needsDepthPrepass)
            {
                g_DebugStats.aoPassSequenceStage = 0;
                if (renderSSAO)
                    renderer->CreateSSAOResources(swapChain, swapChain->GetWidth(), swapChain->GetHeight());
                g_DebugStats.aoPassSequenceStage = 1;
                renderer->BeginDepthPrepass(swapChain);
                for (auto& caster : aoDepthCasters)
                {
                    MaterialData depthMat = {};
                    renderer->RenderMesh(*camera, swapChain, caster.first, *caster.second, depthMat);
                    g_DebugStats.aoDepthPrepassDraws++;
                }
                renderer->EndDepthPrepass(swapChain);
            }

            if (renderSSAO)
            {
                g_DebugStats.aoPassSequenceStage = 2;
                SSAOGraphPassExecutor::Execute(*renderer, swapChain, *camera);
                g_DebugStats.aoPassSequenceStage = 3;
                SSAOBlurGraphPassExecutor::Execute(*renderer, swapChain);
                g_DebugStats.aoPassSequenceStage = 4;
            }
            else
            {
                g_DebugStats.aoPassSequenceStage = 0;
            }

            // ---- HZB Occlusion Culling ----
            if (renderer->IsHZBEnabled() && renderer->CreateHZBResources(swapChain->GetWidth(), swapChain->GetHeight()))
            {
                HZBGraphPassExecutor::Execute(*renderer, swapChain);
            }

            // Require 2 consecutive occluded results before culling to stabilize
            // previous-frame HZB latency and prevent one-frame full-scene pops.
            static std::unordered_map<uint64_t, uint8_t> s_HZBOcclusionStreak;
            auto makeHZBKey = [](Entity entity, uint32_t partId) -> uint64_t
            {
                uint64_t key = static_cast<uint64_t>(entity.GetIndex());
                key = (key << 8) | static_cast<uint64_t>(entity.GetGeneration());
                key = (key << 24) ^ static_cast<uint64_t>(partId);
                return key;
            };

            auto shouldCullByHZB = [renderer, camera, &makeHZBKey](Entity entity, uint32_t partId, float minX, float minY,
                                                                   float minZ, float maxX, float maxY,
                                                                   float maxZ) -> bool
            {
                if (!renderer->IsHZBEnabled())
                    return false;

                g_DebugStats.hzbTests++;
                const bool occluded = renderer->TestHZBOcclusion(*camera, minX, minY, minZ, maxX, maxY, maxZ);

                uint8_t& streak = s_HZBOcclusionStreak[makeHZBKey(entity, partId)];
                if (occluded)
                {
                    streak = static_cast<uint8_t>(std::min<int>(streak + 1, 3));
                }
                else
                {
                    streak = 0;
                }

                if (occluded && streak >= 2)
                {
                    g_DebugStats.hzbCulled++;

                    // Safety valve: if culling ratio becomes implausibly high in one frame,
                    // fail open and reset history to avoid whole-scene disappearance.
                    if (g_DebugStats.hzbTests >= 256 &&
                        (g_DebugStats.hzbCulled * 100) > (g_DebugStats.hzbTests * 97))
                    {
                        s_HZBOcclusionStreak.clear();
                        return false;
                    }

                    return true;
                }

                return false;
            };

            auto passesSceneVisibility = [&passesMainFrustum, &shouldCullByHZB](Entity entity, uint32_t partId, float minX,
                                                                                float minY, float minZ, float maxX,
                                                                                float maxY, float maxZ) -> bool
            {
                if (!passesMainFrustum(minX, minY, minZ, maxX, maxY, maxZ))
                {
                    g_DebugStats.skippedEntities++;
                    return false;
                }

                if (shouldCullByHZB(entity, partId, minX, minY, minZ, maxX, maxY, maxZ))
                {
                    g_DebugStats.skippedEntities++;
                    return false;
                }

                return true;
            };

            // ---- Forward+ Tiled Lighting: Create resources and dispatch culling ----
            renderer->CreateForwardPlusResources(swapChain->GetWidth(), swapChain->GetHeight());
            renderer->CullLights(*camera, swapChain);

            // Restore render state for main pass (Depth DSV, RTV)
            renderer->BeginForwardPass(swapChain);

            std::vector<MaterialData> resolvedMaterials;
            resolvedMaterials.reserve(256);
            MaterialData defaultMaterial;
            defaultMaterial.colorR = 0.7f;
            defaultMaterial.colorG = 0.7f;
            defaultMaterial.colorB = 0.7f;
            defaultMaterial.metallic = 0.0f;
            defaultMaterial.roughness = 0.5f;
            defaultMaterial.emissiveColorR = 0.0f;
            defaultMaterial.emissiveColorG = 0.0f;
            defaultMaterial.emissiveColorB = 0.0f;
            defaultMaterial.emissiveStrength = 0.0f;
            resolvedMaterials.push_back(defaultMaterial); // Index 0 = default material

            std::unordered_map<uint64_t, size_t> materialIndexCache;
            materialIndexCache.reserve(256);

            std::vector<RenderGraphQueuedDraw> drawQueue;
            drawQueue.reserve(4096);
            std::vector<RenderGraphQueuedDraw> viewmodelDrawQueue;
            viewmodelDrawQueue.reserve(256);

            constexpr float kHlodClusterDistance = 80.0f;
            constexpr float kHlodClusterCellSize = 24.0f;
            constexpr size_t kHlodMinimumClusterSize = 3;

            struct HlodCandidate
            {
                Entity entity = kNullEntity;
                std::string meshPath;
                Mat4 worldMatrix = Mat4::Identity();
                const std::vector<MeshData>* sourceMeshes = nullptr;
                size_t startIdx = 0;
                size_t endIdx = 0;
                Vec3 center = Vec3::Zero();
                uint64_t clusterKey = 0;
                float minX = 0.0f;
                float minY = 0.0f;
                float minZ = 0.0f;
                float maxX = 0.0f;
                float maxY = 0.0f;
                float maxZ = 0.0f;
            };
            std::vector<HlodCandidate> hlodCandidates;
            hlodCandidates.reserve(256);

            std::unordered_set<uint32_t> selectedEntityIds;
            selectedEntityIds.reserve(selectedEntities.size() + 1);
            if (selectedEntity.IsValid())
                selectedEntityIds.insert(selectedEntity.id);
            for (const Entity& selected : selectedEntities)
            {
                if (selected.IsValid())
                    selectedEntityIds.insert(selected.id);
            }

            auto getSourceMeshesForPath = [this](const std::string& meshPath) -> const std::vector<MeshData>*
            {
                auto& cacheEntry = m_HlodSourceMeshCache[meshPath];
                if (!cacheEntry.attemptedLoad)
                {
                    cacheEntry.attemptedLoad = true;
                    cacheEntry.meshes.clear();

                    const std::string fullPath = AssetManager::Get().GetFullPath(meshPath);
                    std::filesystem::path path(fullPath);
                    std::string extension = path.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                    if (extension == ".fbx")
                    {
                        std::string unusedColormap;
                        LoadFbxFile(fullPath, cacheEntry.meshes, unusedColormap, path.stem().string());
                    }
                    else if (extension == ".obj")
                    {
                        MeshData meshData;
                        if (LoadObjFile(path, meshData))
                            cacheEntry.meshes.push_back(std::move(meshData));
                    }
                }

                if (cacheEntry.meshes.empty())
                    return nullptr;
                return &cacheEntry.meshes;
            };

            auto resolveMaterialDataFromPath = [&](const std::string& materialPath, MaterialData& matData) -> bool
            {
                if (materialPath.empty())
                    return false;

                std::string fullPath;
                auto resolvedPathIt = resolvedMaterialPathCache->find(materialPath);
                if (resolvedPathIt != resolvedMaterialPathCache->end())
                {
                    fullPath = resolvedPathIt->second;
                }
                else
                {
                    fullPath = materialPath;
                    if (fullPath.find(":") == std::string::npos)
                    {
                        std::string assetPath = fullPath;
                        if (fullPath.find("Materials/") != 0 && fullPath.find("Materials\\") != 0)
                            assetPath = "Materials/" + fullPath;
                        std::filesystem::path assetsPath = AssetManager::Get().GetFullPath(assetPath);
                        if (std::filesystem::exists(assetsPath))
                            fullPath = assetsPath.string();
                    }
                    (*resolvedMaterialPathCache)[materialPath] = fullPath;
                }

                auto runtimeIt = materialRuntimeCache->find(fullPath);
                if (runtimeIt == materialRuntimeCache->end())
                {
                    CachedMaterialRuntime cached;
                    cached.loaded = MaterialLoader::Load(fullPath);

                    if (cached.loaded.valid)
                    {
                        MaterialGraph graph;
                        if (graph.LoadFromFile(fullPath))
                        {
                            std::string hlsl = graph.GenerateHLSL();
                            if (!hlsl.empty())
                                cached.shaderId = renderer->RegisterMaterialShader(hlsl);
                        }
                    }

                    runtimeIt = materialRuntimeCache->emplace(fullPath, std::move(cached)).first;
                }

                const CachedMaterialRuntime& cached = runtimeIt->second;
                if (!cached.loaded.valid)
                    return false;

                matData.colorR = cached.loaded.baseColor.x;
                matData.colorG = cached.loaded.baseColor.y;
                matData.colorB = cached.loaded.baseColor.z;
                matData.metallic = cached.loaded.metallic;
                matData.roughness = cached.loaded.roughness;
                matData.ambientOcclusion = cached.loaded.ambientOcclusion;

                for (int i = 0; i < 4; ++i)
                {
                    if (cached.loaded.hasTextures[i] && !cached.loaded.texturePaths[i].empty())
                        matData.texturePaths[i] = cached.loaded.texturePaths[i];
                    matData.textureSampleTypes[i] = cached.loaded.textureSampleTypes[i];
                }
                matData.albedoTextureSlot = cached.loaded.albedoTextureSlot;
                matData.normalTextureSlot = cached.loaded.normalTextureSlot;
                matData.ormTextureSlot = cached.loaded.ormTextureSlot;
                matData.tilingU = cached.loaded.tilingU;
                matData.tilingV = cached.loaded.tilingV;
                matData.offsetU = cached.loaded.offsetU;
                matData.offsetV = cached.loaded.offsetV;
                matData.filterMode = cached.loaded.filterMode;
                matData.wrapMode = cached.loaded.wrapMode;
                matData.pannerSpeedU = cached.loaded.pannerSpeedU;
                matData.pannerSpeedV = cached.loaded.pannerSpeedV;
                matData.pannerMethod = cached.loaded.pannerMethod;
                matData.pannerLink = cached.loaded.pannerLink;
                matData.materialShaderId = cached.shaderId;
                return true;
            };

            auto resolveMaterialIndex = [&](Entity entity, const MaterialComponent* material,
                                            const PrimitiveMesh* mesh) -> size_t
            {
                const uint64_t materialCacheKey =
                    (static_cast<uint64_t>(entity.id) << 32) ^
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mesh) & 0xFFFFFFFFu);
                auto cachedIt = materialIndexCache.find(materialCacheKey);
                if (cachedIt != materialIndexCache.end())
                    return cachedIt->second;

                MaterialData matData = defaultMaterial;

                if (material && material->useMaterialFile && !material->materialPath.empty())
                {
                    if (!resolveMaterialDataFromPath(material->materialPath, matData))
                    {
                        matData.colorR = material->baseColor.x;
                        matData.colorG = material->baseColor.y;
                        matData.colorB = material->baseColor.z;
                        matData.metallic = material->metallic;
                        matData.roughness = material->roughness;
                        matData.emissiveColorR = material->emissiveColor.x;
                        matData.emissiveColorG = material->emissiveColor.y;
                        matData.emissiveColorB = material->emissiveColor.z;
                        matData.emissiveStrength = material->emissiveStrength;
                    }
                }
                else if (material)
                {
                    matData.colorR = material->baseColor.x;
                    matData.colorG = material->baseColor.y;
                    matData.colorB = material->baseColor.z;
                    matData.metallic = material->metallic;
                    matData.roughness = material->roughness;
                    matData.emissiveColorR = material->emissiveColor.x;
                    matData.emissiveColorG = material->emissiveColor.y;
                    matData.emissiveColorB = material->emissiveColor.z;
                    matData.emissiveStrength = material->emissiveStrength;
                }
                else if (mesh)
                {
                    for (const Submesh& submesh : mesh->submeshes)
                    {
                        if (resolveMaterialDataFromPath(submesh.materialPath, matData))
                            break;
                    }
                }

                matData.lightmapEnabled = false;
                matData.lightmapTextureSlot = -1;
                matData.lightmapTexturePath.clear();
                matData.lightmapScaleU = 1.0f;
                matData.lightmapScaleV = 1.0f;
                matData.lightmapOffsetU = 0.0f;
                matData.lightmapOffsetV = 0.0f;
                matData.lightmapIntensity = 1.0f;

                const size_t index = resolvedMaterials.size();
                resolvedMaterials.push_back(std::move(matData));
                materialIndexCache.emplace(materialCacheKey, index);
                return index;
            };

            auto computeReflectionProbeDistanceSq = [](const SceneReflectionProbeCandidate& probe, float minX, float minY,
                                                       float minZ, float maxX, float maxY, float maxZ)
            {
                const float centerX = (minX + maxX) * 0.5f;
                const float centerY = (minY + maxY) * 0.5f;
                const float centerZ = (minZ + maxZ) * 0.5f;
                const float halfX = (maxX - minX) * 0.5f;
                const float halfY = (maxY - minY) * 0.5f;
                const float halfZ = (maxZ - minZ) * 0.5f;
                const float boundsRadius = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);

                Vec3 localCenter =
                    RotateVectorYDegrees(Vec3(centerX - probe.position.x, centerY - probe.position.y,
                                              centerZ - probe.position.z),
                                         -probe.rotation);
                const Vec3 expandedExtents = probe.boxExtents + Vec3(boundsRadius, boundsRadius, boundsRadius);
                const float dx = std::max(std::abs(localCenter.x) - expandedExtents.x, 0.0f);
                const float dy = std::max(std::abs(localCenter.y) - expandedExtents.y, 0.0f);
                const float dz = std::max(std::abs(localCenter.z) - expandedExtents.z, 0.0f);
                return dx * dx + dy * dy + dz * dz;
            };

            auto selectReflectionProbes =
                [&](RenderGraphReflectionProbe* outProbes, uint32_t& outProbeCount, float minX, float minY,
                    float minZ, float maxX, float maxY, float maxZ)
            {
                struct SelectedProbe
                {
                    const SceneReflectionProbeCandidate* probe = nullptr;
                    float score = 0.0f;
                } selected[2];

                const float centerX = (minX + maxX) * 0.5f;
                const float centerY = (minY + maxY) * 0.5f;
                const float centerZ = (minZ + maxZ) * 0.5f;

                auto computeFadeWeight = [](const SceneReflectionProbeCandidate& probe, float centerX, float centerY,
                                            float centerZ)
                {
                    const float safeFalloff = std::clamp(probe.falloff, 0.0f, 1.0f);
                    Vec3 localCenter =
                        RotateVectorYDegrees(Vec3(centerX - probe.position.x, centerY - probe.position.y,
                                                  centerZ - probe.position.z),
                                             -probe.rotation);
                    const float nx = std::abs(localCenter.x) / std::max(probe.boxExtents.x, 0.001f);
                    const float ny = std::abs(localCenter.y) / std::max(probe.boxExtents.y, 0.001f);
                    const float nz = std::abs(localCenter.z) / std::max(probe.boxExtents.z, 0.001f);
                    const float edge = std::max(nx, std::max(ny, nz));
                    if (edge >= 1.0f)
                        return 0.0f;
                    if (safeFalloff <= 0.0001f)
                        return 1.0f;

                    const float fadeStart = 1.0f - safeFalloff;
                    if (edge <= fadeStart)
                        return 1.0f;

                    const float fadeSpan = std::max(1.0f - fadeStart, 0.0001f);
                    const float t = std::clamp((edge - fadeStart) / fadeSpan, 0.0f, 1.0f);
                    return 1.0f - (t * t * (3.0f - 2.0f * t));
                };

                for (const SceneReflectionProbeCandidate& probe : reflectionProbeCandidates)
                {
                    const float boundsDistanceSq =
                        computeReflectionProbeDistanceSq(probe, minX, minY, minZ, maxX, maxY, maxZ);
                    if (boundsDistanceSq > 0.0f)
                        continue;

                    const float fadeWeight = computeFadeWeight(probe, centerX, centerY, centerZ);
                    const float score = fadeWeight * std::max(probe.intensity, 0.0f);
                    if (score <= 0.0001f)
                        continue;

                    int replaceIndex = -1;
                    if (!selected[0].probe || score > selected[0].score)
                    {
                        replaceIndex = 0;
                    }
                    else if (!selected[1].probe || score > selected[1].score)
                    {
                        replaceIndex = 1;
                    }

                    if (replaceIndex < 0)
                        continue;

                    if (replaceIndex == 0)
                        selected[1] = selected[0];

                    selected[replaceIndex].probe = &probe;
                    selected[replaceIndex].score = score;
                }

                float totalScore = 0.0f;
                outProbeCount = 0;
                for (const SelectedProbe& selection : selected)
                {
                    if (!selection.probe)
                        continue;
                    totalScore += selection.score;
                }

                for (const SelectedProbe& selection : selected)
                {
                    if (!selection.probe || outProbeCount >= 2)
                        continue;

                    RenderGraphReflectionProbe& outProbe = outProbes[outProbeCount++];
                    outProbe.cubemapPath = selection.probe->cubemapPath;
                    outProbe.position = selection.probe->position;
                    outProbe.tint = selection.probe->tint;
                    outProbe.boxExtents = selection.probe->boxExtents;
                    outProbe.radius = selection.probe->radius;
                    outProbe.intensity = selection.probe->intensity;
                    outProbe.falloff = selection.probe->falloff;
                    outProbe.rotation = selection.probe->rotation;
                    outProbe.blendWeight = totalScore > 0.0001f ? (selection.score / totalScore) : 0.0f;
                }
            };

            auto applyReflectionProbeToQueuedDraw =
                [&](RenderGraphQueuedDraw& draw, float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
            {
                draw.reflectionProbeCount = 0;
                draw.hasReflectionProbe = false;
                selectReflectionProbes(draw.reflectionProbes, draw.reflectionProbeCount, minX, minY, minZ, maxX, maxY,
                                       maxZ);
                draw.hasReflectionProbe = draw.reflectionProbeCount > 0;
            };

            auto applyReflectionProbeToMapPreviewDraw =
                [&](RenderGraphMapPreviewDraw& draw, float minX, float minY, float minZ, float maxX, float maxY,
                    float maxZ)
            {
                draw.reflectionProbeCount = 0;
                draw.hasReflectionProbe = false;
                selectReflectionProbes(draw.reflectionProbes, draw.reflectionProbeCount, minX, minY, minZ, maxX, maxY,
                                       maxZ);
                draw.hasReflectionProbe = draw.reflectionProbeCount > 0;
            };

            auto appendQueuedDraw =
                [&applyReflectionProbeToQueuedDraw](std::vector<RenderGraphQueuedDraw>& queue, const Mat4& worldMatrix,
                                                    const PrimitiveMesh* mesh, size_t materialIndex, float minX,
                                                    float minY, float minZ, float maxX, float maxY, float maxZ,
                                                    bool overrideLodThresholds = false, float lod1ScreenHeight = 0.0f,
                                                    float lod2ScreenHeight = 0.0f)
            {
                RenderGraphQueuedDraw draw;
                draw.worldMatrix = worldMatrix;
                draw.mesh = mesh;
                draw.materialIndex = materialIndex;
                draw.overrideLodThresholds = overrideLodThresholds;
                draw.lod1ScreenHeight = lod1ScreenHeight;
                draw.lod2ScreenHeight = lod2ScreenHeight;
                applyReflectionProbeToQueuedDraw(draw, minX, minY, minZ, maxX, maxY, maxZ);
                queue.push_back(draw);
            };

            auto appendTransformQueuedDraw =
                [&appendQueuedDraw](std::vector<RenderGraphQueuedDraw>& queue, const TransformComponent& transform,
                                    const PrimitiveMesh* mesh, size_t materialIndex, float minX, float minY, float minZ,
                                    float maxX, float maxY, float maxZ,
                                    bool overrideLodThresholds = false, float lod1ScreenHeight = 0.0f,
                                    float lod2ScreenHeight = 0.0f)
            {
                appendQueuedDraw(queue, transform.worldMatrix, mesh, materialIndex, minX, minY, minZ, maxX, maxY, maxZ,
                                 overrideLodThresholds, lod1ScreenHeight, lod2ScreenHeight);
            };

            auto usesSpatialSplitBounds = [](float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
            {
                return BuildSpatialSplitFrustumInfo(minX, minY, minZ, maxX, maxY, maxZ).UsesSplits();
            };

            auto enqueueChunkedCubeDraws =
                [&mainFrustum, &frustumCullCounters, &computeWorldBoundsFromMatrix, &appendQueuedDraw, shouldCullByHZB,
                 &drawQueue](Entity entity, const TransformComponent& transform, const PrimitiveMesh& primMesh,
                             size_t materialIndex) -> bool
            {
                (void)entity;
                (void)mainFrustum;
                (void)computeWorldBoundsFromMatrix;
                (void)shouldCullByHZB;

                float minX, minY, minZ, maxX, maxY, maxZ;
                computeWorldBoundsFromMatrix(transform.worldMatrix, primMesh, minX, minY, minZ, maxX, maxY, maxZ);

                frustumCullCounters.frustumTestedObjects++;
                const SpatialSplitFrustumInfo splitInfo =
                    BuildSpatialSplitFrustumInfo(minX, minY, minZ, maxX, maxY, maxZ);
                if (!splitInfo.UsesSplits())
                    return false;

                frustumCullCounters.chunkedObjects++;
                // Fail open for very large cube primitives until chunk-bound visualization
                // is in place. This keeps floors and walls stable instead of popping early.
                appendQueuedDraw(drawQueue, transform.worldMatrix, &primMesh, materialIndex, minX, minY, minZ, maxX,
                                 maxY, maxZ);
                frustumCullCounters.acceptedViaChunkObjects++;

                return true;
            };

            // Collect visible draws using frame-local mesh/material caches.
            world->Each<TransformComponent>(
                [world, renderer, &resolveMaterialIndex, &getMeshesForPath, &passesSceneVisibility, &passesMainFrustum,
                 &computeWorldBounds, sceneRenderMask, enableViewmodelPass, viewmodelRenderMask,
                 &appendTransformQueuedDraw, &drawQueue, &viewmodelDrawQueue, &getSourceMeshesForPath, &hlodCandidates,
                 &selectedEntityIds, camX, camY, camZ](
                    Entity entity, TransformComponent& transform)
                {
                    if (world->GetComponent<DirectionalLightComponent>(entity) ||
                        world->GetComponent<PointLightComponent>(entity) ||
                        world->GetComponent<SpotLightComponent>(entity))
                    {
                        g_DebugStats.skippedEntities++;
                        return;
                    }

                    auto* primitive = world->GetComponent<PrimitiveComponent>(entity);
                    auto* meshComp = world->GetComponent<MeshComponent>(entity);
                    auto* material = world->GetComponent<MaterialComponent>(entity);
                    const bool renderInScene = MatchesRenderMask(*world, entity, sceneRenderMask);
                    const bool renderInViewmodel =
                        enableViewmodelPass && MatchesRenderMask(*world, entity, viewmodelRenderMask);

                    if (!renderInScene && !renderInViewmodel)
                        return;

                    if (primitive)
                    {
                        PrimitiveMesh* primMesh = renderer->GetPrimitiveMesh(primitive->type);
                        if (!primMesh)
                            return;
                        const size_t materialIndex = resolveMaterialIndex(entity, material, primMesh);
                        float minX, minY, minZ, maxX, maxY, maxZ;
                        computeWorldBounds(transform, *primMesh, minX, minY, minZ, maxX, maxY, maxZ);
                        float lod1ScreenHeight = 0.0f;
                        float lod2ScreenHeight = 0.0f;
                        GetPrimitiveLodScreenHeightThresholds(*primitive, lod1ScreenHeight, lod2ScreenHeight);

                        if (renderInScene)
                        {
                            if (!passesSceneVisibility(entity, 0, minX, minY, minZ, maxX, maxY, maxZ))
                                return;

                            appendTransformQueuedDraw(drawQueue, transform, primMesh, materialIndex, minX, minY, minZ,
                                                      maxX, maxY, maxZ,
                                                      primitive->overrideLodThresholds, lod1ScreenHeight,
                                                      lod2ScreenHeight);
                        }

                        if (renderInViewmodel)
                            appendTransformQueuedDraw(viewmodelDrawQueue, transform, primMesh, materialIndex, minX, minY,
                                                      minZ, maxX, maxY, maxZ,
                                                      primitive->overrideLodThresholds, lod1ScreenHeight,
                                                      lod2ScreenHeight);
                        return;
                    }

                    if (!meshComp || meshComp->meshPath.empty())
                        return;

                    const auto& meshes = getMeshesForPath(meshComp->meshPath);
                    size_t startIdx = 0;
                    size_t endIdx = meshes.size();
                    if (meshComp->submeshIndex >= 0 && static_cast<size_t>(meshComp->submeshIndex) < meshes.size())
                    {
                        startIdx = static_cast<size_t>(meshComp->submeshIndex);
                        endIdx = startIdx + 1;
                    }

                    const bool canClusterIntoHlod =
                        renderInScene && !renderInViewmodel && !material && meshComp->submeshIndex < 0 &&
                        selectedEntityIds.find(entity.id) == selectedEntityIds.end();

                    if (canClusterIntoHlod)
                    {
                        const std::vector<MeshData>* sourceMeshes = getSourceMeshesForPath(meshComp->meshPath);
                        if (sourceMeshes && startIdx < sourceMeshes->size())
                        {
                            const size_t sourceEndIdx = std::min(endIdx, sourceMeshes->size());
                            if (sourceEndIdx > startIdx)
                            {
                                float minX = 0.0f, minY = 0.0f, minZ = 0.0f, maxX = 0.0f, maxY = 0.0f,
                                      maxZ = 0.0f;
                                bool haveBounds = false;
                                for (size_t i = startIdx; i < endIdx; ++i)
                                {
                                    PrimitiveMesh* mesh = meshes[i];
                                    if (!mesh)
                                        continue;

                                    float meshMinX, meshMinY, meshMinZ, meshMaxX, meshMaxY, meshMaxZ;
                                    computeWorldBounds(transform, *mesh, meshMinX, meshMinY, meshMinZ, meshMaxX,
                                                       meshMaxY, meshMaxZ);
                                    if (!haveBounds)
                                    {
                                        minX = meshMinX;
                                        minY = meshMinY;
                                        minZ = meshMinZ;
                                        maxX = meshMaxX;
                                        maxY = meshMaxY;
                                        maxZ = meshMaxZ;
                                        haveBounds = true;
                                    }
                                    else
                                    {
                                        minX = std::min(minX, meshMinX);
                                        minY = std::min(minY, meshMinY);
                                        minZ = std::min(minZ, meshMinZ);
                                        maxX = std::max(maxX, meshMaxX);
                                        maxY = std::max(maxY, meshMaxY);
                                        maxZ = std::max(maxZ, meshMaxZ);
                                    }
                                }

                                if (haveBounds)
                                {
                                    if (!passesMainFrustum(minX, minY, minZ, maxX, maxY, maxZ))
                                    {
                                        g_DebugStats.skippedEntities++;
                                        return;
                                    }

                                    const Vec3 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f,
                                                      (minZ + maxZ) * 0.5f);
                                    const float dx = center.x - camX;
                                    const float dy = center.y - camY;
                                    const float dz = center.z - camZ;
                                    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                                    if (distance >= kHlodClusterDistance)
                                    {
                                        const int32_t cellX =
                                            static_cast<int32_t>(std::floor(center.x / kHlodClusterCellSize));
                                        const int32_t cellY =
                                            static_cast<int32_t>(std::floor(center.y / kHlodClusterCellSize));
                                        const int32_t cellZ =
                                            static_cast<int32_t>(std::floor(center.z / kHlodClusterCellSize));
                                        hlodCandidates.push_back(
                                            {entity, meshComp->meshPath, transform.worldMatrix, sourceMeshes, startIdx,
                                             sourceEndIdx, center, MakeHlodCellKey(cellX, cellY, cellZ), minX, minY,
                                             minZ, maxX, maxY, maxZ});
                                        return;
                                    }
                                }
                            }
                        }
                    }

                    for (size_t i = startIdx; i < endIdx; ++i)
                    {
                        PrimitiveMesh* mesh = meshes[i];
                        if (!mesh)
                            continue;
                        const size_t materialIndex = resolveMaterialIndex(entity, material, mesh);
                        float minX, minY, minZ, maxX, maxY, maxZ;
                        computeWorldBounds(transform, *mesh, minX, minY, minZ, maxX, maxY, maxZ);

                        if (renderInScene)
                        {
                            if (!passesSceneVisibility(entity, static_cast<uint32_t>(i + 1), minX, minY, minZ, maxX, maxY,
                                                       maxZ))
                                continue;

                            appendTransformQueuedDraw(drawQueue, transform, mesh, materialIndex, minX, minY, minZ, maxX,
                                                      maxY, maxZ);
                        }

                        if (renderInViewmodel)
                            appendTransformQueuedDraw(viewmodelDrawQueue, transform, mesh, materialIndex, minX, minY, minZ,
                                                      maxX, maxY, maxZ);
                    }
                });

            if (!hlodCandidates.empty())
            {
                std::unordered_map<uint64_t, std::vector<const HlodCandidate*>> hlodClusters;
                hlodClusters.reserve(hlodCandidates.size());
                for (const HlodCandidate& candidate : hlodCandidates)
                    hlodClusters[candidate.clusterKey].push_back(&candidate);

                for (const auto& clusterIt : hlodClusters)
                {
                    const uint64_t clusterKey = clusterIt.first;
                    const std::vector<const HlodCandidate*>& clusterItems = clusterIt.second;
                    if (clusterItems.size() < kHlodMinimumClusterSize)
                    {
                        for (const HlodCandidate* candidate : clusterItems)
                        {
                            if (!passesSceneVisibility(candidate->entity, static_cast<uint32_t>(candidate->startIdx + 1),
                                                       candidate->minX, candidate->minY, candidate->minZ, candidate->maxX,
                                                       candidate->maxY, candidate->maxZ))
                            {
                                continue;
                            }

                            const auto& meshes = getMeshesForPath(candidate->meshPath);
                            for (size_t i = candidate->startIdx; i < candidate->endIdx && i < meshes.size(); ++i)
                            {
                                PrimitiveMesh* mesh = meshes[i];
                                if (!mesh)
                                    continue;
                                const size_t materialIndex = resolveMaterialIndex(candidate->entity, nullptr, mesh);
                                appendQueuedDraw(drawQueue, candidate->worldMatrix, mesh, materialIndex, candidate->minX,
                                                 candidate->minY, candidate->minZ, candidate->maxX, candidate->maxY,
                                                 candidate->maxZ);
                            }
                        }
                        continue;
                    }

                    uint64_t signature = 1469598103934665603ull;
                    Vec3 clusterCenter = Vec3::Zero();
                    size_t clusterMeshPartCount = 0;
                    float clusterMinX = 0.0f, clusterMinY = 0.0f, clusterMinZ = 0.0f;
                    float clusterMaxX = 0.0f, clusterMaxY = 0.0f, clusterMaxZ = 0.0f;
                    bool haveClusterBounds = false;
                    for (const HlodCandidate* candidate : clusterItems)
                    {
                        signature = HashCombine64(signature, static_cast<uint64_t>(candidate->entity.id));
                        signature = HashCombine64(signature, HashString64(candidate->meshPath));
                        signature = HashCombine64(signature, HashMat4(candidate->worldMatrix));
                        signature = HashCombine64(signature, static_cast<uint64_t>(candidate->startIdx));
                        signature = HashCombine64(signature, static_cast<uint64_t>(candidate->endIdx));
                        clusterCenter += candidate->center;
                        clusterMeshPartCount += candidate->endIdx - candidate->startIdx;

                        if (!haveClusterBounds)
                        {
                            clusterMinX = candidate->minX;
                            clusterMinY = candidate->minY;
                            clusterMinZ = candidate->minZ;
                            clusterMaxX = candidate->maxX;
                            clusterMaxY = candidate->maxY;
                            clusterMaxZ = candidate->maxZ;
                            haveClusterBounds = true;
                        }
                        else
                        {
                            clusterMinX = std::min(clusterMinX, candidate->minX);
                            clusterMinY = std::min(clusterMinY, candidate->minY);
                            clusterMinZ = std::min(clusterMinZ, candidate->minZ);
                            clusterMaxX = std::max(clusterMaxX, candidate->maxX);
                            clusterMaxY = std::max(clusterMaxY, candidate->maxY);
                            clusterMaxZ = std::max(clusterMaxZ, candidate->maxZ);
                        }
                    }
                    clusterCenter /= static_cast<float>(clusterItems.size());

                    HlodProxyEntry& proxyEntry = m_HlodProxyCache[clusterKey];
                    if (!proxyEntry.mesh || proxyEntry.signature != signature)
                    {
                        MeshData proxyMeshData;
                        proxyMeshData.vertices.reserve(clusterMeshPartCount * 256);
                        proxyMeshData.indices.reserve(clusterMeshPartCount * 512);

                        bool haveBounds = false;
                        for (const HlodCandidate* candidate : clusterItems)
                        {
                            if (!candidate->sourceMeshes)
                                continue;

                            for (size_t meshIdx = candidate->startIdx; meshIdx < candidate->endIdx &&
                                                                        meshIdx < candidate->sourceMeshes->size();
                                 ++meshIdx)
                            {
                                const MeshData& sourceMesh = (*candidate->sourceMeshes)[meshIdx];
                                if (sourceMesh.vertices.empty() || sourceMesh.indices.empty())
                                    continue;

                                const uint32_t vertexBase = static_cast<uint32_t>(proxyMeshData.vertices.size());
                                const uint32_t indexBase = static_cast<uint32_t>(proxyMeshData.indices.size());

                                proxyMeshData.vertices.reserve(proxyMeshData.vertices.size() + sourceMesh.vertices.size());
                                for (const PrimitiveVertex& sourceVertex : sourceMesh.vertices)
                                {
                                    PrimitiveVertex transformedVertex = sourceVertex;
                                    const Vec3 localPosition = candidate->worldMatrix.TransformPoint(
                                                                   Vec3(sourceVertex.x, sourceVertex.y, sourceVertex.z)) -
                                                               clusterCenter;
                                    transformedVertex.x = localPosition.x;
                                    transformedVertex.y = localPosition.y;
                                    transformedVertex.z = localPosition.z;

                                    Vec3 transformedNormal = candidate->worldMatrix.TransformDirection(
                                        Vec3(sourceVertex.nx, sourceVertex.ny, sourceVertex.nz));
                                    if (transformedNormal.LengthSquared() > 1e-8f)
                                        transformedNormal.Normalize();
                                    else
                                        transformedNormal = Vec3(sourceVertex.nx, sourceVertex.ny, sourceVertex.nz);
                                    transformedVertex.nx = transformedNormal.x;
                                    transformedVertex.ny = transformedNormal.y;
                                    transformedVertex.nz = transformedNormal.z;
                                    proxyMeshData.vertices.push_back(transformedVertex);

                                    if (!haveBounds)
                                    {
                                        proxyMeshData.boundsMinX = proxyMeshData.boundsMaxX = localPosition.x;
                                        proxyMeshData.boundsMinY = proxyMeshData.boundsMaxY = localPosition.y;
                                        proxyMeshData.boundsMinZ = proxyMeshData.boundsMaxZ = localPosition.z;
                                        haveBounds = true;
                                    }
                                    else
                                    {
                                        proxyMeshData.boundsMinX = std::min(proxyMeshData.boundsMinX, localPosition.x);
                                        proxyMeshData.boundsMinY = std::min(proxyMeshData.boundsMinY, localPosition.y);
                                        proxyMeshData.boundsMinZ = std::min(proxyMeshData.boundsMinZ, localPosition.z);
                                        proxyMeshData.boundsMaxX = std::max(proxyMeshData.boundsMaxX, localPosition.x);
                                        proxyMeshData.boundsMaxY = std::max(proxyMeshData.boundsMaxY, localPosition.y);
                                        proxyMeshData.boundsMaxZ = std::max(proxyMeshData.boundsMaxZ, localPosition.z);
                                    }
                                }

                                proxyMeshData.indices.reserve(proxyMeshData.indices.size() + sourceMesh.indices.size());
                                for (uint32_t sourceIndex : sourceMesh.indices)
                                    proxyMeshData.indices.push_back(vertexBase + sourceIndex);

                                if (sourceMesh.submeshes.empty())
                                {
                                    Submesh proxySubmesh;
                                    proxySubmesh.indexStart = indexBase;
                                    proxySubmesh.indexCount = static_cast<uint32_t>(sourceMesh.indices.size());
                                    proxyMeshData.submeshes.push_back(std::move(proxySubmesh));
                                }
                                else
                                {
                                    proxyMeshData.submeshes.reserve(proxyMeshData.submeshes.size() +
                                                                   sourceMesh.submeshes.size());
                                    for (const Submesh& sourceSubmesh : sourceMesh.submeshes)
                                    {
                                        if (sourceSubmesh.indexCount == 0)
                                            continue;
                                        Submesh proxySubmesh = sourceSubmesh;
                                        proxySubmesh.indexStart = indexBase + sourceSubmesh.indexStart;
                                        proxyMeshData.submeshes.push_back(std::move(proxySubmesh));
                                    }
                                }
                            }
                        }

                        if (!proxyMeshData.vertices.empty() && !proxyMeshData.indices.empty())
                        {
                            proxyEntry.mesh = renderer->CreateRuntimeMesh(proxyMeshData);
                            proxyEntry.signature = signature;
                            proxyEntry.worldMatrix = Mat4::Translation(clusterCenter);
                        }
                    }

                    if (proxyEntry.mesh)
                    {
                        const size_t materialIndex =
                            resolveMaterialIndex(clusterItems.front()->entity, nullptr, proxyEntry.mesh.get());
                        const uint32_t proxyPartId =
                            0x800000u | static_cast<uint32_t>(clusterKey & 0x007FFFFFu);
                        if (haveClusterBounds &&
                            passesSceneVisibility(clusterItems.front()->entity, proxyPartId, clusterMinX, clusterMinY,
                                                  clusterMinZ, clusterMaxX, clusterMaxY, clusterMaxZ))
                        {
                            appendQueuedDraw(drawQueue, proxyEntry.worldMatrix, proxyEntry.mesh.get(), materialIndex,
                                             clusterMinX, clusterMinY, clusterMinZ, clusterMaxX, clusterMaxY,
                                             clusterMaxZ);
                        }
                        continue;
                    }

                    for (const HlodCandidate* candidate : clusterItems)
                    {
                        if (!passesSceneVisibility(candidate->entity, static_cast<uint32_t>(candidate->startIdx + 1),
                                                   candidate->minX, candidate->minY, candidate->minZ, candidate->maxX,
                                                   candidate->maxY, candidate->maxZ))
                        {
                            continue;
                        }

                        const auto& meshes = getMeshesForPath(candidate->meshPath);
                        for (size_t i = candidate->startIdx; i < candidate->endIdx && i < meshes.size(); ++i)
                        {
                            PrimitiveMesh* mesh = meshes[i];
                            if (!mesh)
                                continue;
                            const size_t materialIndex = resolveMaterialIndex(candidate->entity, nullptr, mesh);
                            appendQueuedDraw(drawQueue, candidate->worldMatrix, mesh, materialIndex, candidate->minX,
                                             candidate->minY, candidate->minZ, candidate->maxX, candidate->maxY,
                                             candidate->maxZ);
                        }
                    }
                }
            }

            // Batch by material+mesh order to reduce render state churn.
            auto sortQueue = [](std::vector<RenderGraphQueuedDraw>& queue)
            {
                std::sort(queue.begin(), queue.end(),
                          [](const RenderGraphQueuedDraw& a, const RenderGraphQueuedDraw& b)
                          {
                              if (a.materialIndex != b.materialIndex)
                                  return a.materialIndex < b.materialIndex;
                              return a.mesh < b.mesh;
                          });
            };
            sortQueue(drawQueue);
            sortQueue(viewmodelDrawQueue);

            std::vector<RenderGraphMapPreviewDraw> mapPreviewDraws;
            if (mapPreviewParts)
            {
                mapPreviewDraws.reserve(mapPreviewParts->size());
                for (size_t partIndex = 0; partIndex < mapPreviewParts->size(); ++partIndex)
                {
                    const MapPreviewPart& part = (*mapPreviewParts)[partIndex];
                    if (!part.mesh)
                        continue;

                    float minX = part.mesh->boundsMinX;
                    float minY = part.mesh->boundsMinY;
                    float minZ = part.mesh->boundsMinZ;
                    float maxX = part.mesh->boundsMaxX;
                    float maxY = part.mesh->boundsMaxY;
                    float maxZ = part.mesh->boundsMaxZ;

                    const uint32_t mapPartId = 0x400000u | static_cast<uint32_t>(partIndex & 0x003FFFFFu);
                    if (!passesSceneVisibility(kNullEntity, mapPartId, minX, minY, minZ, maxX, maxY, maxZ))
                        continue;

                    RenderGraphMapPreviewDraw previewDraw;
                    previewDraw.worldMatrix = mapWorld;
                    previewDraw.mesh = part.mesh.get();
                    previewDraw.material = &part.material;
                    applyReflectionProbeToMapPreviewDraw(previewDraw, minX, minY, minZ, maxX, maxY, maxZ);
                    mapPreviewDraws.push_back(std::move(previewDraw));
                }
            }

            preparedViewmodelPass->drawQueue = viewmodelDrawQueue;
            preparedViewmodelPass->resolvedMaterials = resolvedMaterials;
            preparedViewmodelPass->antiAliasingEnabled = antiAliasingEnabled;
            preparedViewmodelPass->valid = enableViewmodelPass && !viewmodelDrawQueue.empty();

            MainSceneGraphPassExecutor::Execute(*renderer, swapChain, *camera, drawQueue, mapPreviewDraws,
                                                resolvedMaterials, antiAliasingEnabled,
                                                preparedViewmodelPass->valid, &g_DebugStats.renderCalls);

            g_DebugStats.frustumTestedObjects = frustumCullCounters.frustumTestedObjects;
            g_DebugStats.frustumChunkedObjects = frustumCullCounters.chunkedObjects;
            g_DebugStats.frustumCulledChunks = frustumCullCounters.frustumCulledChunks;
            g_DebugStats.frustumAcceptedViaChunk = frustumCullCounters.acceptedViaChunkObjects;

            // NOTE: Do not apply a second fullscreen SSAO composite here.
            // Main forward shading already samples SSAO per-pixel.
        });

    if (enableViewmodelPass)
    {
        m_FrameGraph.AddPass(
            "ViewmodelPass",
            [&sceneColorHandle, &resolvedSceneDepthHandle](FrameGraphPassBuilder& builder)
            {
                if (sceneColorHandle.IsValid())
                    builder.Read(sceneColorHandle, FrameGraphResourceUsage::ColorAttachment);
                if (resolvedSceneDepthHandle.IsValid())
                    builder.Read(resolvedSceneDepthHandle, FrameGraphResourceUsage::DepthStencilRead);
                if (sceneColorHandle.IsValid())
                    sceneColorHandle = builder.Write(sceneColorHandle, FrameGraphResourceUsage::ColorAttachment);
                if (resolvedSceneDepthHandle.IsValid())
                    resolvedSceneDepthHandle =
                        builder.Write(resolvedSceneDepthHandle, FrameGraphResourceUsage::DepthStencilWrite);
            },
            [renderer, swapChain, viewmodelCamera, preparedViewmodelPass](RHIDevice&, const FrameGraphPass&)
            {
                if (!preparedViewmodelPass->valid)
                    return;

                ViewmodelGraphPassExecutor::Execute(*renderer, swapChain, viewmodelCamera,
                                                    preparedViewmodelPass->drawQueue,
                                                    preparedViewmodelPass->resolvedMaterials,
                                                    preparedViewmodelPass->antiAliasingEnabled,
                                                    &g_DebugStats.renderCalls);
                preparedViewmodelPass->drawQueue.clear();
                preparedViewmodelPass->resolvedMaterials.clear();
                preparedViewmodelPass->valid = false;
            });
    }

    // Add Light Gizmo Pass - renders visual icons for light entities (NOT in play mode)
    m_FrameGraph.AddPass(
        "LightGizmoPass",
        [playMode, showLightGizmos, showCameraFrustums, showAttachmentSockets, showColliderGizmos, showContactPoints,
         debugVisMode,
         &sceneColorHandle](FrameGraphPassBuilder& builder)
        {
            if (!sceneColorHandle.IsValid())
                return;
            if (debugVisMode == DebugVisMode::Overdraw)
                return;

            const bool wantsLightOverlay =
                (!playMode && (showLightGizmos || showCameraFrustums || showAttachmentSockets)) || showColliderGizmos ||
                showContactPoints;
            if (!wantsLightOverlay)
                return;

            builder.Read(sceneColorHandle, FrameGraphResourceUsage::ColorAttachment);
            sceneColorHandle = builder.Write(sceneColorHandle, FrameGraphResourceUsage::ColorAttachment);
        },
        [gizmoRenderer, world, camera, swapChain, playMode, showLightGizmos, showCameraFrustums, showAttachmentSockets,
         showColliderGizmos, showContactPoints, physicsContactPoints, selectedEntity = m_SelectedEntity,
         selectedEntities = m_SelectedEntities](RHIDevice& device, const FrameGraphPass& pass)
        {
            (void)device;
            (void)pass;

            if (!gizmoRenderer)
                return;
            if (playMode && !showColliderGizmos && (!showContactPoints || physicsContactPoints.empty()))
                return;

            gizmoRenderer->Begin();

            if (showLightGizmos && !playMode)
            {
                // Draw gizmos for all light entities
                world->Each<TransformComponent>(
                    [gizmoRenderer, world](Entity entity, TransformComponent& transform)
                    {
                        const Vec3 worldPosition = GetWorldPosition(transform);
                        const Vec3 worldForward = GetWorldForward(transform);
                        float x = worldPosition.x;
                        float y = worldPosition.y;
                        float z = worldPosition.z;

                    // Directional Light - Draw sun icon with direction arrow
                    auto* dirLight = world->GetComponent<DirectionalLightComponent>(entity);
                    if (dirLight)
                    {
                        float lr = dirLight->color.x;
                        float lg = dirLight->color.y;
                        float lb = dirLight->color.z;

                        // Draw sun symbol (small sphere icon)
                        gizmoRenderer->DrawCircle(x, y, z, 0, 1, 0, 0.3f, lr, lg, lb);
                        gizmoRenderer->DrawCircle(x, y, z, 1, 0, 0, 0.3f, lr, lg, lb);
                        gizmoRenderer->DrawCircle(x, y, z, 0, 0, 1, 0.3f, lr, lg, lb);

                        // Draw direction arrow (based on entity rotation)
                        float dx = worldForward.x;
                        float dy = worldForward.y;
                        float dz = worldForward.z;
                        gizmoRenderer->DrawArrow(x, y, z, dx, dy, dz, 1.5f, lr, lg, lb);
                        return;
                    }

                    // Point Light - Draw sphere showing range
                    auto* pointLight = world->GetComponent<PointLightComponent>(entity);
                    if (pointLight)
                    {
                        float lr = pointLight->color.x;
                        float lg = pointLight->color.y;
                        float lb = pointLight->color.z;
                        float range = pointLight->range;

                        // Draw point icon (diamond shape)
                        gizmoRenderer->DrawLine(x, y + 0.2f, z, x + 0.2f, y, z, lr, lg, lb);
                        gizmoRenderer->DrawLine(x + 0.2f, y, z, x, y - 0.2f, z, lr, lg, lb);
                        gizmoRenderer->DrawLine(x, y - 0.2f, z, x - 0.2f, y, z, lr, lg, lb);
                        gizmoRenderer->DrawLine(x - 0.2f, y, z, x, y + 0.2f, z, lr, lg, lb);

                        // Draw range sphere (3 circles)
                        gizmoRenderer->DrawCircle(x, y, z, 1, 0, 0, range, lr * 0.5f, lg * 0.5f, lb * 0.5f, 0.5f);
                        gizmoRenderer->DrawCircle(x, y, z, 0, 1, 0, range, lr * 0.5f, lg * 0.5f, lb * 0.5f, 0.5f);
                        gizmoRenderer->DrawCircle(x, y, z, 0, 0, 1, range, lr * 0.5f, lg * 0.5f, lb * 0.5f, 0.5f);
                        return;
                    }

                    // Spot Light - Draw cone wireframe
                    auto* spotLight = world->GetComponent<SpotLightComponent>(entity);
                    if (spotLight)
                    {
                        float lr = spotLight->color.x;
                        float lg = spotLight->color.y;
                        float lb = spotLight->color.z;
                        float range = spotLight->range;
                        float outerAngle = spotLight->outerConeAngle * 3.14159f / 180.0f;
                        float coneRadius = range * std::tan(outerAngle);

                        const float dx = worldForward.x;
                        const float dy = worldForward.y;
                        const float dz = worldForward.z;

                        // End point of cone
                        float ex = x + dx * range;
                        float ey = y + dy * range;
                        float ez = z + dz * range;

                        // Draw cone lines
                        gizmoRenderer->DrawLine(x, y, z, ex, ey, ez, lr, lg, lb);

                        // Draw circle at cone base
                        gizmoRenderer->DrawCircle(ex, ey, ez, dx, dy, dz, coneRadius, lr * 0.7f, lg * 0.7f, lb * 0.7f,
                                                  0.7f);

                        // Draw 4 edge lines of cone
                        float perpX = 0, perpY = 1, perpZ = 0;
                        if (std::abs(dy) > 0.9f)
                        {
                            perpX = 1;
                            perpY = 0;
                            perpZ = 0;
                        }
                        // Cross product to get perpendicular
                        float p1x = dy * perpZ - dz * perpY;
                        float p1y = dz * perpX - dx * perpZ;
                        float p1z = dx * perpY - dy * perpX;
                        float len = std::sqrt(p1x * p1x + p1y * p1y + p1z * p1z);
                        if (len > 0.001f)
                        {
                            p1x /= len;
                            p1y /= len;
                            p1z /= len;
                        }

                        gizmoRenderer->DrawLine(x, y, z, ex + p1x * coneRadius, ey + p1y * coneRadius,
                                                ez + p1z * coneRadius, lr, lg, lb, 0.7f);
                        gizmoRenderer->DrawLine(x, y, z, ex - p1x * coneRadius, ey - p1y * coneRadius,
                                                ez - p1z * coneRadius, lr, lg, lb, 0.7f);
                    }
                    });
            }

            if (showAttachmentSockets && !playMode)
            {
                world->Each<TransformComponent>(
                    [gizmoRenderer, world, camera, selectedEntity, selectedEntities](Entity entity, TransformComponent& transform)
                    {
                        AttachmentPointComponent* attachmentPoint = world->GetComponent<AttachmentPointComponent>(entity);
                        if (!attachmentPoint)
                            return;

                        const Vec3 worldPosition = GetWorldPosition(transform);
                        constexpr float markerRadius = 0.12f;
                        constexpr float stemHeight = 0.18f;
                        const bool isSelected =
                            entity == selectedEntity ||
                            std::find(selectedEntities.begin(), selectedEntities.end(), entity) != selectedEntities.end();
                        const float r = isSelected ? 0.55f : 0.2f;
                        const float g = 1.0f;
                        const float b = isSelected ? 0.65f : 0.35f;

                        gizmoRenderer->DrawCircle(worldPosition.x, worldPosition.y, worldPosition.z, 0, 1, 0, markerRadius,
                                                  r, g, b);
                        gizmoRenderer->DrawLine(worldPosition.x, worldPosition.y - stemHeight * 0.5f, worldPosition.z,
                                                worldPosition.x, worldPosition.y + stemHeight * 0.5f, worldPosition.z, r, g,
                                                b, 0.9f);

                        if (!attachmentPoint->socketName.empty())
                        {
                            gizmoRenderer->DrawBillboardText(*camera, worldPosition.x,
                                                             worldPosition.y + markerRadius + 0.06f, worldPosition.z,
                                                             attachmentPoint->socketName.c_str(), 0.16f, r, g, b, 0.95f);
                        }
                    });
            }

            if (showCameraFrustums && !playMode)
            {
                // Camera frustum gizmos
                world->Each<TransformComponent>(
                    [gizmoRenderer, world](Entity entity, TransformComponent& transform)
                    {
                        // Check if this entity has a CameraComponent
                        CameraComponent* cam = world->GetComponent<CameraComponent>(entity);
                        if (!cam)
                            return;

                    const Vec3 worldPosition = GetWorldPosition(transform);
                    const Vec3 forward = GetWorldForward(transform);
                    Vec3 right = transform.worldMatrix.TransformDirection(Vec3::UnitX()).Normalized();
                    Vec3 up = transform.worldMatrix.TransformDirection(Vec3::UnitY()).Normalized();
                    float x = worldPosition.x;
                    float y = worldPosition.y;
                    float z = worldPosition.z;

                    // Camera color: green if active, cyan if inactive
                    float cr = cam->isActive ? 0.3f : 0.2f;
                    float cg = cam->isActive ? 1.0f : 0.8f;
                    float cb = cam->isActive ? 0.3f : 1.0f;

                    // Draw small circle at camera position
                    gizmoRenderer->DrawCircle(x, y, z, 0, 1, 0, 0.15f, cr, cg, cb);

                    // Calculate frustum geometry
                    // Frustum dimensions at display distance
                    const float dist = 2.0f;
                    float fovRad = cam->fov * 3.14159f / 180.0f;
                    float halfH = std::tan(fovRad * 0.5f) * dist;
                    float halfW = halfH * 1.5f; // Approximate aspect ratio

                    // Far plane center point
                    float fx = x + forward.x * dist;
                    float fy = y + forward.y * dist;
                    float fz = z + forward.z * dist;

                    // Calculate 4 corners of far plane
                    float tr_x = fx + right.x * halfW + up.x * halfH;
                    float tr_y = fy + right.y * halfW + up.y * halfH;
                    float tr_z = fz + right.z * halfW + up.z * halfH;

                    float tl_x = fx - right.x * halfW + up.x * halfH;
                    float tl_y = fy - right.y * halfW + up.y * halfH;
                    float tl_z = fz - right.z * halfW + up.z * halfH;

                    float br_x = fx + right.x * halfW - up.x * halfH;
                    float br_y = fy + right.y * halfW - up.y * halfH;
                    float br_z = fz + right.z * halfW - up.z * halfH;

                    float bl_x = fx - right.x * halfW - up.x * halfH;
                    float bl_y = fy - right.y * halfW - up.y * halfH;
                    float bl_z = fz - right.z * halfW - up.z * halfH;

                    // Draw 4 lines from camera to corners (pyramid edges)
                    gizmoRenderer->DrawLine(x, y, z, tr_x, tr_y, tr_z, cr, cg, cb, 1.0f);
                    gizmoRenderer->DrawLine(x, y, z, tl_x, tl_y, tl_z, cr, cg, cb, 1.0f);
                    gizmoRenderer->DrawLine(x, y, z, br_x, br_y, br_z, cr, cg, cb, 1.0f);
                    gizmoRenderer->DrawLine(x, y, z, bl_x, bl_y, bl_z, cr, cg, cb, 1.0f);

                    // Draw far plane rectangle
                    gizmoRenderer->DrawLine(tr_x, tr_y, tr_z, tl_x, tl_y, tl_z, cr, cg, cb, 1.0f);
                    gizmoRenderer->DrawLine(tl_x, tl_y, tl_z, bl_x, bl_y, bl_z, cr, cg, cb, 1.0f);
                        gizmoRenderer->DrawLine(bl_x, bl_y, bl_z, br_x, br_y, br_z, cr, cg, cb, 1.0f);
                        gizmoRenderer->DrawLine(br_x, br_y, br_z, tr_x, tr_y, tr_z, cr, cg, cb, 1.0f);
                    });
            }

            if (showColliderGizmos)
            {
                // Sphere Collider Gizmos
                world->Each<TransformComponent>(
                    [gizmoRenderer, world](Entity entity, TransformComponent& transform)
                    {
                        SphereColliderComponent* sphere = world->GetComponent<SphereColliderComponent>(entity);
                        if (!sphere)
                            return;

                    // Color: Bright Green
                    float r = 0.2f, g = 1.0f, b = 0.2f;

                    // World position (approximate - handles offset but ignores complex rotation/scale for now)
                    // Ideally apply full transform matrix to offset
                    const Vec3 worldCenter = transform.worldMatrix.TransformPoint(sphere->center);
                    float x = worldCenter.x;
                    float y = worldCenter.y;
                    float z = worldCenter.z;

                    // Initial scale average
                    const Vec3 worldScale = GetWorldScale(transform);
                    float scale = (worldScale.x + worldScale.y + worldScale.z) / 3.0f;
                    float radius = sphere->radius * scale;

                    // Draw 3 circles for wireframe sphere
                        gizmoRenderer->DrawCircle(x, y, z, 1, 0, 0, radius, r, g, b); // YZ plane
                        gizmoRenderer->DrawCircle(x, y, z, 0, 1, 0, radius, r, g, b); // XZ plane
                        gizmoRenderer->DrawCircle(x, y, z, 0, 0, 1, radius, r, g, b); // XY plane
                    });

                // Box Collider Gizmos
                world->Each<TransformComponent>(
                    [gizmoRenderer, world](Entity entity, TransformComponent& transform)
                    {
                        BoxColliderComponent* box = world->GetComponent<BoxColliderComponent>(entity);
                        if (!box)
                            return;

                    // Color: Bright Green
                    float r = 0.2f, g = 1.0f, b = 0.2f;

                    // Half extents scaled
                    const Vec3 worldScale = GetWorldScale(transform);
                    float hx = box->size.x * 0.5f * worldScale.x;
                    float hy = box->size.y * 0.5f * worldScale.y;
                    float hz = box->size.z * 0.5f * worldScale.z;

                    // Center position (ignoring rotation on offset for simplicity for now)
                    const Vec3 worldCenter = transform.worldMatrix.TransformPoint(box->center);
                    float cx = worldCenter.x;
                    float cy = worldCenter.y;
                    float cz = worldCenter.z;

                    // Warning: This gizmo assumes axis-aligned for now regarding rotation,
                    // supporting full OBB gizmo requires transforming all 8 corners by rotation matrix.
                    // For now, let's just use AABB approximation or basic rotation support if easy.

                    // Simple AABB lines around center
                    float minX = cx - hx;
                    float maxX = cx + hx;
                    float minY = cy - hy;
                    float maxY = cy + hy;
                    float minZ = cz - hz;
                    float maxZ = cz + hz;

                    // Bottom face
                    gizmoRenderer->DrawLine(minX, minY, minZ, maxX, minY, minZ, r, g, b);
                    gizmoRenderer->DrawLine(maxX, minY, minZ, maxX, minY, maxZ, r, g, b);
                    gizmoRenderer->DrawLine(maxX, minY, maxZ, minX, minY, maxZ, r, g, b);
                    gizmoRenderer->DrawLine(minX, minY, maxZ, minX, minY, minZ, r, g, b);

                    // Top face
                    gizmoRenderer->DrawLine(minX, maxY, minZ, maxX, maxY, minZ, r, g, b);
                    gizmoRenderer->DrawLine(maxX, maxY, minZ, maxX, maxY, maxZ, r, g, b);
                    gizmoRenderer->DrawLine(maxX, maxY, maxZ, minX, maxY, maxZ, r, g, b);
                    gizmoRenderer->DrawLine(minX, maxY, maxZ, minX, maxY, minZ, r, g, b);

                    // Verticals
                    gizmoRenderer->DrawLine(minX, minY, minZ, minX, maxY, minZ, r, g, b);
                    gizmoRenderer->DrawLine(maxX, minY, minZ, maxX, maxY, minZ, r, g, b);
                    gizmoRenderer->DrawLine(maxX, minY, maxZ, maxX, maxY, maxZ, r, g, b);
                        gizmoRenderer->DrawLine(minX, minY, maxZ, minX, maxY, maxZ, r, g, b);
                    });
            }

            if (showContactPoints)
            {
                constexpr float markerRadius = 0.06f;
                constexpr float normalLength = 0.18f;
                for (const PhysicsContactDebugPoint& contact : physicsContactPoints)
                {
                    const Vec3 normal = contact.normal.LengthSquared() > 1e-6f ? contact.normal.Normalized() : Vec3::Up();
                    gizmoRenderer->DrawLine(contact.point.x - markerRadius, contact.point.y, contact.point.z,
                                            contact.point.x + markerRadius, contact.point.y, contact.point.z, 1.0f, 0.55f, 0.1f);
                    gizmoRenderer->DrawLine(contact.point.x, contact.point.y - markerRadius, contact.point.z,
                                            contact.point.x, contact.point.y + markerRadius, contact.point.z, 1.0f, 0.55f, 0.1f);
                    gizmoRenderer->DrawLine(contact.point.x, contact.point.y, contact.point.z - markerRadius,
                                            contact.point.x, contact.point.y, contact.point.z + markerRadius, 1.0f, 0.55f, 0.1f);
                    gizmoRenderer->DrawLine(contact.point.x, contact.point.y, contact.point.z,
                                            contact.point.x + normal.x * normalLength, contact.point.y + normal.y * normalLength,
                                            contact.point.z + normal.z * normalLength, 1.0f, 0.85f, 0.2f);
                }
            }

            gizmoRenderer->End(*camera, swapChain);
        });

    // Add Gizmo Pass - keeps scene debug gizmos depth-tested while drawing transform handles as an overlay.
    m_FrameGraph.AddPass(
        "GizmoPass",
        [playMode, debugVisMode, &sceneColorHandle, &resolvedSceneDepthHandle](FrameGraphPassBuilder& builder)
        {
            if (playMode || !sceneColorHandle.IsValid() || debugVisMode == DebugVisMode::Overdraw)
                return;

            builder.Read(sceneColorHandle, FrameGraphResourceUsage::ColorAttachment);
            if (resolvedSceneDepthHandle.IsValid())
                builder.Read(resolvedSceneDepthHandle, FrameGraphResourceUsage::DepthStencilRead);
            sceneColorHandle = builder.Write(sceneColorHandle, FrameGraphResourceUsage::ColorAttachment);
        },
        [gizmoRenderer, overlayGizmoRenderer, gridRenderer, world, camera, swapChain, selectedEntity,
         selectedEntities = m_SelectedEntities, transformSpace, activeGizmo, playMode, mapEditingEnabled,
         mapClipToolActive, mapClipPreviewOffset, mapClipPreviewFlipPlane,
         mapClipPreviewHovered = m_MapClipPreviewHovered, mapClipDragging = m_MapClipDragging,
         mapExtrudeToolActive, mapExtrudePreviewDistance, mapExtrudePreviewHovered = m_MapExtrudePreviewHovered,
         mapExtrudeDragging = m_MapExtrudeDragging, mapHollowPreviewActive, mapHollowPreviewThickness,
         entityMarqueePending = m_EntityMarqueePending, entityMarqueeActive = m_EntityMarqueeActive,
         entityMarqueeStart = m_EntityMarqueeStart, entityMarqueeEnd = m_EntityMarqueeEnd,
         mapMarqueePending = m_MapMarqueePending, mapMarqueeActive = m_MapMarqueeActive,
         mapMarqueeStart = m_MapMarqueeStart, mapMarqueeEnd = m_MapMarqueeEnd, viewportX = m_ViewportX,
         viewportY = m_ViewportY, viewportWidth = m_ViewportWidth, viewportHeight = m_ViewportHeight,
         mapSelectionValid, mapGizmo, mapGizmoPivot, mapDocument = m_MapDocument, showSelectionGizmo,
         showNavMeshGizmo, navMeshDebugLines](RHIDevice& device, const FrameGraphPass& pass)
        {
            (void)device;
            (void)pass;

            ViewportOverlayPassContext overlayContext;
            overlayContext.gizmoRenderer = gizmoRenderer;
            overlayContext.overlayGizmoRenderer = overlayGizmoRenderer;
            overlayContext.gridRenderer = gridRenderer;
            overlayContext.world = world;
            overlayContext.camera = camera;
            overlayContext.swapChain = swapChain;
            overlayContext.playMode = playMode;
            overlayContext.showSelectionGizmo = showSelectionGizmo;
            overlayContext.showNavMeshGizmo = showNavMeshGizmo;
            overlayContext.mapEditingEnabled = mapEditingEnabled;
            overlayContext.mapClipToolActive = mapClipToolActive;
            overlayContext.mapClipPreviewFlipPlane = mapClipPreviewFlipPlane;
            overlayContext.mapClipPreviewHovered = mapClipPreviewHovered;
            overlayContext.mapClipDragging = mapClipDragging;
            overlayContext.mapExtrudeToolActive = mapExtrudeToolActive;
            overlayContext.mapExtrudePreviewHovered = mapExtrudePreviewHovered;
            overlayContext.mapExtrudeDragging = mapExtrudeDragging;
            overlayContext.mapHollowPreviewActive = mapHollowPreviewActive;
            overlayContext.entityMarqueePending = entityMarqueePending;
            overlayContext.entityMarqueeActive = entityMarqueeActive;
            overlayContext.mapMarqueePending = mapMarqueePending;
            overlayContext.mapMarqueeActive = mapMarqueeActive;
            overlayContext.mapSelectionValid = mapSelectionValid;
            overlayContext.useLocalTransformSpace = transformSpace == TransformSpace::Local;
            overlayContext.mapClipPreviewOffset = mapClipPreviewOffset;
            overlayContext.mapExtrudePreviewDistance = mapExtrudePreviewDistance;
            overlayContext.mapHollowPreviewThickness = mapHollowPreviewThickness;
            overlayContext.viewportX = viewportX;
            overlayContext.viewportY = viewportY;
            overlayContext.viewportWidth = viewportWidth;
            overlayContext.viewportHeight = viewportHeight;
            overlayContext.entityMarqueeStart = entityMarqueeStart;
            overlayContext.entityMarqueeEnd = entityMarqueeEnd;
            overlayContext.mapMarqueeStart = mapMarqueeStart;
            overlayContext.mapMarqueeEnd = mapMarqueeEnd;
            overlayContext.selectedEntity = selectedEntity;
            overlayContext.selectedEntities = &selectedEntities;
            overlayContext.activeGizmo = activeGizmo;
            overlayContext.mapGizmo = mapGizmo;
            overlayContext.mapGizmoPivot = mapGizmoPivot;
            overlayContext.mapDocument = mapDocument;
            overlayContext.navMeshDebugLines = &navMeshDebugLines;
            RenderViewportOverlayPass(overlayContext);
        });

    m_FrameGraph.AddPass(
        "FinalCompositePass",
        [showSSAODebugFullscreen, &sceneColorHandle, &ssaoBlurHandle, &backBufferHandle, &finalBackBufferHandle](
            FrameGraphPassBuilder& builder)
        {
            if (sceneColorHandle.IsValid())
                builder.Read(sceneColorHandle, FrameGraphResourceUsage::ShaderRead);
            if (showSSAODebugFullscreen && ssaoBlurHandle.IsValid())
                builder.Read(ssaoBlurHandle, FrameGraphResourceUsage::ShaderRead);
            finalBackBufferHandle = builder.Write(backBufferHandle, FrameGraphResourceUsage::ColorAttachment);
        },
        [renderer, swapChain, showSSAODebugFullscreen](RHIDevice&, const FrameGraphPass&)
        {
            if (showSSAODebugFullscreen)
                SSAODebugCompositeExecutor::Execute(*renderer, swapChain);
        });

    m_FrameGraph.AddPass(
        "PresentPass",
        [&finalBackBufferHandle, &presentHandle](FrameGraphPassBuilder& builder)
        {
            presentHandle = builder.Write(finalBackBufferHandle, FrameGraphResourceUsage::Present);
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    if (presentHandle.IsValid())
        m_FrameGraph.SetOutput(presentHandle);
    else if (finalBackBufferHandle.IsValid())
        m_FrameGraph.SetOutput(finalBackBufferHandle);

    // Reset renderer frame-local upload state once before any graph pass runs.
    renderer->BeginFrame();

    // Compile and execute the frame graph
    m_FrameGraph.Compile();
    m_FrameGraph.Execute(*m_Device);
    ExecutePendingReflectionProbeBake(swapChain);
}

void ViewportPanel::ApplyEntitySelectionInteraction(const std::vector<Entity>& entities, SelectionInteraction interaction)
{
    if (interaction == SelectionInteraction::Replace)
    {
        SetSelectedEntities(entities, entities.empty() ? kNullEntity : entities.back());
        return;
    }

    std::vector<Entity> updatedSelection = GetValidSelectedEntities();
    Entity primaryEntity = m_SelectedEntity;

    for (Entity entity : entities)
    {
        auto it = std::find(updatedSelection.begin(), updatedSelection.end(), entity);
        if (interaction == SelectionInteraction::Add)
        {
            if (it == updatedSelection.end())
            {
                updatedSelection.push_back(entity);
                primaryEntity = entity;
            }
        }
        else if (interaction == SelectionInteraction::Toggle)
        {
            if (it != updatedSelection.end())
            {
                updatedSelection.erase(it);
                if (primaryEntity == entity)
                    primaryEntity = updatedSelection.empty() ? kNullEntity : updatedSelection.back();
            }
            else
            {
                updatedSelection.push_back(entity);
                primaryEntity = entity;
            }
        }
    }

    SetSelectedEntities(updatedSelection, primaryEntity);
}

bool ViewportPanel::TrySelectEntity(const Ray& ray, SelectionInteraction interaction)
{
    Entity closestEntity = kNullEntity;
    float closestDist = 1e30f;

    m_World->Each<TransformComponent>(
        [&](Entity entity, TransformComponent& transform)
        {
            // Create AABB for the entity (assume 1 unit cube for now)
            const Vec3 worldPosition = GetWorldPosition(transform);
            float halfSize = 0.5f;
            AABB box =
                AABB::FromCenterSize(worldPosition.x, worldPosition.y, worldPosition.z, halfSize, halfSize, halfSize);

            float tMin, tMax;
            if (RayAABBIntersect(ray, box, tMin, tMax) && tMin < closestDist)
            {
                closestDist = tMin;
                closestEntity = entity;
            }
        });

    if (closestEntity.IsValid())
    {
        ApplyEntitySelectionInteraction({closestEntity}, interaction);
        return true;
    }

    if (interaction == SelectionInteraction::Replace)
        SetSelectedEntity(kNullEntity);
    return false;
}

bool ViewportPanel::TrySelectMapElement(const Ray& ray, SelectionInteraction interaction)
{
    if (!m_MapDocument)
        return false;

    MapSelection selection;
    if (m_MapDocument->Pick(ray, selection))
    {
        switch (interaction)
        {
        case SelectionInteraction::Replace:
            m_MapDocument->SetSelection(selection);
            break;
        case SelectionInteraction::Add:
            m_MapDocument->AddSelection(selection);
            break;
        case SelectionInteraction::Toggle:
            m_MapDocument->ToggleSelection(selection);
            break;
        }
        UpdateMapGizmoPlacement();
        return true;
    }

    if (interaction == SelectionInteraction::Replace)
        m_MapDocument->ClearSelection();
    return false;
}

void ViewportPanel::HandleMouseInput()
{
    ViewportInteractionController::HandleInput(*this);
}

void ViewportPanel::OnImGui()
{
    if (!m_Open)
        return;

    SyncSelectionFromContext();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    BeginChromeWindow(m_Name.c_str(), &m_Open);

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();

    m_ViewportWidth = viewportSize.x;
    m_ViewportHeight = viewportSize.y;
    m_ViewportX = viewportPos.x;
    m_ViewportY = viewportPos.y;

    ImGui::InvisibleButton("##viewport", viewportSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    m_ViewportHovered = ImGui::IsItemHovered();
    m_ViewportActive = ImGui::IsItemActive();

    // Model Asset Drag and Drop Target
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET"))
        {
            const char* pathStr = (const char*)payload->Data;
            std::string relativePath = pathStr;

            // Create Entity at Drop Position (intersect with grid Y=0)
            ImGuiIO& io = ImGui::GetIO();
            Ray ray =
                RayPicker::ScreenToWorldRay(io.MousePos.x, io.MousePos.y, io.DisplaySize.x, io.DisplaySize.y, m_Camera);

            // Ray-Plane intersection (P_y = 0)
            // Origin.y + t * Dir.y = 0  => t = -Origin.y / Dir.y
            float t = 0.0f;
            if (std::abs(ray.direction.y) > 0.0001f)
            {
                t = -ray.origin.y / ray.direction.y;
            }
            // Clamp t to a reasonable forward distance if pointing away or almost level
            if (t <= 0.0f)
                t = 5.0f;

            Vec3 dropPos = ray.GetPoint(t);

            // Instantiate Model
            std::filesystem::path modelPath(relativePath);
            std::string entityName = modelPath.stem().string();
            // Check extension
            std::string ext = modelPath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            if (ext == ".fbx")
            {
                ImportFbxMaterialsForAsset(relativePath);
                const std::vector<std::string> materialPaths = GetFbxSubmeshMaterialPaths(relativePath);
                // Get submesh count
                size_t submeshCount = GetFbxSubmeshCountForAsset(relativePath);

                if (submeshCount <= 1)
                {
                    // Single submesh - create single entity
                    Entity entity = m_World->CreateEntity();
                    auto& nameComp = m_World->AddComponent<NameComponent>(entity);
                    nameComp.name = entityName;

                    auto& transform = m_World->AddComponent<TransformComponent>(entity);
                    transform.position = dropPos;

                    auto& meshComp = m_World->AddComponent<MeshComponent>(entity);
                    meshComp.meshPath = relativePath;
                    meshComp.submeshIndex = 0;
                    meshComp.isLoaded = false;
                    AutoAssignImportedMeshMaterial(*m_World, entity, materialPaths, 0);

                    SetSelectedEntity(entity);
                }
                else
                {
                    Entity entity = m_World->CreateEntity();
                    auto& nameComp = m_World->AddComponent<NameComponent>(entity);
                    nameComp.name = entityName;

                    auto& transform = m_World->AddComponent<TransformComponent>(entity);
                    transform.position = dropPos;

                    auto& meshComp = m_World->AddComponent<MeshComponent>(entity);
                    meshComp.meshPath = relativePath;
                    meshComp.submeshIndex = -1;
                    meshComp.isLoaded = false;
                    ClearExplicitMeshMaterialOverride(*m_World, entity);

                    SetSelectedEntity(entity);
                }

                DOT_LOG_INFO("Viewport: Imported FBX as single entity with %zu submeshes: %s", submeshCount,
                             relativePath.c_str());
            }
            else
            {
                // Non-FBX (OBJ, etc.) - single entity
                Entity entity = m_World->CreateEntity();
                auto& nameComp = m_World->AddComponent<NameComponent>(entity);
                nameComp.name = entityName;

                auto& transform = m_World->AddComponent<TransformComponent>(entity);
                transform.position = dropPos;

                auto& meshComp = m_World->AddComponent<MeshComponent>(entity);
                meshComp.meshPath = relativePath;
                meshComp.isLoaded = false;

                SetSelectedEntity(entity);
                DOT_LOG_INFO("Viewport: Imported model via drag-and-drop: %s", relativePath.c_str());
            }
        }
        ImGui::EndDragDropTarget();
    }

    HandleMouseInput();

    // Draw toolbar overlay at top of viewport
    DrawToolbar();


    // Draw selection info
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    char infoText[128];
    if (m_MapEditingEnabled && m_MapDocument)
    {
        const MapSelection& selection = m_MapDocument->GetSelection();
        const size_t selectionCount = m_MapDocument->GetSelections().size();
        if (selection.brushId != 0)
        {
            if (selectionCount > 1)
            {
                snprintf(infoText, sizeof(infoText), "Map Selection: %zu %s", selectionCount,
                         GetMapSelectionKindLabel(selection, selectionCount));
            }
            else if (selection.faceIndex >= 0)
                snprintf(infoText, sizeof(infoText), "Map Selection: Brush %u Face %d", selection.brushId, selection.faceIndex);
            else if (selection.vertexIndex >= 0)
                snprintf(infoText, sizeof(infoText), "Map Selection: Brush %u Vertex %d", selection.brushId,
                         selection.vertexIndex);
            else if (selection.edgeVertexA >= 0 && selection.edgeVertexB >= 0)
                snprintf(infoText, sizeof(infoText), "Map Selection: Brush %u Edge %d-%d", selection.brushId,
                         selection.edgeVertexA, selection.edgeVertexB);
            else
                snprintf(infoText, sizeof(infoText), "Map Selection: Brush %u", selection.brushId);
        }
        else
        {
            snprintf(infoText, sizeof(infoText), "Map Mode - Click to select | 1-4 modes | W move | Shift+A box");
        }
    }
    else if (!m_SelectedEntities.empty())
    {
        if (m_SelectedEntities.size() > 1)
        {
            const Vec3 pivot = ComputeSelectionPivot();
            snprintf(infoText, sizeof(infoText), "Selected: %zu entities (Pivot %.1f, %.1f, %.1f)",
                     m_SelectedEntities.size(), pivot.x, pivot.y, pivot.z);
        }
        else if (m_SelectedEntity.IsValid())
        {
            auto* name = m_World->GetComponent<NameComponent>(m_SelectedEntity);
            auto* transform = m_World->GetComponent<TransformComponent>(m_SelectedEntity);
            if (name && transform)
            {
                snprintf(infoText, sizeof(infoText), "Selected: %s (%.1f, %.1f, %.1f)", name->name.c_str(),
                         transform->position.x, transform->position.y, transform->position.z);
            }
            else
            {
                snprintf(infoText, sizeof(infoText), "Selected: Entity %u", m_SelectedEntity.GetIndex());
            }
        }
    }
    else
    {
        snprintf(infoText, sizeof(infoText), "No selection - Click to select");
    }
    drawList->AddText(ImVec2(viewportPos.x + 10, viewportPos.y + 40), IM_COL32(200, 200, 200, 255), infoText);

    // Entity count
    char countText[64];
    snprintf(countText, sizeof(countText), "Entities: %zu", m_World->GetEntityCount());
    drawList->AddText(ImVec2(viewportPos.x + 10, viewportPos.y + 58), IM_COL32(100, 200, 100, 255), countText);

    float overlayY = viewportPos.y + 76.0f;
    char frameGraphText[128];
    snprintf(frameGraphText, sizeof(frameGraphText), "FrameGraph: %.2f / %.2f ms", m_FrameGraph.GetLastCompileTimeMs(),
             m_FrameGraph.GetLastExecuteTimeMs());
    drawList->AddText(ImVec2(viewportPos.x + 10, overlayY), IM_COL32(140, 190, 255, 255), frameGraphText);
    overlayY += 18.0f;

    std::vector<const FrameGraphPass*> gpuPasses;
    gpuPasses.reserve(m_FrameGraph.GetPassCount());
    for (const FrameGraphPass& pass : m_FrameGraph.GetPasses())
    {
        if (!pass.culled && pass.gpuTimeMs > 0.0)
            gpuPasses.push_back(&pass);
    }
    std::sort(gpuPasses.begin(), gpuPasses.end(),
              [](const FrameGraphPass* a, const FrameGraphPass* b) { return a->gpuTimeMs > b->gpuTimeMs; });
    const size_t gpuPassCount = (std::min)(gpuPasses.size(), static_cast<size_t>(3));
    for (size_t i = 0; i < gpuPassCount; ++i)
    {
        char gpuPassText[160];
        snprintf(gpuPassText, sizeof(gpuPassText), "GPU %zu: %s %.2f ms", i + 1, gpuPasses[i]->name.c_str(),
                 gpuPasses[i]->gpuTimeMs);
        drawList->AddText(ImVec2(viewportPos.x + 10, overlayY), IM_COL32(255, 210, 120, 255), gpuPassText);
        overlayY += 18.0f;
    }

    char cullingText[160];
    snprintf(cullingText, sizeof(cullingText), "Cull: frustum %d tests | HZB %d / %d",
             g_DebugStats.frustumTestedObjects, g_DebugStats.hzbCulled, g_DebugStats.hzbTests);
    drawList->AddText(ImVec2(viewportPos.x + 10, overlayY), IM_COL32(150, 230, 170, 255), cullingText);
    overlayY += 18.0f;

    if (!m_MapEditingEnabled && m_SelectedEntity.IsValid() && m_World && m_Renderer)
    {
        World& world = *m_World;
        const Camera& sceneCamera = m_PlayMode ? m_PlayCamera : m_Camera;
        Entity activeCameraEntity = FindActiveCameraEntity(world);
        CameraComponent* activeSceneCamera =
            activeCameraEntity.IsValid() ? world.GetComponent<CameraComponent>(activeCameraEntity) : nullptr;
        const uint32 sceneRenderMask = (m_PlayMode && activeSceneCamera) ? activeSceneCamera->renderMask : 0xFFFFFFFFu;
        const bool enableViewmodelPass =
            m_PlayMode && activeSceneCamera && activeSceneCamera->enableViewmodelPass && activeSceneCamera->viewmodelMask != 0;
        const uint32 viewmodelRenderMask = enableViewmodelPass ? activeSceneCamera->viewmodelMask : 0u;

        TransformComponent* transform = world.GetComponent<TransformComponent>(m_SelectedEntity);
        PrimitiveComponent* primitive = world.GetComponent<PrimitiveComponent>(m_SelectedEntity);
        MeshComponent* meshComp = world.GetComponent<MeshComponent>(m_SelectedEntity);
        MaterialComponent* material = world.GetComponent<MaterialComponent>(m_SelectedEntity);

        if (transform)
        {
            const bool renderInScene = MatchesRenderMask(world, m_SelectedEntity, sceneRenderMask);
            const bool renderInViewmodel = enableViewmodelPass && MatchesRenderMask(world, m_SelectedEntity, viewmodelRenderMask);
            const char* visibilityLabel = renderInScene && renderInViewmodel ? "scene + viewmodel"
                                         : renderInScene            ? "scene"
                                         : renderInViewmodel        ? "viewmodel"
                                                                    : "masked";

            Camera diagnosticsCamera = sceneCamera;
            if (renderInViewmodel && !renderInScene && activeSceneCamera)
            {
                diagnosticsCamera.SetPerspective(activeSceneCamera->viewmodelFov, sceneCamera.GetAspect(),
                                                 activeSceneCamera->viewmodelNearPlane, activeSceneCamera->farPlane);
            }

            float camX = 0.0f, camY = 0.0f, camZ = 0.0f;
            sceneCamera.GetPosition(camX, camY, camZ);
            const Camera::Frustum shadowFrustum = m_Renderer->ComputeShadowFrustum(sceneCamera);

            constexpr float kHlodClusterDistance = 80.0f;
            constexpr float kShadowSelectiveMaxDistance = 140.0f;
            constexpr float kSmallObjectExtent = 2.0f;

            auto formatShadowDecision = [&](char* buffer, size_t bufferSize, float minX, float minY, float minZ,
                                            float maxX, float maxY, float maxZ, bool shadowCasterEnabled,
                                            const char* disabledReason)
            {
                if (!ViewSettings::Get().shadowsEnabled)
                {
                    snprintf(buffer, bufferSize, "Shadow: disabled globally");
                    return;
                }
                if (!shadowCasterEnabled)
                {
                    snprintf(buffer, bufferSize, "Shadow: %s", disabledReason);
                    return;
                }
                if (!renderInScene)
                {
                    snprintf(buffer, bufferSize, "Shadow: skipped by scene render mask");
                    return;
                }

                const float cx = (minX + maxX) * 0.5f;
                const float cy = (minY + maxY) * 0.5f;
                const float cz = (minZ + maxZ) * 0.5f;
                const float extent = (std::max)({maxX - minX, maxY - minY, maxZ - minZ});
                const float dx = cx - camX;
                const float dy = cy - camY;
                const float dz = cz - camZ;
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (distance > kShadowSelectiveMaxDistance && extent < kSmallObjectExtent)
                {
                    snprintf(buffer, bufferSize, "Shadow: directional culled (small + far) | local eligible");
                    return;
                }

                const bool directionalVisible = shadowFrustum.TestAABB(minX, minY, minZ, maxX, maxY, maxZ);
                snprintf(buffer, bufferSize, "Shadow: directional %s | local eligible",
                         directionalVisible ? "submitted" : "culled");
            };

            char renderDiagText[256];
            char lodDiagText[256];
            char hlodDiagText[256];
            char shadowDiagText[256];

            if (primitive)
            {
                PrimitiveMesh* mesh = m_Renderer->GetPrimitiveMesh(primitive->type);
                snprintf(renderDiagText, sizeof(renderDiagText), "RenderDiag: Primitive %s | %s",
                         GetPrimitiveTypeName(primitive->type), visibilityLabel);

                if (mesh)
                {
                    float lod1Threshold = 0.0f;
                    float lod2Threshold = 0.0f;
                    GetPrimitiveLodScreenHeightThresholds(*primitive, lod1Threshold, lod2Threshold);
                    const float screenHeight =
                        EstimateProjectedScreenHeightForDiagnostics(diagnosticsCamera, *mesh, *transform);
                    const uint32_t lodBand = (screenHeight < lod2Threshold) ? 2u : ((screenHeight < lod1Threshold) ? 1u : 0u);
                    const uint32_t geometryLod =
                        ChooseMeshLodLevelForDiagnostics(diagnosticsCamera, *mesh, *transform, lod1Threshold, lod2Threshold);
                    snprintf(lodDiagText, sizeof(lodDiagText),
                             "LOD: screen %.3f -> band L%u, geom L%u | thresholds %.3f / %.3f%s", screenHeight, lodBand,
                             geometryLod, lod1Threshold, lod2Threshold,
                             primitive->overrideLodThresholds ? " custom" : " default");
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: n/a for primitives");

                    float minX = 0.0f, minY = 0.0f, minZ = 0.0f, maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
                    ComputeWorldBoundsForDiagnostics(*transform, *mesh, minX, minY, minZ, maxX, maxY, maxZ);
                    formatShadowDecision(shadowDiagText, sizeof(shadowDiagText), minX, minY, minZ, maxX, maxY, maxZ,
                                         mesh->indexCount > 0, "unavailable");
                }
                else
                {
                    snprintf(lodDiagText, sizeof(lodDiagText), "LOD: unavailable (primitive mesh missing)");
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: n/a for primitives");
                    snprintf(shadowDiagText, sizeof(shadowDiagText), "Shadow: unavailable (primitive mesh missing)");
                }
            }
            else if (meshComp && !meshComp->meshPath.empty())
            {
                const auto meshes = m_Renderer->LoadMesh(meshComp->meshPath);
                std::string meshName = std::filesystem::path(meshComp->meshPath).filename().string();
                if (meshName.empty())
                    meshName = meshComp->meshPath;
                snprintf(renderDiagText, sizeof(renderDiagText), "RenderDiag: Mesh %s | %s", meshName.c_str(), visibilityLabel);

                size_t startIdx = 0;
                size_t endIdx = meshes.size();
                if (meshComp->submeshIndex >= 0 && static_cast<size_t>(meshComp->submeshIndex) < meshes.size())
                {
                    startIdx = static_cast<size_t>(meshComp->submeshIndex);
                    endIdx = startIdx + 1;
                }

                PrimitiveMesh* representativeMesh = nullptr;
                float maxExtent = -1.0f;
                float minX = 0.0f, minY = 0.0f, minZ = 0.0f, maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
                bool haveBounds = false;
                for (size_t i = startIdx; i < endIdx && i < meshes.size(); ++i)
                {
                    PrimitiveMesh* mesh = meshes[i];
                    if (!mesh)
                        continue;

                    if (mesh->GetMaxExtent() > maxExtent)
                    {
                        maxExtent = mesh->GetMaxExtent();
                        representativeMesh = mesh;
                    }

                    float meshMinX = 0.0f, meshMinY = 0.0f, meshMinZ = 0.0f, meshMaxX = 0.0f, meshMaxY = 0.0f,
                          meshMaxZ = 0.0f;
                    ComputeWorldBoundsForDiagnostics(*transform, *mesh, meshMinX, meshMinY, meshMinZ, meshMaxX, meshMaxY,
                                                     meshMaxZ);
                    if (!haveBounds)
                    {
                        minX = meshMinX;
                        minY = meshMinY;
                        minZ = meshMinZ;
                        maxX = meshMaxX;
                        maxY = meshMaxY;
                        maxZ = meshMaxZ;
                        haveBounds = true;
                    }
                    else
                    {
                        minX = (std::min)(minX, meshMinX);
                        minY = (std::min)(minY, meshMinY);
                        minZ = (std::min)(minZ, meshMinZ);
                        maxX = (std::max)(maxX, meshMaxX);
                        maxY = (std::max)(maxY, meshMaxY);
                        maxZ = (std::max)(maxZ, meshMaxZ);
                    }
                }

                if (representativeMesh)
                {
                    const float lod1Threshold =
                        (std::max)(representativeMesh->lodScreenHeightThresholds[1] * ViewSettings::Get().lodAggressiveness, 0.0f);
                    const float lod2Threshold = std::clamp(
                        representativeMesh->lodScreenHeightThresholds[2] * ViewSettings::Get().lodAggressiveness, 0.0f,
                        lod1Threshold);
                    const float screenHeight =
                        EstimateProjectedScreenHeightForDiagnostics(diagnosticsCamera, *representativeMesh, *transform);
                    const uint32_t lodBand = (screenHeight < lod2Threshold) ? 2u : ((screenHeight < lod1Threshold) ? 1u : 0u);
                    const uint32_t geometryLod = ChooseMeshLodLevelForDiagnostics(
                        diagnosticsCamera, *representativeMesh, *transform, lod1Threshold, lod2Threshold);
                    snprintf(lodDiagText, sizeof(lodDiagText),
                             "LOD: screen %.3f -> band L%u, geom L%u | thresholds %.3f / %.3f | parts %zu", screenHeight,
                             lodBand, geometryLod, lod1Threshold, lod2Threshold, endIdx - startIdx);
                }
                else
                {
                    snprintf(lodDiagText, sizeof(lodDiagText), "LOD: unavailable (mesh not loaded)");
                }

                if (!renderInScene)
                {
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: skipped by scene render mask");
                }
                else if (renderInViewmodel)
                {
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: disabled by viewmodel rendering");
                }
                else if (material)
                {
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: disabled by material override");
                }
                else if (meshComp->submeshIndex >= 0)
                {
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: disabled by submesh selection");
                }
                else if (haveBounds)
                {
                    const float centerX = (minX + maxX) * 0.5f;
                    const float centerY = (minY + maxY) * 0.5f;
                    const float centerZ = (minZ + maxZ) * 0.5f;
                    const float dx = centerX - camX;
                    const float dy = centerY - camY;
                    const float dz = centerZ - camZ;
                    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (distance >= kHlodClusterDistance)
                    {
                        snprintf(hlodDiagText, sizeof(hlodDiagText),
                                 "HLOD: bypassed for selected entity at %.1fm (threshold %.0fm)", distance,
                                 kHlodClusterDistance);
                    }
                    else
                    {
                        snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: source mesh kept (%.1fm < %.0fm)",
                                 distance, kHlodClusterDistance);
                    }
                }
                else
                {
                    snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: unavailable (bounds missing)");
                }

                if (haveBounds)
                {
                    formatShadowDecision(shadowDiagText, sizeof(shadowDiagText), minX, minY, minZ, maxX, maxY, maxZ,
                                         meshComp->castShadow && representativeMesh != nullptr,
                                         meshComp->castShadow ? "unavailable" : "disabled on mesh component");
                }
                else
                {
                    snprintf(shadowDiagText, sizeof(shadowDiagText), "Shadow: unavailable (bounds missing)");
                }
            }
            else
            {
                snprintf(renderDiagText, sizeof(renderDiagText), "RenderDiag: non-renderable entity");
                snprintf(lodDiagText, sizeof(lodDiagText), "LOD: n/a");
                snprintf(hlodDiagText, sizeof(hlodDiagText), "HLOD: n/a");
                snprintf(shadowDiagText, sizeof(shadowDiagText), "Shadow: n/a");
            }

            drawList->AddText(ImVec2(viewportPos.x + 10, overlayY + 4.0f), IM_COL32(170, 220, 255, 255), renderDiagText);
            drawList->AddText(ImVec2(viewportPos.x + 10, overlayY + 22.0f), IM_COL32(255, 230, 150, 255), lodDiagText);
            drawList->AddText(ImVec2(viewportPos.x + 10, overlayY + 40.0f), IM_COL32(180, 255, 200, 255), hlodDiagText);
            drawList->AddText(ImVec2(viewportPos.x + 10, overlayY + 58.0f), IM_COL32(255, 190, 190, 255), shadowDiagText);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void ViewportPanel::DrawToolbar()
{
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentMin = ImGui::GetWindowContentRegionMin();

    // Position toolbar at top of viewport content area
    ImGui::SetCursorPos(ImVec2(contentMin.x + 5, contentMin.y + 5));

    // Use a child window for toolbar styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.12f, 0.9f));

    ImGui::BeginChild("##viewport_toolbar", ImVec2(0, 32), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_MapEditingEnabled)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button("W", ImVec2(24, 24)))
            m_GizmoMode = GizmoMode::Translate;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move Selection (W)");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("1 Brush  2 Face  3 Edge  4 Vertex  Shift+A Box  Del Delete  Ctrl+D Duplicate");
    }
    else
    {
        // Transform mode buttons
        ImGui::PushStyleColor(ImGuiCol_Button, m_GizmoMode == GizmoMode::Translate ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f)
                                                                                   : ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        if (ImGui::Button("W", ImVec2(24, 24)))
            m_GizmoMode = GizmoMode::Translate;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Translate (W)");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, m_GizmoMode == GizmoMode::Rotate ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f)
                                                                                : ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        if (ImGui::Button("E", ImVec2(24, 24)))
            m_GizmoMode = GizmoMode::Rotate;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rotate (E)");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, m_GizmoMode == GizmoMode::Scale ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f)
                                                                               : ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        if (ImGui::Button("R", ImVec2(24, 24)))
            m_GizmoMode = GizmoMode::Scale;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale (R)");
        ImGui::PopStyleColor();

        // Separator
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");

        // Local/World toggle
        ImGui::SameLine();
        const char* spaceLabel = (m_TransformSpace == TransformSpace::Local) ? "Local" : "World";
        ImVec4 spaceColor = (m_TransformSpace == TransformSpace::Local) ? ImVec4(0.6f, 0.4f, 0.2f, 1.0f)
                                                                        : ImVec4(0.2f, 0.6f, 0.4f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, spaceColor);
        std::string spaceButtonLabel = std::string(spaceLabel) + "##TransformSpace";
        if (ImGui::Button(spaceButtonLabel.c_str(), ImVec2(50, 24)))
        {
            m_TransformSpace = (m_TransformSpace == TransformSpace::Local) ? TransformSpace::World : TransformSpace::Local;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toggle Local/World Transform Space");
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    ImGui::PopStyleColor(); // ChildBg
    ImGui::PopStyleVar(2);  // WindowPadding, ItemSpacing
}

} // namespace Dot

