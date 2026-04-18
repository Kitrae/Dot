Texture2D SceneTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer FXAAParams : register(b0)
{
    float2 TexelSize;
    float SubpixelQuality;
    float EdgeThreshold;
};

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 rgbNW = SceneTexture.Sample(LinearSampler, uv + float2(-1, -1) * TexelSize).rgb;
    float3 rgbNE = SceneTexture.Sample(LinearSampler, uv + float2(1, -1) * TexelSize).rgb;
    float3 rgbSW = SceneTexture.Sample(LinearSampler, uv + float2(-1, 1) * TexelSize).rgb;
    float3 rgbSE = SceneTexture.Sample(LinearSampler, uv + float2(1, 1) * TexelSize).rgb;
    float3 rgbM = SceneTexture.Sample(LinearSampler, uv).rgb;

    float3 luma = float3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < EdgeThreshold)
    {
        return float4(rgbM, 1.0);
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * SubpixelQuality, 0.001);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -8.0, 8.0) * TexelSize;

    float3 rgbA = 0.5 *
                  (SceneTexture.Sample(LinearSampler, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                   SceneTexture.Sample(LinearSampler, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 *
                                   (SceneTexture.Sample(LinearSampler, uv + dir * -0.5).rgb +
                                    SceneTexture.Sample(LinearSampler, uv + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);
    if (lumaB < lumaMin || lumaB > lumaMax)
    {
        return float4(rgbA, 1.0);
    }
    return float4(rgbB, 1.0);
}
