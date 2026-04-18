// =============================================================================
// Dot Engine - Material Preview Renderer Implementation
// =============================================================================

#include "MaterialPreviewRenderer.h"

#include "Core/Assets/AssetManager.h"
#include "RHI/RHIGUI.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Helper functions defined in SimpleRenderer.cpp
extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);
extern D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer);

namespace Dot
{

namespace
{

DXGI_FORMAT GetPreviewDxgiFormat(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R8G8B8A8_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

} // namespace

// Vertex format for preview sphere with UVs
struct PreviewVertex
{
    float x, y, z;    // Position
    float nx, ny, nz; // Normal
    float u, v;       // UV coordinates
    float tx, ty, tz, tw; // Tangent.xyz + handedness sign
};

// Preview shader HLSL - PBR-ish lighting
static const char* s_PreviewVS = R"(
cbuffer Constants : register(b0)
{
    float4x4 MVP;
    float4x4 World;
    float3 LightDir;
    float HasTexture;  // 1.0 if texture bound, 0.0 otherwise
    float3 CameraPos;
    float _Time;       // Elapsed time for animation
    float3 Albedo;
    float Metallic;
    float Roughness;
    float _pad2;
    float2 UVTiling;   // UV scale
    float2 UVOffset;   // UV offset
    float2 PannerSpeed; // UV animation speed
    int Channel;
    float3 _pad3;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float4 worldTangent : TANGENT;
    float3 worldPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0), MVP);
    output.worldNormal = mul(input.normal, (float3x3)World);
    float3 worldTangent = mul(input.tangent.xyz, (float3x3)World);
    worldTangent = worldTangent - output.worldNormal * dot(output.worldNormal, worldTangent);
    float tangentLenSq = dot(worldTangent, worldTangent);
    if (tangentLenSq > 1e-8)
        worldTangent *= rsqrt(tangentLenSq);
    output.worldTangent = float4(worldTangent, input.tangent.w);
    output.worldPos = mul(float4(input.position, 1.0), World).xyz;
    output.uv = input.uv;
    return output;
}
)";

static const char* s_PreviewPS = R"(
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
SamplerState sampler0 : register(s0);
SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);
SamplerState sampler3 : register(s3);

cbuffer Constants : register(b0)
{
    float4x4 MVP;
    float4x4 World;
    float3 LightDir;
    float HasTexture;
    float3 CameraPos;
    float _Time;
    float3 Albedo;
    float Metallic;
    float Roughness;
    float _pad2;
    float2 UVTiling;
    float2 UVOffset;
    float2 PannerSpeed;
    int Channel; // -1=RGB, 0=R, 1=G, 2=B
    float3 _pad3;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float4 worldTangent : TANGENT;
    float3 worldPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float3 DotBuildFallbackTangent(float3 N)
{
    float3 referenceAxis = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return normalize(cross(referenceAxis, N));
}

float3 DotDecodeNormalSample(float3 encodedNormal, float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent)
{
    float3 N = normalize(worldNormal);
    float3 T = float3(0.0, 0.0, 0.0);
    float3 B = float3(0.0, 0.0, 0.0);

    float tangentLenSq = dot(worldTangent.xyz, worldTangent.xyz);
    if (tangentLenSq > 1e-6)
    {
        T = worldTangent.xyz - N * dot(N, worldTangent.xyz);
        float orthoLenSq = dot(T, T);
        if (orthoLenSq > 1e-8)
        {
            T *= rsqrt(orthoLenSq);
            float tangentSign = worldTangent.w < 0.0 ? -1.0 : 1.0;
            B = normalize(cross(N, T)) * tangentSign;
        }
    }

    if (dot(T, T) < 1e-6 || dot(B, B) < 1e-6)
    {
        float3 dpdx = ddx_fine(worldPos);
        float3 dpdy = ddy_fine(worldPos);
        float2 duvdx = ddx_fine(uv);
        float2 duvdy = ddy_fine(uv);
        float3 dp2perp = cross(dpdy, N);
        float3 dp1perp = cross(N, dpdx);
        T = dp2perp * duvdx.x + dp1perp * duvdy.x;
        B = dp2perp * duvdx.y + dp1perp * duvdy.y;
        float tLenSq = dot(T, T);
        float bLenSq = dot(B, B);
        float maxLenSq = max(tLenSq, bLenSq);
        if (maxLenSq > 1e-10)
        {
            float invMaxLen = rsqrt(maxLenSq);
            T *= invMaxLen;
            B *= invMaxLen;
            T = normalize(T - N * dot(N, T));
            B = normalize(B - N * dot(N, B));
            if (dot(cross(T, B), N) < 0.0)
                B = -B;
        }
        else
        {
            T = DotBuildFallbackTangent(N);
            B = normalize(cross(N, T));
        }
    }
    float2 xy = encodedNormal.xy * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(xy, xy)));
    float3 tangentNormal = normalize(float3(xy, z));
    return normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
}

