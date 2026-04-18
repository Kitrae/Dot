cbuffer Constants : register(b0)
{
    uint srcMip;
    uint dstWidth;
    uint dstHeight;
    uint pad;
};

Texture2D<float> srcTex : register(t0);
RWTexture2D<float> dstTex : register(u0);
SamplerState pointSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dstWidth || id.y >= dstHeight)
    {
        return;
    }

    float2 srcSize = float2(dstWidth * 2, dstHeight * 2);
    float2 uv = (float2(id.xy) * 2.0 + 0.5) / srcSize;
    float2 texelSize = 1.0 / srcSize;

    float d00 = srcTex.SampleLevel(pointSampler, uv, 0);
    float d10 = srcTex.SampleLevel(pointSampler, uv + float2(texelSize.x, 0), 0);
    float d01 = srcTex.SampleLevel(pointSampler, uv + float2(0, texelSize.y), 0);
    float d11 = srcTex.SampleLevel(pointSampler, uv + texelSize, 0);

    dstTex[id.xy] = max(max(d00, d10), max(d01, d11));
}
