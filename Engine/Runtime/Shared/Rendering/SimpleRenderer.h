// =============================================================================
// Dot Engine - Simple Renderer
// =============================================================================
// Minimal 3D renderer that draws primitive shapes.
// =============================================================================

#pragma once

#include "Core/Assets/Asset.h"
#include "Core/Assets/TextureAsset.h"
#include "Core/Material/MaterialTextureUtils.h"
#include "Core/Scene/PointLightShadowUtils.h"

#include "Camera.h"
#include "PrimitiveMeshes.h" // For Submesh struct
#include "SimpleRendererSubsystems.h"
#include "RHI/RHI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
struct ID3D12Device;

namespace Dot
{

class D3D12Device;
enum class PrimitiveType : uint8_t;
struct SSAOGraphPassExecutor;
struct SSAOBlurGraphPassExecutor;
struct HZBGraphPassExecutor;
struct SSAODebugCompositeExecutor;
struct DirectionalShadowGraphPassExecutor;
struct LocalShadowGraphPassExecutor;

// =============================================================================
// Anti-Aliasing Settings (for future API exposure)
// =============================================================================
enum class AntiAliasingMode : uint8_t
{
    None = 0,
    FXAA,   // Fast Approximate Anti-Aliasing (post-process)
    MSAA2x, // 2x Multisample Anti-Aliasing
    MSAA4x  // 4x Multisample Anti-Aliasing
};

struct SSAOSettings
{
    float radius = 0.75f;            // World-space radius for the fixed stable gather
    float intensity = 1.0f;          // Darkening strength applied to the occlusion result
    float thickness = 2.0f;          // Depth window for accepting samples in the gather pass
    int sampleCount = 8;             // Number of taps to use from the fixed stable kernel (clamped to 8)
    bool halfResolution = true;      // Render AO at half resolution to keep the pass cheap
    float blurDepthThreshold = 4.0f; // Depth rejection strength for the small bilateral blur
    bool preferExternalShaders = true; // Use Editor/Shaders files when available; fall back to embedded copies

    // Deprecated compatibility fields retained for config parsing and older saved values.
    float bias = 0.5f;             // Unused compatibility field; not read by the stable AO gather
    float power = 1.0f;            // Unused compatibility field; not read by the stable AO gather
    float maxScreenRadius = 48.0f; // Unused compatibility field; not read by the stable AO gather
    bool enabled = true;
};

struct AntiAliasingSettings
{
    AntiAliasingMode mode = AntiAliasingMode::FXAA; // Re-enabled after fix
    float fxaaSubpixelQuality = 0.75f;              // 0.0 = sharper, 1.0 = softer
    float fxaaEdgeThreshold = 0.166f;               // Edge detection threshold
    float fxaaEdgeThresholdMin = 0.0833f;           // Minimum edge threshold
    bool enabled = true;
};

/// Mesh buffer data for a primitive type
struct PrimitiveMesh
{
    struct RenderSection
    {
        uint32_t indexStart = 0;
        uint32_t indexCount = 0;
    };

    struct RenderBatch
    {
        std::string texturePath;
        AssetHandle<TextureAsset> textureHandle;
        std::vector<RenderSection> sections;
    };

    RHIBufferPtr vertexBuffer;
    RHIBufferPtr indexBuffer;
    uint32_t indexCount = 0;
    static constexpr uint32_t kLodCount = 3;
    std::array<RHIBufferPtr, kLodCount> lodVertexBuffers = {};
    std::array<uint32_t, kLodCount> lodVertexCounts = {0, 0, 0};
    std::array<RHIBufferPtr, kLodCount> lodIndexBuffers = {};
    std::array<uint32_t, kLodCount> lodIndexCounts = {0, 0, 0};
    std::array<std::vector<Submesh>, kLodCount> lodSubmeshes;
    std::array<std::vector<RenderBatch>, kLodCount> lodRenderBatches;
    std::array<bool, kLodCount> lodHasTexturedSubmeshes = {false, false, false};
    std::array<float, kLodCount> lodScreenHeightThresholds = {0.0f, 0.22f, 0.08f};

    // Bounding box (for gizmo sizing)
    float boundsMinX = 0, boundsMinY = 0, boundsMinZ = 0;
    float boundsMaxX = 0, boundsMaxY = 0, boundsMaxZ = 0;

