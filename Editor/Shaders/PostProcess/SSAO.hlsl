// =============================================================================
// SSAO - Cheap Stable Fixed-Pattern Screen Space Ambient Occlusion
// =============================================================================

Texture2D DepthTex : register(t0);
Texture2D NoiseTex : register(t1);
StructuredBuffer<float4> SampleKernel : register(t4);
SamplerState PointClamp : register(s0);
SamplerState WrapSampler : register(s1);

cbuffer SSAOConstants : register(b0)
{
    float4x4 matProj;
    float4 aoParams0; // radius, nearZ, intensity, farZ
    float4 aoParams1; // thickness, sampleCount, unusedCompatibility, unusedCompatibility
    float4 aoParams2; // texelSize.xy, noiseScale.xy
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const int MAX_KERNEL_SAMPLES = 16;

float LinearizeDepth(float d, float nearZ, float farZ)
{
    nearZ = max(nearZ, 0.0001);
    farZ = max(farZ, nearZ + 0.0001);
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float3 ReconstructViewPos(float2 uv, float linearDepth)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    float tanHalfFovY = 1.0 / max(abs(matProj[1][1]), 0.001);
    float aspectRatio = abs(matProj[1][1]) / max(abs(matProj[0][0]), 0.001);

    float3 viewPos;
    viewPos.x = ndc.x * tanHalfFovY * aspectRatio * linearDepth;
    viewPos.y = ndc.y * tanHalfFovY * linearDepth;
    viewPos.z = linearDepth;
    return viewPos;
}

float3 ReconstructNormal(float2 uv, float2 texelSize, float nearZ, float farZ)
{
    float d0 = LinearizeDepth(DepthTex.Sample(PointClamp, uv).r, nearZ, farZ);
    float d1 = LinearizeDepth(DepthTex.Sample(PointClamp, uv + float2(texelSize.x, 0.0)).r, nearZ, farZ);
    float d2 = LinearizeDepth(DepthTex.Sample(PointClamp, uv + float2(0.0, texelSize.y)).r, nearZ, farZ);

    float3 p0 = ReconstructViewPos(uv, d0);
    float3 p1 = ReconstructViewPos(uv + float2(texelSize.x, 0.0), d1);
    float3 p2 = ReconstructViewPos(uv + float2(0.0, texelSize.y), d2);
    float3 normal = normalize(cross(p2 - p0, p1 - p0));
    if (dot(normal, p0) > 0.0)
    {
        normal = -normal;
    }
    return normal;
}

float2 ProjectViewPosToUV(float3 viewPos)
{
    float4 clip = mul(matProj, float4(viewPos, 1.0));
    float invW = 1.0 / max(abs(clip.w), 0.0001);
    float2 ndc = clip.xy * invW;
    return float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
}

float StableRotationNoise(float2 pixelCoord)
{
    return frac(52.9829189 * frac(dot(pixelCoord, float2(0.06711056, 0.00583715))));
}

float2 GetStableRotation(float2 pixelCoord)
{
    const float angle = StableRotationNoise(pixelCoord) * 6.28318530718;
    return float2(cos(angle), sin(angle));
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float rawDepth = DepthTex.Sample(PointClamp, input.uv).r;
    if (rawDepth >= 1.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float radius = aoParams0.x;
    float nearZ = aoParams0.y;
    float intensity = aoParams0.z;
    float farZ = aoParams0.w;
    float thickness = aoParams1.x;
    int sampleCount = clamp((int)round(aoParams1.y), 1, MAX_KERNEL_SAMPLES);
    float2 texelSize = aoParams2.xy;

    float safeRadius = max(radius, 0.001);
    float safeThickness = max(thickness, 0.001);

    float centerDepth = LinearizeDepth(rawDepth, nearZ, farZ);
    float3 centerPos = ReconstructViewPos(input.uv, centerDepth);
    float3 normal = ReconstructNormal(input.uv, texelSize, nearZ, farZ);

    float2 pixelCoord = floor(input.uv / max(texelSize, float2(1e-6, 1e-6)));
    float3 randomVec = float3(GetStableRotation(pixelCoord), 0.0);
    float3 tangent = randomVec - normal * dot(randomVec, normal);
    if (dot(tangent, tangent) < 1e-4)
    {
        tangent = abs(normal.z) < 0.999 ? cross(normal, float3(0.0, 0.0, 1.0)) : cross(normal, float3(0.0, 1.0, 0.0));
    }
    tangent = normalize(tangent);
    float3 bitangent = normalize(cross(normal, tangent));
    float3x3 tbn = float3x3(tangent, bitangent, normal);

    float occlusionAccum = 0.0;
    float weightAccum = 0.0;

    [loop]
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        float3 kernelDir = mul(tbn, SampleKernel[sampleIndex].xyz);
        float3 samplePos = centerPos + kernelDir * safeRadius;
        float2 sampleUV = ProjectViewPosToUV(samplePos);

        if (sampleUV.x <= texelSize.x || sampleUV.x >= 1.0 - texelSize.x || sampleUV.y <= texelSize.y ||
            sampleUV.y >= 1.0 - texelSize.y)
        {
            continue;
        }

        float sampleRawDepth = DepthTex.Sample(PointClamp, sampleUV).r;
        if (sampleRawDepth >= 1.0)
        {
            continue;
        }

        float sampleDepth = LinearizeDepth(sampleRawDepth, nearZ, farZ);
        float3 sampleViewPos = ReconstructViewPos(sampleUV, sampleDepth);
        float3 sampleVec = sampleViewPos - centerPos;
        float sampleDistance = length(sampleVec);
        if (sampleDistance <= 0.0001 || sampleDistance > safeRadius)
        {
            continue;
        }

        float3 sampleDir = sampleVec / sampleDistance;
        float hemisphereWeight = saturate(dot(normal, sampleDir));
        float distanceWeight = saturate(1.0 - sampleDistance / safeRadius);
        float depthWeight = saturate(1.0 - abs(centerDepth - sampleDepth) / safeThickness);
        float sampleWeight = hemisphereWeight * distanceWeight * depthWeight;
        float blocker = sampleDepth < samplePos.z ? 1.0 : 0.0;

        occlusionAccum += blocker * sampleWeight;
        weightAccum += sampleWeight;
    }

    float occlusion = (weightAccum > 0.0) ? (occlusionAccum / weightAccum) : 0.0;
    float ao = saturate(1.0 - occlusion * max(intensity, 0.0));
    ao = max(ao, 0.05);
    return float4(ao, ao, ao, 1.0);
}