float3 DotDecodeNormalSample(float3 encodedNormal, float2 uv, float3 worldPos, float3 worldNormal)
{
    return DotDecodeNormalSample(encodedNormal, uv, worldPos, worldNormal, float4(0.0, 0.0, 0.0, 1.0));
}

// --- Injection Point ---
// When MATERIAL_SURFACE_CUSTOM is defined, the GetMaterialSurface function is provided
// by the injected code. The injected code is placed AFTER this template header.
#ifdef MATERIAL_SURFACE_CUSTOM
    // Forward declare GetMaterialSurface - actual implementation injected after cbuffer
    void GetMaterialSurface(float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent, inout float3 albedo, inout float metallic, inout float roughness, inout float ao, inout float3 normal);
#else
    void GetMaterialSurface(float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent, inout float3 albedo, inout float metallic, inout float roughness, inout float ao, inout float3 normal)
    {
        if (HasTexture > 0.5)
        {
            // Apply UV transform with panner animation
            float2 texUV = uv * UVTiling + UVOffset + _Time * PannerSpeed;
            float4 sampled = tex0.Sample(sampler0, texUV);
            if (Channel == 0) albedo = sampled.rrr;
            else if (Channel == 1) albedo = sampled.ggg;
            else if (Channel == 2) albedo = sampled.bbb;
            else albedo = sampled.rgb;
        }
    }
#endif

float DotPow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 DotFresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * DotPow5(1.0 - saturate(cosTheta));
}

float3 DotFresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float oneMinusRoughness = 1.0 - roughness;
    float3 grazing = max(float3(oneMinusRoughness, oneMinusRoughness, oneMinusRoughness), F0);
    return F0 + (grazing - F0) * DotPow5(1.0 - saturate(cosTheta));
}

float DotDistributionGGX(float NdotH, float roughness)
{
    float a = max(roughness * roughness, 0.045);
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 1e-4);
}

float DotGeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-4);
}

float DotGeometrySmith(float NdotV, float NdotL, float roughness)
{
    return DotGeometrySchlickGGX(saturate(NdotV), roughness) *
           DotGeometrySchlickGGX(saturate(NdotL), roughness);
}

float3 DotEvaluateSpecularBRDF(float3 F0, float roughness, float NdotV, float NdotL, float NdotH, float VdotH)
{
    float3 F = DotFresnelSchlick(VdotH, F0);
    float D = DotDistributionGGX(NdotH, roughness);
    float G = DotGeometrySmith(NdotV, NdotL, roughness);
    return (D * G * F) / max(4.0 * max(NdotV, 1e-4) * max(NdotL, 1e-4), 1e-4);
}

float3 DotPreviewSkylightDiffuse(float3 diffuseAlbedo, float3 normalWS, float ambientOcclusion)
{
    const float3 ambientColor = float3(0.26, 0.30, 0.36);
    const float ambientIntensity = 0.45;
    const float3 lightColor = float3(1.0, 0.97, 0.92);
    float hemi = saturate(normalWS.y * 0.5 + 0.5);
    float horizon = 1.0 - abs(normalWS.y);
    float3 baseAmbient = ambientColor * ambientIntensity;
    float3 skyColor = baseAmbient * 1.35 + lightColor * 0.04;
    float3 groundColor = baseAmbient * 0.30;
    float3 horizonColor = baseAmbient * 0.70;
    float3 irradiance = lerp(groundColor, skyColor, hemi);
    irradiance = lerp(irradiance, horizonColor, horizon * 0.35);
    return diffuseAlbedo * irradiance * ambientOcclusion;
}

