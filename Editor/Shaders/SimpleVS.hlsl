// Simple Vertex Shader - Transforms position and passes color
// Compiled with: dxc -T vs_6_0 -E VSMain SimpleVS.hlsl -Fo SimpleVS.cso

cbuffer ConstantBuffer : register(b0)
{
    float4x4 MVP;  // Model-View-Projection matrix
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.Position = mul(MVP, float4(input.Position, 1.0f));
    output.Color = input.Color;
    return output;
}
