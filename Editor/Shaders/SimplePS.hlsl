// Simple Pixel Shader - Outputs vertex color
// Compiled with: dxc -T ps_6_0 -E PSMain SimplePS.hlsl -Fo SimplePS.cso

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Color;
}