    std::string colormapPath; // Path extracted from file (FBX only)

    // Per-material submeshes (for multi-material FBX models)
    std::vector<Submesh> submeshes;

    // Get the maximum extent of the mesh (for gizmo sizing)
    float GetMaxExtent() const
    {
        float extentX = boundsMaxX - boundsMinX;
        float extentY = boundsMaxY - boundsMinY;
        float extentZ = boundsMaxZ - boundsMinZ;
        return std::max({extentX, extentY, extentZ});
    }
};

/// Scene lighting data passed to renderer via constant buffer
/// Supports: 1 directional + 16 point + 16 spot lights
struct SceneLightData
{
    static constexpr int MAX_POINT_LIGHTS = 16;
    static constexpr int MAX_SPOT_LIGHTS = 16;

    // Primary directional light
    float lightDirX = 0.5f;
    float lightDirY = -0.7f;
    float lightDirZ = 0.5f;
    float lightIntensity = 1.0f;
    float lightColorR = 1.0f;
    float lightColorG = 0.95f;
    float lightColorB = 0.9f;
    float _pad1 = 0.0f;

    // Ambient
    float ambientColorR = 0.3f;
    float ambientColorG = 0.35f;
    float ambientColorB = 0.4f;
    float ambientIntensity = 0.3f;

    // Light counts
    int numPointLights = 0;
    int numSpotLights = 0;

    // Shadow settings
    float shadowDistance = 100.0f;
    float shadowBias = 0.0005f;
    bool shadowEnabled = true;
    float _pad2 = 0.0f;

    // Point lights array (8 floats each = 128 floats for 16)
    struct PointLight
    {
        float posX, posY, posZ;
        float range;
        float colorR, colorG, colorB;
        float intensity;
        float shadowEnabled;
        float shadowBaseSlice;
        float shadowBias;
        float _pad = 0.0f;
    };
    PointLight pointLights[MAX_POINT_LIGHTS] = {};

    // Spot lights array (16 floats each = 256 floats for 16)
    struct SpotLight
    {
        float posX, posY, posZ;
        float range;
        float dirX, dirY, dirZ;
        float innerCos;
        float colorR, colorG, colorB;
        float outerCos;
        float intensity;
        float shadowEnabled;
        float shadowBaseSlice;
        float shadowBias;
    };
    SpotLight spotLights[MAX_SPOT_LIGHTS] = {};
};
// Total size: 8+4+4 + 16*8 + 16*16 = 16 + 128 + 256 = 400 floats = 1600 bytes

/// Material properties for per-object rendering
struct MaterialData
{
    // Base color (used as fallback if no texture)
    float colorR = 0.7f;
    float colorG = 0.7f;
    float colorB = 0.7f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambientOcclusion = 1.0f;
    float emissiveColorR = 0.0f;
    float emissiveColorG = 0.0f;
    float emissiveColorB = 0.0f;
    float emissiveStrength = 0.0f;

    // Texture settings (GPU cbuffer aligned)
    float hasTexture = 0.0f; // 1.0 if texture bound, 0.0 otherwise
    float tilingU = 1.0f;
    float tilingV = 1.0f;
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    int filterMode = 1; // 0=Nearest, 1=Bilinear, 2=Trilinear
    int wrapMode = 0;   // 0=Repeat, 1=Clamp, 2=Mirror

    // Panner animation speed (UV scroll per second)
    float pannerSpeedU = 0.0f;
    float pannerSpeedV = 0.0f;
    int pannerMethod = 0;
    bool pannerLink = false;
    bool lightmapEnabled = false;
    int lightmapTextureSlot = -1;
    float lightmapScaleU = 1.0f;
    float lightmapScaleV = 1.0f;
    float lightmapOffsetU = 0.0f;
    float lightmapOffsetV = 0.0f;
    float lightmapIntensity = 1.0f;
    std::string lightmapTexturePath;

    // Texture paths (CPU only - not uploaded to GPU)
    std::string texturePaths[4];
    int textureSampleTypes[4] = {static_cast<int>(TextureSampleType::Color), static_cast<int>(TextureSampleType::Color),
                                 static_cast<int>(TextureSampleType::Color), static_cast<int>(TextureSampleType::Color)};
    int albedoTextureSlot = -1;
    int normalTextureSlot = -1;
    int ormTextureSlot = -1;