float3 DotPreviewSkylightSpecular(float3 F0, float roughness, float3 normalWS, float3 viewDir, float ambientOcclusion)
{
    const float3 ambientColor = float3(0.26, 0.30, 0.36);
    const float ambientIntensity = 0.45;
    const float3 lightColor = float3(1.0, 0.97, 0.92);
    float3 reflected = reflect(-viewDir, normalWS);
    float hemi = saturate(reflected.y * 0.5 + 0.5);
    float horizon = 1.0 - abs(reflected.y);
    float3 baseAmbient = ambientColor * ambientIntensity;
    float3 skyColor = baseAmbient * 1.35 + lightColor * 0.04;
    float3 groundColor = baseAmbient * 0.25;
    float3 horizonColor = baseAmbient * 0.60;
    float3 envColor = lerp(groundColor, skyColor, hemi);
    envColor = lerp(envColor, horizonColor, horizon * 0.40);
    float NdotV = saturate(dot(normalWS, viewDir));
    float3 envF = DotFresnelSchlickRoughness(NdotV, F0, roughness);
    float glossy = saturate(1.0 - roughness);
    float specStrength = lerp(0.04, 0.55, glossy * glossy);
    float specOcclusion = lerp(ambientOcclusion * 0.55, ambientOcclusion, glossy);
    return envColor * envF * specStrength * specOcclusion;
}

float4 main(PSInput input) : SV_TARGET
{
    float3 albedo = Albedo;
    float metallic = Metallic;
    float roughness = Roughness;
    float ao = 1.0;
    float3 normal = normalize(input.worldNormal);

    GetMaterialSurface(input.uv, input.worldPos, input.worldNormal, input.worldTangent, albedo, metallic, roughness, ao, normal);

    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.045, 1.0);
    albedo = saturate(albedo);

    float3 macroN = normalize(input.worldNormal);
    float3 N = normalize(normal);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.worldPos);
    float rawMacroNdotL = dot(macroN, L);
    float macroLightGate = saturate((rawMacroNdotL + 0.12) / 0.36);
    float NdotL = max(dot(N, L), 0.0) * macroLightGate;
    float NdotV = max(dot(N, V), 0.0);

    float ambientOcclusion = lerp(1.0, saturate(ao), 0.85);
    float3 diffuseAlbedo = albedo * (1.0 - metallic);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 totalDiffuse = DotPreviewSkylightDiffuse(diffuseAlbedo, N, ambientOcclusion);
    float3 totalSpecular = DotPreviewSkylightSpecular(F0, roughness, N, V, ambientOcclusion);

    if (NdotL > 0.0)
    {
        const float3 lightColor = float3(1.0, 0.97, 0.92);
        float3 radiance = lightColor;
        float3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);
        totalDiffuse += diffuseAlbedo * radiance * NdotL;
        totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) * radiance * NdotL;
    }

    float3 finalColor = totalDiffuse + totalSpecular;
    
    return float4(finalColor, 1.0);
}
)";

MaterialPreviewRenderer::MaterialPreviewRenderer() = default;

MaterialPreviewRenderer::~MaterialPreviewRenderer()
{
    Shutdown();
}

bool MaterialPreviewRenderer::Initialize(RHIDevice* device, RHIGUI* gui, uint32_t size)
{
    if (m_Initialized)
        return true;

    m_Device = device;
    m_GUI = gui;
    m_Size = size;

    if (!CreateRenderTarget())
        return false;
    if (!CreateShaders())
        return false;
    if (!CreatePipelineState())
        return false;
    if (!CreateSphereMesh())
        return false;
    if (!CreateConstantBuffer())
        return false;

    m_Initialized = true;
    return true;
}

