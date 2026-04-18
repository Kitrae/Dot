// =============================================================================
// SSAO Blur - Wider Depth-Aware Bilateral Blur
// =============================================================================

// Runtime/fallback contract: SRV layout is [Depth=t0, Noise=t1, Occlusion=t2, Blurred=t3]
Texture2D AOInput : register(t2);
Texture2D DepthTex : register(t0);
SamplerState PointClamp : register(s0);

cbuffer BlurConstants : register(b0)
{
    float2 texelSize;
    float depthThreshold;
    float nearZ;
    float farZ;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float LinearizeDepth(float d, float nearZ, float farZ)
{
    nearZ = max(nearZ, 0.0001);
    farZ = max(farZ, nearZ + 0.0001);
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float centerDepth = LinearizeDepth(DepthTex.Sample(PointClamp, input.uv).r, nearZ, farZ);
    float totalAO = 0.0;
    float totalWeight = 0.0;

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            float2 sampleUV = input.uv + float2(x, y) * texelSize;
            float sampleAO = AOInput.Sample(PointClamp, sampleUV).r;
            float sampleDepth = LinearizeDepth(DepthTex.Sample(PointClamp, sampleUV).r, nearZ, farZ);

            float spatial = exp(-0.35f * float(x * x + y * y));
            float depthDelta = abs(sampleDepth - centerDepth);
            float depthWeight = exp(-depthDelta * max(depthThreshold, 0.001f));
            float weight = spatial * depthWeight;

            totalAO += sampleAO * weight;
            totalWeight += weight;
        }
    }

    float ao = totalAO / max(totalWeight, 0.001f);
    return float4(ao, ao, ao, 1.0f);
}