    // Mipmap setting (CPU only - used during texture loading)
    bool useMipmaps = true;

    // Custom material shader ID (0 = standard)
    uint32_t materialShaderId = 0;
};

struct ReflectionProbeDrawData
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
    bool enabled = false;
};

static constexpr uint32_t kMaxReflectionProbeBlendCount = 2;

// Constant buffer data structure matching HLSL register(b0)
// This ensures correct padding and alignment for D3D12
struct ConstantBufferData
{
    float mvpMatrix[16];
    float worldMatrix[16];

    // Directional light
    float dirLightDir[3];
    float dirLightIntensity;
    float dirLightColor[3];
    float _pad1;

    // Ambient
    float ambientColor[3];
    float ambientIntensity;

    // Light counts
    int numPointLights;
    int numSpotLights;
    float _pad2[2];

    // Point lights (aligned to 8 floats each)
    struct PointLight
    {
        float posX, posY, posZ;
        float range;
        float colorR, colorG, colorB;
        float intensity;
        float shadowEnabled;
        float shadowBaseSlice;
        float shadowBias;
        float _pad;
    } pointLights[16];

    // Spot lights (aligned to 16 floats each)
    struct SpotLight
    {
        float posX, posY, posZ;
        float range;
        float dirX, dirY, dirZ;
        float innerCos;
        float colorR, colorG, colorB;
        float outerCos;
        float intensity;
        float shadowEnabled;
        float shadowBaseSlice;
        float shadowBias;
    } spotLights[16];

    // Material row 0
    float materialColor[3];
    float materialMetallic;

    // Material row 1
    float materialRoughness;
    float materialEmissiveColor[3];

    // Material row 2
    float materialEmissiveStrength;
    float lodDebugTint[3];

    // Material row 3
    float hasTextures[4];

    // Material row 4
    float albedoTextureSlot;
    float normalTextureSlot;
    float ormTextureSlot;
    float _padTextureSlots;

    // Material row 5
    float uvTiling[2];
    float uvOffset[2];

    // Material row 6
    float cameraPos[3];
    float time;

    // Material row 7
    float pannerSpeed[2];
    int pannerMethod;
    int pannerLink;

    // Material row 8
    float lightmapUvScale[2];
    float lightmapUvOffset[2];

    // Material row 9
    float lightmapEnabled;
    float lightmapTextureSlot;
    float lightmapIntensity;
    float materialAmbientOcclusion;

    // Reflection probe rows 10-15 (two probes, 3 rows each)
    float reflectionProbePositionRadius[kMaxReflectionProbeBlendCount][4];
    float reflectionProbeTintIntensity[kMaxReflectionProbeBlendCount][4];
    float reflectionProbeParams[kMaxReflectionProbeBlendCount][4];
    float reflectionProbeBoxExtents[kMaxReflectionProbeBlendCount][4];

    // Shadow parameters
    float shadowMatrix[16];
    float shadowBias;
    float shadowEnabled;
    float viewportWidth;
    float viewportHeight;
    float fPlusEnabled;
    uint32_t tileCountX;
    uint32_t tileCountY;
    uint32_t debugVisMode; // DebugVisMode enum value (0=Lit, 1=Unlit, etc.)
    float ssaoEnabled;
    float _pad3[3];
};

static_assert((offsetof(ConstantBufferData, shadowMatrix) % 16) == 0,
              "ConstantBufferData::shadowMatrix must start on a 16-byte register boundary");
static_assert(offsetof(ConstantBufferData, shadowMatrix) == 2272,
              "ConstantBufferData::shadowMatrix offset must match the HLSL layout");
static_assert(sizeof(ConstantBufferData) == 2384,
              "ConstantBufferData size must stay in sync with the HLSL layout");
static_assert((sizeof(ConstantBufferData) % 16) == 0,
              "ConstantBufferData size must remain 16-byte aligned");

/// Simple 3D renderer for the viewport
class SimpleRenderer
{
public:
    SimpleRenderer();
    ~SimpleRenderer();

