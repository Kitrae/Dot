cbuffer Constants : register(b0)
{
    float4x4 invProj;
    uint tileCountX;
    uint tileCountY;
    uint screenWidth;
    uint screenHeight;
    uint numLights;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct Light
{
    float3 position;
    float range;
    float3 color;
    float intensity;
    uint type;
    float3 direction;
    float spotAngle;
    float3 pad;
};

Texture2D<float> depthTex : register(t0);
StructuredBuffer<Light> lights : register(t1);
RWBuffer<uint> lightGrid : register(u0);
RWBuffer<uint> lightIndices : register(u1);

SamplerState pointSampler : register(s0);

#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 256

groupshared uint minDepthInt;
groupshared uint maxDepthInt;
groupshared uint visibleLightCount;
groupshared uint visibleLightIndices[MAX_LIGHTS_PER_TILE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
    {
        minDepthInt = 0xFFFFFFFF;
        maxDepthInt = 0;
        visibleLightCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixelPos = groupId.xy * TILE_SIZE + threadId.xy;
    float2 uv = (float2(pixelPos) + 0.5) / float2(screenWidth, screenHeight);

    float depth = depthTex.SampleLevel(pointSampler, uv, 0);
    uint depthInt = asuint(depth);
    InterlockedMin(minDepthInt, depthInt);
    InterlockedMax(maxDepthInt, depthInt);
    GroupMemoryBarrierWithGroupSync();

    float minDepth = asfloat(minDepthInt);
    float maxDepth = asfloat(maxDepthInt);

    uint lightsPerThread = (numLights + 255) / 256;
    uint startLight = groupIndex * lightsPerThread;
    uint endLight = min(startLight + lightsPerThread, numLights);

    for (uint i = startLight; i < endLight; i++)
    {
        Light light = lights[i];

        float lightDepth = light.position.z;
        float nearZ = minDepth;
        float farZ = maxDepth;

        if (lightDepth - light.range < farZ && lightDepth + light.range > nearZ)
        {
            uint slot;
            InterlockedAdd(visibleLightCount, 1, slot);
            if (slot < MAX_LIGHTS_PER_TILE)
            {
                visibleLightIndices[slot] = i;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0)
    {
        uint tileIndex = groupId.y * tileCountX + groupId.x;
        uint offset = tileIndex * MAX_LIGHTS_PER_TILE;
        uint count = min(visibleLightCount, MAX_LIGHTS_PER_TILE);

        lightGrid[tileIndex * 2] = offset;
        lightGrid[tileIndex * 2 + 1] = count;

        for (uint i = 0; i < count; i++)
        {
            lightIndices[offset + i] = visibleLightIndices[i];
        }
    }
}