void MaterialPreviewRenderer::Shutdown()
{
    if (m_ImGuiTexId && m_GUI)
    {
        m_GUI->UnregisterTexture(m_ImGuiTexId);
        m_ImGuiTexId = nullptr;
    }

    // Release D3D12 resources
    if (m_RTVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_RTVHeap)->Release();
        m_RTVHeap = nullptr;
    }
    if (m_DSVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_DSVHeap)->Release();
        m_DSVHeap = nullptr;
    }
    if (m_DepthBuffer)
    {
        reinterpret_cast<ID3D12Resource*>(m_DepthBuffer)->Release();
        m_DepthBuffer = nullptr;
    }
    if (m_RootSignature)
    {
        reinterpret_cast<ID3D12RootSignature*>(m_RootSignature)->Release();
        m_RootSignature = nullptr;
    }
    if (m_PipelineState)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_PipelineState)->Release();
        m_PipelineState = nullptr;
    }
    if (m_SRVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_SRVHeap)->Release();
        m_SRVHeap = nullptr;
    }
    if (m_SamplerHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_SamplerHeap)->Release();
        m_SamplerHeap = nullptr;
    }
    if (m_CBVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_CBVHeap)->Release();
        m_CBVHeap = nullptr;
    }
    if (m_ConstantBuffer)
    {
        reinterpret_cast<ID3D12Resource*>(m_ConstantBuffer)->Release();
        m_ConstantBuffer = nullptr;
    }

    m_RenderTarget.reset();
    m_VertexBuffer.reset();
    m_IndexBuffer.reset();

    ClearTextureSlots();

    m_Initialized = false;
}

void MaterialPreviewRenderer::SetAlbedoTexture(const std::string& path, float tilingU, float tilingV, float offsetU,
                                               float offsetV, int filterMode, int wrapMode, float pannerSpeedU,
                                               float pannerSpeedV, int channel)
{
    // Store UV transform and sampling options
    m_TilingU = tilingU;
    m_TilingV = tilingV;
    m_OffsetU = offsetU;
    m_OffsetV = offsetV;
    m_FilterMode = filterMode;
    m_WrapMode = wrapMode;
    m_PannerSpeedU = pannerSpeedU;
    m_PannerSpeedV = pannerSpeedV;
    m_Channel = channel;

    SetTextureSlot(0, path, filterMode, wrapMode);
}

void MaterialPreviewRenderer::SetTextureSlot(int slot, const std::string& path, int filterMode, int wrapMode, int sampleType)
{
    if (slot < 0 || slot >= static_cast<int>(m_TextureSlots.size()))
        return;

    PreviewTextureSlot& textureSlot = m_TextureSlots[slot];
    filterMode = std::clamp(filterMode, 0, 2);
    wrapMode = std::clamp(wrapMode, 0, 2);
    sampleType = std::clamp(sampleType, static_cast<int>(TextureSampleType::Color), static_cast<int>(TextureSampleType::Mask));

    if (textureSlot.path == path && textureSlot.filterMode == filterMode && textureSlot.wrapMode == wrapMode &&
        textureSlot.sampleType == sampleType)
        return;

    textureSlot.filterMode = filterMode;
    textureSlot.wrapMode = wrapMode;
    textureSlot.sampleType = sampleType;
    RefreshSamplerDescriptors();

    if (path.empty())
    {
        ReleaseTextureSlot(slot);
        return;
    }

    if (LoadTextureSlot(slot, path, TextureSemanticFromSampleType(static_cast<TextureSampleType>(sampleType))))
    {
        textureSlot.path = path;
    }
    else
    {
        ReleaseTextureSlot(slot);
    }
}

void MaterialPreviewRenderer::ClearTextureSlots()
{
    for (int slot = 0; slot < static_cast<int>(m_TextureSlots.size()); ++slot)
        ReleaseTextureSlot(slot);
    RefreshSamplerDescriptors();
}

bool MaterialPreviewRenderer::LoadTextureSlot(int slot, const std::string& path, TextureSemantic semantic)
{
    if (slot < 0 || slot >= static_cast<int>(m_TextureSlots.size()))
        return false;

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice || !m_SRVHeap)
        return false;

    ReleaseTextureSlot(slot);
    AssetHandle<TextureAsset> handle = AssetManager::Get().LoadTexture(path, semantic);
    if (!handle.IsValid())
        return false;
    AssetManager::Get().Wait(handle.GetInternal());
    if (!handle.IsReady() || !handle->GetTexture())
        return false;

    ID3D12Resource* texture =
        static_cast<ID3D12Resource*>(m_Device->GetNativeTextureResource(handle->GetTexture().get()));
    if (!texture)
        return false;

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = GetPreviewDxgiFormat(handle->GetFormat());
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = handle->GetTexture()->GetMipLevels();
    srvDesc.Texture2D.MostDetailedMip = 0;

    UINT srvDescSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12DescriptorHeap* srvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SRVHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE destHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    destHandle.ptr += slot * srvDescSize;
    d3dDevice->CreateShaderResourceView(texture, &srvDesc, destHandle);

    // Store resources
    PreviewTextureSlot& textureSlot = m_TextureSlots[slot];
    textureSlot.handle = handle;
    textureSlot.path = path;
    RefreshHasTextureFlag();

    return true;
}