    /// Initialize rendering resources
    bool Initialize(RHIDevice* device);
    void SetShaderRootPath(const std::string& path, bool allowEditorFallback = true)
    {
        m_ShaderRootPathOverride = path;
        m_AllowEditorShaderFallback = allowEditorFallback;
    }

    /// Set scene lighting data (call before Render calls each frame)
    void SetLightData(const SceneLightData& lightData)
    {
        m_LightData = lightData;
        m_ShadowDistance = lightData.shadowDistance;
        m_ShadowBias = lightData.shadowBias;
        m_ShadowEnabled = lightData.shadowEnabled;
    }

    /// Set material data for next render call (defaults to gray material)
    void SetMaterialData(const MaterialData& material) { m_MaterialData = material; }

    /// Set local reflection probe data for the next render call.
    void SetReflectionProbeData(uint32_t slot, const std::string& cubemapPath, const Vec3& position, float radius,
                                const Vec3& boxExtents, float intensity, float falloff, const Vec3& tint,
                                float rotationDegrees, float blendWeight, bool enabled);
    void ClearReflectionProbeData()
    {
        for (uint32_t i = 0; i < kMaxReflectionProbeBlendCount; ++i)
            m_ReflectionProbeData[i] = {};
    }
    bool BeginExternalRenderTarget(RHITexturePtr color, RHITexturePtr depth = nullptr, float clearR = 0.0f,
                                   float clearG = 0.0f, float clearB = 0.0f, float clearA = 1.0f);
    void EndExternalRenderTarget();
    void SetSceneCaptureMode(bool enabled)
    {
        if (m_SceneCaptureMode != enabled)
        {
            m_SceneCaptureMode = enabled;
            m_SharedDescriptorFrame = UINT64_MAX;
        }
    }
    bool IsSceneCaptureMode() const { return m_SceneCaptureMode; }

    /// Prepare renderer for a new frame
    void BeginFrame();
    RHITexturePtr GetDirectionalShadowGraphTexture() const { return m_DirectionalShadowGraphTexture; }
    RHITexturePtr GetLocalShadowGraphTexture() const { return m_LocalShadowGraphTexture; }
    RHITexturePtr GetSSAOOcclusionGraphTexture() const { return m_SSAOOcclusionGraphTexture; }
    RHITexturePtr GetSSAOBlurredGraphTexture() const { return m_SSAOBlurredGraphTexture; }
    uint32_t GetSSAOBufferWidth() const { return m_SSAOBufferWidth; }
    uint32_t GetSSAOBufferHeight() const { return m_SSAOBufferHeight; }
    RHITexturePtr GetHZBGraphTexture() const { return m_HZBGraphTexture; }

    /// Render the scene with cube at specified transform (deprecated - use Mat4 version)
    void Render(const Camera& camera, RHISwapChain* swapChain, float posX = 0, float posY = 0, float posZ = 0,
                float rotX = 0, float rotY = 0, float rotZ = 0, float scaleX = 1, float scaleY = 1, float scaleZ = 1);

    /// Render with world matrix (preferred for hierarchy support)
    void Render(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix);

    /// Render a specific primitive type with world matrix
    void Render(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix, PrimitiveType primitiveType);

    /// Render an arbitrary mesh with world matrix
    void RenderMesh(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix, const PrimitiveMesh& mesh);

    /// Render with material data override
    void RenderMesh(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix, const PrimitiveMesh& mesh,
                    const MaterialData& material);

    /// Render multiple instances of the same mesh using hardware instancing.
    /// Returns number of GPU draw calls submitted.
    uint32_t RenderMeshInstanced(const Camera& camera, RHISwapChain* swapChain, const PrimitiveMesh& mesh,
                                 const std::vector<const float*>& worldMatrices);

    /// Load a mesh from file (OBJ/FBX) - caches loaded meshes
    /// Returns vector of pointers to cached mesh parts (one per submesh/material)
    std::vector<PrimitiveMesh*> LoadMesh(const std::string& filepath);

    /// Create a transient GPU mesh from raw mesh data.
    std::unique_ptr<PrimitiveMesh> CreateRuntimeMesh(const MeshData& meshData);

