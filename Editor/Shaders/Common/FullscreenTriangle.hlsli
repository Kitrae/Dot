// =============================================================================
// Common - Fullscreen Triangle Vertex Shader
// =============================================================================
// Used by all post-process effects. Generates a fullscreen triangle from 
// vertex ID without any vertex buffer.
// =============================================================================

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput FullscreenVS(uint vertexID : SV_VertexID)
{
    VSOutput output;
    
    // Generate fullscreen triangle from vertex ID
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    output.UV = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    
    return output;
}