void MaterialPreviewRenderer::ReleaseTextureSlot(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(m_TextureSlots.size()))
        return;

    PreviewTextureSlot& textureSlot = m_TextureSlots[slot];

    textureSlot.handle = {};
    textureSlot.path.clear();

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (d3dDevice && m_SRVHeap)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
        nullDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullDesc.Texture2D.MipLevels = 1;
        nullDesc.Texture2D.MostDetailedMip = 0;

        ID3D12DescriptorHeap* srvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SRVHeap);
        UINT srvDescSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE destHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        destHandle.ptr += slot * srvDescSize;
        d3dDevice->CreateShaderResourceView(nullptr, &nullDesc, destHandle);
    }

    RefreshHasTextureFlag();
}

void MaterialPreviewRenderer::RefreshSamplerDescriptors()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice || !m_SamplerHeap)
        return;

    D3D12_FILTER filters[] = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                              D3D12_FILTER_MIN_MAG_MIP_LINEAR};
    D3D12_TEXTURE_ADDRESS_MODE addressModes[] = {D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                                 D3D12_TEXTURE_ADDRESS_MODE_MIRROR};

    ID3D12DescriptorHeap* samplerHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);
    UINT samplerDescSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle = samplerHeap->GetCPUDescriptorHandleForHeapStart();

    for (const PreviewTextureSlot& textureSlot : m_TextureSlots)
    {
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = filters[std::clamp(textureSlot.filterMode, 0, 2)];
        samplerDesc.AddressU = addressModes[std::clamp(textureSlot.wrapMode, 0, 2)];
        samplerDesc.AddressV = samplerDesc.AddressU;
        samplerDesc.AddressW = samplerDesc.AddressU;
        samplerDesc.MipLODBias = 0;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        d3dDevice->CreateSampler(&samplerDesc, samplerHandle);
        samplerHandle.ptr += samplerDescSize;
    }
}

void MaterialPreviewRenderer::RefreshHasTextureFlag()
{
    m_HasTexture = false;
    for (const PreviewTextureSlot& textureSlot : m_TextureSlots)
    {
        if (textureSlot.handle.IsReady() && textureSlot.handle->GetTexture())
        {
            m_HasTexture = true;
            break;
        }
    }
}

bool MaterialPreviewRenderer::CreateRenderTarget()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice)
        return false;

    // Create render target texture
    RHITextureDesc rtDesc;
    rtDesc.width = m_Size;
    rtDesc.height = m_Size;
    rtDesc.format = RHIFormat::R8G8B8A8_UNORM;
    rtDesc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
    rtDesc.debugName = "MaterialPreviewRT";

    m_RenderTarget = m_Device->CreateTexture(rtDesc);
    if (!m_RenderTarget)
        return false;

    // Create RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* rtvHeap = nullptr;
    HRESULT hr = d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr))
        return false;
    m_RTVHeap = rtvHeap;

    // Create RTV
    ID3D12Resource* rtResource =
        reinterpret_cast<ID3D12Resource*>(m_Device->GetNativeTextureResource(m_RenderTarget.get()));
    d3dDevice->CreateRenderTargetView(rtResource, nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create depth buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = m_Size;
    depthDesc.Height = m_Size;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    ID3D12Resource* depthBuffer = nullptr;
    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&depthBuffer));
    if (FAILED(hr))
        return false;
    m_DepthBuffer = depthBuffer;

    // Create DSV heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* dsvHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
    if (FAILED(hr))
        return false;
    m_DSVHeap = dsvHeap;

    // Create DSV
    d3dDevice->CreateDepthStencilView(depthBuffer, nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());

    // Register with ImGui
    m_ImGuiTexId = m_GUI->RegisterTexture(rtResource);

    return true;
}