    /// Get primitive mesh by type (for shadow caster collection)
    PrimitiveMesh* GetPrimitiveMesh(PrimitiveType type)
    {
        size_t index = static_cast<size_t>(type);
        if (index >= kPrimitiveCount)
            return nullptr;
        return &m_Meshes[index];
    }

    /// Get D3D12 device (used for UI descriptor calculations)
    ID3D12Device* GetD3D12Device();

    /// Render a cubemap skybox (renders behind all objects)
    /// cubemapPath: single cross-layout cubemap image
    /// Falls back to gradient if path is empty or loading fails
    /// topColor/bottomColor are RGB (0-1) for gradient fallback
    /// wrapMode: 0=Clamp, 1=Repeat, 2=Mirror
    /// rotation: horizontal rotation in degrees (0-360)
    /// showMarkers: if true, display face labels (R/L/U/D/F/B) for debugging
    void RenderSkybox(const Camera& camera, RHISwapChain* swapChain, const std::string& cubemapPath, int wrapMode,
                      float rotation, bool showMarkers, float topR, float topG, float topB, float bottomR,
                      float bottomG, float bottomB);

    /// Register a custom material shader from HLSL injection
    /// Returns a hash ID that can be used in MaterialData
    uint32_t RegisterMaterialShader(const std::string& surfaceHLSL);

    /// Compute shadow frustum for culling (call before collecting shadow casters)
    /// Returns a frustum that can be used with Camera::Frustum::TestAABB
    Camera::Frustum ComputeShadowFrustum(const Camera& camera);

    /// Render shadow depth map from light's perspective
    /// Call before main scene rendering each frame
    void RenderShadowMap(const Camera& camera, RHISwapChain* swapChain,
                         const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters);

    /// Render point and spot local-light shadows into the shared shadow texture array.
    void RenderLocalLightShadowMaps(RHISwapChain* swapChain,
                                    const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters);

    /// Get shadow map SRV for ImGui debug display
    /// Returns GPU descriptor handle suitable for ImGui::Image
    void* GetShadowMapImGuiTexture() const { return m_ShadowSRVHeap; }

    /// Anti-aliasing settings (for future API exposure)
    const SSAOSettings& GetSSAOSettings() const { return m_SSAOSettings; }
    void SetSSAOSettings(const SSAOSettings& settings) { m_SSAOSettings = settings; }
    void SetSSAOEnabled(bool enabled) { m_SSAOSettings.enabled = enabled; }
    const AntiAliasingSettings& GetAntiAliasingSettings() const { return m_AntiAliasingSettings; }
    void SetAntiAliasingSettings(const AntiAliasingSettings& settings) { m_AntiAliasingSettings = settings; }
    void SetAntiAliasingMode(AntiAliasingMode mode) { m_AntiAliasingSettings.mode = mode; }
    void SetAntiAliasingEnabled(bool enabled) { m_AntiAliasingSettings.enabled = enabled; }

    /// FXAA render pass management
    /// Call BeginFXAAPass before scene rendering to redirect to intermediate RT
    /// Call EndFXAAPass after scene rendering to apply FXAA and blit to swap chain
    bool BeginFXAAPass(RHISwapChain* swapChain);
    void EndFXAAPass(RHISwapChain* swapChain);

    /// Cleanup
    void Shutdown();

    // ---- SSAO & Depth Pre-pass ----
    bool CreateSSAOResources(RHISwapChain* swapChain, uint32_t width, uint32_t height);
    void ApplySSAO(RHISwapChain* swapChain, const Camera& camera);
    void ApplySSAOBlur(RHISwapChain* swapChain);
    void DebugDrawSSAO(RHISwapChain* swapChain);

    void BeginDepthPrepass(RHISwapChain* swapChain);
    void EndDepthPrepass(RHISwapChain* swapChain);

    // Transition depth buffer back to DSV for main pass
    void BeginForwardPass(RHISwapChain* swapChain);
    void ClearSceneDepth(RHISwapChain* swapChain);