bool MaterialPreviewRenderer::CreateShaders()
{
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    // Compile vertex shader
    HRESULT hr = D3DCompile(s_PreviewVS, strlen(s_PreviewVS), "PreviewVS", nullptr, nullptr, "main", "vs_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            printf("VS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    // Compile pixel shader
    hr = D3DCompile(s_PreviewPS, strlen(s_PreviewPS), "PreviewPS", nullptr, nullptr, "main", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            printf("PS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    // Store bytecode
    m_VSBytecode.resize(vsBlob->GetBufferSize());
    memcpy(m_VSBytecode.data(), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());

    m_PSBytecode.resize(psBlob->GetBufferSize());
    memcpy(m_PSBytecode.data(), psBlob->GetBufferPointer(), psBlob->GetBufferSize());

    return true;
}

bool MaterialPreviewRenderer::CreatePipelineState()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice)
        return false;

    // Descriptor range for texture SRV
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;
    srvRange.BaseShaderRegister = 0; // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    // Sampler descriptor range for dynamic sampler
    D3D12_DESCRIPTOR_RANGE samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 4;
    samplerRange.BaseShaderRegister = 0; // s0
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Create root signature with constants, texture, and sampler
    D3D12_ROOT_PARAMETER rootParams[3] = {};
    // Root param 0: Constants (already set above - need to redo)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.Num32BitValues = 56;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // Root param 1: Texture SRV
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // Root param 2: Sampler
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 0; // No static samplers - we use dynamic
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &errorBlob);
    if (FAILED(hr))
        return false;

    if (!m_RootSignature)
    {
        ID3D12RootSignature* rootSig = nullptr;
        hr = d3dDevice->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&rootSig));
        if (FAILED(hr))
            return false;
        m_RootSignature = rootSig;
    }

    if (!m_SRVHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 4;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ID3D12DescriptorHeap* srvHeap = nullptr;
        hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
        if (FAILED(hr))
            return false;
        m_SRVHeap = srvHeap;

        for (int slot = 0; slot < 4; ++slot)
            ReleaseTextureSlot(slot);
    }

    if (!m_SamplerHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
        samplerHeapDesc.NumDescriptors = 4;
        samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ID3D12DescriptorHeap* samplerHeap = nullptr;
        hr = d3dDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&samplerHeap));
        if (FAILED(hr))
            return false;
        m_SamplerHeap = samplerHeap;
    }

    RefreshSamplerDescriptors();

    // Input layout (matches PreviewVertex: pos, normal, uv)
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    // Pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>(m_RootSignature);
    psoDesc.VS = {m_VSBytecode.data(), m_VSBytecode.size()};
    psoDesc.PS = {m_PSBytecode.data(), m_PSBytecode.size()};
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // Debug: Disable culling
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr))
        return false;
    m_PipelineState = pso;

    return true;
}

bool MaterialPreviewRenderer::CreateSphereMesh()
{
    // Generate UV sphere with proper texture coordinates
    const int latSegments = 32;
    const int lonSegments = 64;
    const float radius = 0.5f;
    const float PI = 3.14159265f;

    std::vector<PreviewVertex> vertices;
    std::vector<uint16_t> indices;

    // Generate vertices with UVs
    for (int lat = 0; lat <= latSegments; ++lat)
    {
        float theta = lat * PI / latSegments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);
        float v = static_cast<float>(lat) / latSegments;

        for (int lon = 0; lon <= lonSegments; ++lon)
        {
            float phi = lon * 2.0f * PI / lonSegments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);
            float u = static_cast<float>(lon) / lonSegments;

            float x = cosPhi * sinTheta;
            float y = cosTheta;
            float z = sinPhi * sinTheta;

            PreviewVertex vtx;
            vtx.x = x * radius;
            vtx.y = y * radius;
            vtx.z = z * radius;
            vtx.nx = x;
            vtx.ny = y;
            vtx.nz = z;
            vtx.u = u;
            vtx.v = v;
            vtx.tx = -sinPhi;
            vtx.ty = 0.0f;
            vtx.tz = cosPhi;
            vtx.tw = 1.0f;
            vertices.push_back(vtx);
        }
    }

    // Generate indices
    for (int lat = 0; lat < latSegments; ++lat)
    {
        for (int lon = 0; lon < lonSegments; ++lon)
        {
            uint16_t first = static_cast<uint16_t>(lat * (lonSegments + 1) + lon);
            uint16_t second = static_cast<uint16_t>(first + lonSegments + 1);

            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }

    // Create vertex buffer
    RHIBufferDesc vbDesc;
    vbDesc.size = vertices.size() * sizeof(PreviewVertex);
    vbDesc.usage = RHIBufferUsage::Vertex;
    vbDesc.memory = RHIMemoryUsage::CPU_To_GPU;

    m_VertexBuffer = m_Device->CreateBuffer(vbDesc);
    if (!m_VertexBuffer)
        return false;
    m_VertexBuffer->Update(vertices.data(), vbDesc.size);

    // Create index buffer
    RHIBufferDesc ibDesc;
    ibDesc.size = indices.size() * sizeof(uint16_t);
    ibDesc.usage = RHIBufferUsage::Index;
    ibDesc.memory = RHIMemoryUsage::CPU_To_GPU;

    m_IndexBuffer = m_Device->CreateBuffer(ibDesc);
    if (!m_IndexBuffer)
        return false;
    m_IndexBuffer->Update(indices.data(), ibDesc.size);

    m_IndexCount = static_cast<uint32_t>(indices.size());

    return true;
}

bool MaterialPreviewRenderer::CreateConstantBuffer()
{
    // No separate CB needed - using root constants
    return true;
}

void MaterialPreviewRenderer::Render(float albedoR, float albedoG, float albedoB, float metallic, float roughness,
                                     float rotation)
{
    if (!m_Initialized)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return;

    ID3D12Resource* rtResource =
        reinterpret_cast<ID3D12Resource*>(m_Device->GetNativeTextureResource(m_RenderTarget.get()));

    // Transition to render target
    // On first render, resource is in COMMON state; after that, it's in PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = rtResource;
    barrier.Transition.StateBefore =
        m_FirstRender ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    m_FirstRender = false;

    // Set render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        reinterpret_cast<ID3D12DescriptorHeap*>(m_RTVHeap)->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        reinterpret_cast<ID3D12DescriptorHeap*>(m_DSVHeap)->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Clear
    float clearColor[4] = {0.1f, 0.1f, 0.12f, 1.0f};
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_Size), static_cast<float>(m_Size), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_Size), static_cast<LONG>(m_Size)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set pipeline
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(m_PipelineState));
    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_RootSignature));

    // Build matrices
    const float PI = 3.14159265f;
    float fov = 45.0f * PI / 180.0f;
    float nearZ = 0.1f, farZ = 100.0f;

    // Camera at Z=-2.5, looking at origin (D3D left-handed: +Z is forward)
    float camZ = -2.5f;
    float camY = 0.5f;

    // Simple perspective projection (row-major for v * M)
    float h = 1.0f / tanf(fov * 0.5f);
    float proj[16] = {h, 0, 0, 0, 0, h, 0, 0, 0, 0, farZ / (farZ - nearZ), 1, 0, 0, -nearZ * farZ / (farZ - nearZ), 0};

    // View matrix (translate world so camera is at origin)
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -camY, -camZ, 1};

    // World matrix - identity (no rotation of geometry)
    // Rotation will be applied to the light direction instead for a turntable effect
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    // MVP = World * View * Proj
    // Matrices are row-major, so we compute v * World * View * Proj
    float wv[16], mvp[16];

    // wv = World * View
    for (int c = 0; c < 4; c++) // Row
    {
        for (int r = 0; r < 4; r++) // Col
        {
            wv[c * 4 + r] = 0;
            for (int k = 0; k < 4; k++)
                wv[c * 4 + r] += world[c * 4 + k] * view[k * 4 + r];
        }
    }

    // mvp = wv * Proj (World * View * Proj)
    for (int c = 0; c < 4; c++) // Row
    {
        for (int r = 0; r < 4; r++) // Col
        {
            mvp[c * 4 + r] = 0;
            for (int k = 0; k < 4; k++)
                mvp[c * 4 + r] += wv[c * 4 + k] * proj[k * 4 + r];
        }
    }

    // Set constants: MVP(16) + World(16) + LightDir(3) + HasTexture(1) + CameraPos(3) + _Time(1) + Albedo(3) +
    // Metallic(1) + Roughness(1) + _pad2(1) + UVTiling(2) + UVOffset(2) + PannerSpeed(2) + _pad3(2) = 56 floats
    m_ElapsedTime += 0.016f; // Approximate 60fps delta time
    float constants[56];
    memcpy(constants, mvp, 16 * sizeof(float));
    memcpy(constants + 16, world, 16 * sizeof(float));

    // Light direction - rotate around Y based on rotation slider
    float rotRad = rotation * PI / 180.0f;
    float baseLightX = 0.5f, baseLightZ = 0.5f;
    constants[32] = baseLightX * cosf(rotRad) - baseLightZ * sinf(rotRad);
    constants[33] = -0.7f; // Y stays fixed
    constants[34] = baseLightX * sinf(rotRad) + baseLightZ * cosf(rotRad);
    constants[35] = m_HasTexture ? 1.0f : 0.0f; // HasTexture flag
    // Camera position + Time
    constants[36] = 0.0f;
    constants[37] = camY;
    constants[38] = camZ;
    constants[39] = m_ElapsedTime; // _Time for animation
    // Material
    constants[40] = albedoR;
    constants[41] = albedoG;
    constants[42] = albedoB;
    constants[43] = metallic;
    constants[44] = roughness;
    constants[45] = 0.0f; // _pad2
    // UV Transform
    constants[46] = m_TilingU;
    constants[47] = m_TilingV;
    constants[48] = m_OffsetU;
    constants[49] = m_OffsetV;
    // Panner Speed
    constants[50] = m_PannerSpeedU;
    constants[51] = m_PannerSpeedV;
    // Channel selection
    constants[52] = (float)m_Channel;
    constants[53] = 0.0f; // _pad3
    constants[54] = 0.0f;
    constants[55] = 0.0f;

    cmdList->SetGraphicsRoot32BitConstants(0, 56, constants, 0);

    // Bind texture and sampler if available
    if (m_SRVHeap && m_SamplerHeap)
    {
        // Set both descriptor heaps (SRV and Sampler must be separate heaps)
        ID3D12DescriptorHeap* srvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SRVHeap);
        ID3D12DescriptorHeap* samplerHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);
        ID3D12DescriptorHeap* heaps[] = {srvHeap, samplerHeap};
        cmdList->SetDescriptorHeaps(2, heaps);

        // Bind SRV (root param 1)
        cmdList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
        cmdList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());
    }

    // Set mesh
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = GetD3D12BufferGPUAddress(m_VertexBuffer.get());
    vbView.SizeInBytes = static_cast<UINT>(m_VertexBuffer->GetSize());
    vbView.StrideInBytes = sizeof(PreviewVertex);
    cmdList->IASetVertexBuffers(0, 1, &vbView);

    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = GetD3D12BufferGPUAddress(m_IndexBuffer.get());
    ibView.SizeInBytes = static_cast<UINT>(m_IndexBuffer->GetSize());
    ibView.Format = DXGI_FORMAT_R16_UINT;
    cmdList->IASetIndexBuffer(&ibView);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw
    cmdList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);

    // Transition back to shader resource
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}