    // ---- HZB Occlusion Culling ----
    bool CreateHZBResources(uint32_t width, uint32_t height);
    void GenerateHZB(RHISwapChain* swapChain);
    bool TestHZBOcclusion(const Camera& camera, float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    void SetHZBEnabled(bool enabled) { m_Occlusion.enabled = enabled; }
    bool IsHZBEnabled() const { return m_Occlusion.enabled; }

    // ---- Forward+ Tiled Lighting ----
    bool CreateForwardPlusResources(uint32_t screenWidth, uint32_t screenHeight);
    void CullLights(const Camera& camera, RHISwapChain* swapChain);
    void SetForwardPlusEnabled(bool enabled) { m_ForwardPlusEnabled = enabled; }
    bool IsForwardPlusEnabled() const { return m_ForwardPlusEnabled; }

private:
    friend struct SSAOGraphPassExecutor;
    friend struct SSAOBlurGraphPassExecutor;
    friend struct HZBGraphPassExecutor;
friend struct SSAODebugCompositeExecutor;
friend struct DirectionalShadowGraphPassExecutor;
friend struct LocalShadowGraphPassExecutor;
friend struct MainSceneGraphPassExecutor;
friend struct ViewmodelGraphPassExecutor;

    bool CreateShaders();
    bool CreatePipelineState();
    bool CreateAllMeshes();
    bool CreateConstantBuffer();
    bool CreateShadowResources();
    bool CreateMeshBuffers(const struct MeshData& meshData, PrimitiveMesh& outMesh);

    // Support for custom procedural materials
    struct CustomMaterialPSO
    {
        void* pso = nullptr; // ID3D12PipelineState*
        std::string hlsl;
    };
    std::unordered_map<uint32_t, CustomMaterialPSO> m_CustomPSOs;
    void* CreateCustomPSO(const std::string& surfaceHLSL);

    RHIDevice* m_Device = nullptr;
    D3D12Device* m_D3D12Device = nullptr;

    // Pipeline
    void* m_RootSignature = nullptr;
    void* m_PipelineState = nullptr;
    void* m_InstancedPipelineState = nullptr; // Solid lit pass with instanced VS
    void* m_WireframePSO = nullptr; // Wireframe render mode
    void* m_DepthPSO = nullptr;     // Depth visualization mode
    void* m_OverdrawPSO = nullptr;  // Additive overdraw visualization mode

    // Skybox PSOs for each wrap mode (0=Clamp, 1=Wrap, 2=Mirror)
    static constexpr size_t kSkyboxWrapModeCount = 3;
    void* m_SkyboxPSO[kSkyboxWrapModeCount] = {nullptr, nullptr, nullptr};

    // Meshes for each primitive type (indexed by PrimitiveType enum)
    static constexpr size_t kPrimitiveCount = 6;
    std::array<PrimitiveMesh, kPrimitiveCount> m_Meshes;

    // Skybox mesh (inverted cube)
    PrimitiveMesh m_SkyboxMesh;

    // Constant buffer for light data (CBV)
    RHIBufferPtr m_LightConstantBuffer;
    void* m_CBVHeap = nullptr;           // ID3D12DescriptorHeap*
    void* m_CBResource = nullptr;        // ID3D12Resource*
    void* m_MappedLightBuffer = nullptr; // Persistent mapped pointer
    void* m_InstanceCBResource = nullptr; // ID3D12Resource* (b1)
    void* m_MappedInstanceBuffer = nullptr;

    static constexpr uint32_t kMaxInstanceBatch = 64;

    // Scene lighting (set per frame before rendering)
    SceneLightData m_LightData;

    // Per-object material data
    MaterialData m_MaterialData;

    // Constant buffer management
    uint32_t m_CurrentCBOffset = 0;
    static const uint32_t kMaxCBSize = 1024 * 1024 * 4; // 4MB buffer for dynamic upload

    // Shader bytecode
    std::vector<uint8_t> m_VSBytecode;
    std::vector<uint8_t> m_InstancedVSBytecode;
    std::vector<uint8_t> m_PSBytecode;
    std::vector<uint8_t> m_DepthPSBytecode; // Depth visualization shader

    bool m_Initialized = false;
    float m_ElapsedTime = 0.0f; // Animation time for panner effects
    std::string m_ShaderRootPathOverride;
    bool m_AllowEditorShaderFallback = true;
    RHITexturePtr m_ExternalColorTarget;
    RHITexturePtr m_ExternalDepthTarget;
    bool m_UsingExternalRenderTarget = false;
    bool m_SceneCaptureMode = false;

    // Loaded mesh cache (keyed by file path)
    // Stores a vector of unique_ptr mainly because multiple entities might refer to parts of same file
    using MeshGroup = std::vector<std::unique_ptr<PrimitiveMesh>>;
    std::unordered_map<std::string, MeshGroup> m_MeshCache;

    // Skybox rendering
    std::vector<uint8_t> m_SkyboxVSBytecode;
    std::vector<uint8_t> m_SkyboxPSBytecode;
    // Root signatures for each wrap mode (0=Clamp, 1=Wrap, 2=Mirror)
    void* m_SkyboxRootSignature[kSkyboxWrapModeCount] = {nullptr, nullptr, nullptr};
    void* m_SkyboxSRVHeap = nullptr;  // SRV descriptor heap for cubemap
    RHITexturePtr m_SkyboxCubemap;    // Loaded cubemap texture
    std::string m_LoadedCubemapPath;  // Path of currently loaded cubemap
    int m_CurrentSkyboxWrapMode = -1; // Cached wrap mode (-1 = not set, 0=Clamp, 1=Repeat, 2=Mirror)

    // Material texture rendering
    void* m_SamplerHeap = nullptr;                       // 9 samplers (3 filter x 3 wrap modes)
    void* m_MaterialSRVHeap = nullptr;                   // SRV heap for material textures
    static constexpr uint32_t kMaxMaterialTextures = 64; // Max concurrent textures

    // Texture cache for materials
    struct TextureCacheEntry
    {
        AssetHandle<TextureAsset> handle;
        void* resource = nullptr; // ID3D12Resource*
        void* srvHeap = nullptr;  // ID3D12DescriptorHeap*
        uint32_t width = 0, height = 0;
        uint32_t mipLevels = 1; // Number of mip levels
        std::string descriptorSignature;
        uint64_t descriptorUpdateFrame = 0;
    };
    TextureCacheEntry* LoadMaterialTexture(const std::string& path,
                                          TextureSemantic semantic = TextureSemantic::Color);
    std::unordered_map<std::string, TextureCacheEntry> m_TextureCache;

    struct CubemapCacheEntry
    {
        RHITexturePtr texture;
        uint32_t mipLevels = 1;
        uint64_t sourceStamp = 0;
    };
    CubemapCacheEntry* LoadCubemapTexture(const std::string& path);
    void WriteReflectionProbeDescriptors(void* descriptorHeap, uint32_t descriptorIndexBase);
    std::unordered_map<std::string, CubemapCacheEntry> m_CubemapCache;
    std::unordered_map<void*, std::string> m_ReflectionProbeDescriptorSignatures;

    // Current bound texture for rendering
    std::string m_CurrentTexturePath;
    ReflectionProbeDrawData m_ReflectionProbeData[kMaxReflectionProbeBlendCount];

    // ---- Shadow Mapping ----
    static constexpr uint32_t kShadowMapResolution = 2048;
    void* m_ShadowDepthBuffer = nullptr;   // ID3D12Resource* - depth texture
    void* m_ShadowDSVHeap = nullptr;       // DSV for shadow pass
    void* m_ShadowSRVHeap = nullptr;       // SRV for sampling in main pass
    void* m_ShadowPSO = nullptr;           // Depth-only PSO
    void* m_ShadowRootSignature = nullptr; // Root signature for shadow pass
    float m_ShadowLightMatrix[16] = {};    // Light's view-projection matrix
    float m_ShadowDistance = 100.0f;       // Shadow frustum distance
    bool m_ShadowEnabled = true;           // Global shadow enable
    float m_ShadowBias = 0.0005f;          // Depth bias to reduce acne
    void* m_LocalShadowDepthBuffer = nullptr; // ID3D12Resource* - depth texture array
    void* m_LocalShadowDSVHeap = nullptr;     // DSV heap with one slice per local-light shadow projection
    RHITexturePtr m_DirectionalShadowGraphTexture;
    RHITexturePtr m_LocalShadowGraphTexture;

    // ---- Anti-Aliasing ----
    AntiAliasingSettings m_AntiAliasingSettings;

    // FXAA post-process resources
    void* m_FXAAIntermediateRT = nullptr;       // ID3D12Resource* - scene render target
    void* m_FXAARTVHeap = nullptr;              // RTV for intermediate target
    void* m_FXAASRVHeap = nullptr;              // SRV for sampling in FXAA pass
    void* m_FXAAPSO = nullptr;                  // FXAA pixel shader PSO
    void* m_FXAARootSignature = nullptr;        // Root signature for FXAA
    std::vector<uint8_t> m_FXAAVSBytecode;      // Full-screen triangle VS
    std::vector<uint8_t> m_FXAAPSBytecode;      // FXAA pixel shader
    uint32_t m_FXAAWidth = 0, m_FXAAHeight = 0; // Cached RT dimensions
    bool m_IsInFXAAPass = false;                // Active flag for Begin/End
    bool CreateFXAAResources(uint32_t width, uint32_t height);
    void ApplyFXAA(RHISwapChain* swapChain);

    // ---- SSAO ----
    SSAOSettings m_SSAOSettings;
    void* m_SSAOOcclusionRT = nullptr;         // ID3D12Resource*
    void* m_SSAOBlurredRT = nullptr;           // ID3D12Resource*
    void* m_SSAONoiseTexture = nullptr;        // ID3D12Resource*
    void* m_SSAOKernelBuffer = nullptr;        // StructuredBuffer for samples
    void* m_SSAORTVHeap = nullptr;             // RTVs for occlusion buffers
    void* m_SSAOSRVHeap = nullptr;             // SRVs for sampling
    void* m_SSAORootSignature = nullptr;       // Root signature for SSAO
    void* m_SSAOPSO = nullptr;                 // SSAO calculation PSO
    void* m_SSAOBlurPSO = nullptr;             // Bilateral blur PSO
    void* m_SSAODebugPSO = nullptr;            // Debug PSO (RGBA output)
    void* m_SSAOCompositePSO = nullptr;        // Composite PSO (multiply blend)
    std::vector<uint8_t> m_SSAOVSBytecode;     // Full-screen triangle VS
    std::vector<uint8_t> m_SSAOPSBytecode;     // SSAO calculation PS
    std::vector<uint8_t> m_SSAOBlurPSBytecode; // Bilateral blur PS

    std::vector<float> m_SSAOKernel; // 16 stable fixed-pattern samples
    float m_SSAONearZ = 0.1f;
    float m_SSAOFarZ = 1000.0f;
    uint32_t m_SSAOWidth = 0, m_SSAOHeight = 0;
    uint32_t m_SSAOBufferWidth = 0, m_SSAOBufferHeight = 0;
    bool m_SSAOUsingExternalShaders = true;
    bool m_IsDepthPrepass = false; // Active flag for depth-only rendering
    RHITexturePtr m_SSAOOcclusionGraphTexture;
    RHITexturePtr m_SSAOBlurredGraphTexture;

    std::vector<uint8_t> m_ShadowVSBytecode; // Shadow depth vertex shader
    TextureCacheEntry* m_CurrentTexture = nullptr;

    // ---- HZB Occlusion Culling ----
    OcclusionState m_Occlusion;
    RHITexturePtr m_HZBGraphTexture;
    uint64_t m_FrameCounter = 0;
    uint64_t m_SharedDescriptorFrame = 0;

    // ---- Forward+ Tiled Lighting ----
    static constexpr uint32_t TILE_SIZE = 16; // 16x16 pixel tiles
    static constexpr uint32_t MAX_LIGHTS_PER_TILE = 256;
    void* m_LightBuffer = nullptr;        // StructuredBuffer with all lights
    void* m_LightIndexBuffer = nullptr;   // Per-tile light indices (RWBuffer)
    void* m_LightGridBuffer = nullptr;    // Per-tile offset/count
    void* m_LightCullPSO = nullptr;       // Compute PSO for light culling
    void* m_LightCullRootSig = nullptr;   // Root signature for light culling
    void* m_ForwardPlusSRVHeap = nullptr; // SRV heap for Forward+ resources
    uint32_t m_TileCountX = 0, m_TileCountY = 0;
    bool m_ForwardPlusEnabled = true;

    void RefreshGraphInteropTextures();
};

} // namespace Dot