void MaterialPreviewRenderer::UpdateCustomMaterial(const std::string& surfaceHLSL)
{
    if (!m_Initialized)
        return;

    // Build the full shader source with the custom surface function
    // Order: template (cbuffer + structs) + define + custom code at end for function implementation
    std::string fullSource = "#define MATERIAL_SURFACE_CUSTOM\n";
    fullSource += s_PreviewPS; // Template with cbuffer, structs, forward declare
    fullSource += "\n\n// Custom surface implementation:\n";
    fullSource += surfaceHLSL; // Custom GetMaterialSurface implementation

    ComPtr<ID3DBlob> psBlob, errorBlob;
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    HRESULT hr = D3DCompile(fullSource.c_str(), fullSource.length(), "PreviewPS_Custom", nullptr, nullptr, "main",
                            "ps_5_0", compileFlags, 0, &psBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("MaterialPreviewRenderer: Custom PS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());

        // Fill with standard shader bytecode as fallback
        CreateShaders();
    }
    else
    {
        // Update bytecode
        m_PSBytecode.resize(psBlob->GetBufferSize());
        memcpy(m_PSBytecode.data(), psBlob->GetBufferPointer(), psBlob->GetBufferSize());
    }

    // Re-create the pipeline state with the new pixel shader
    if (m_PipelineState)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_PipelineState)->Release();
        m_PipelineState = nullptr;
    }

    CreatePipelineState();

    std::printf("MaterialPreviewRenderer: Updated custom material shader.\n");
}

} // namespace Dot
