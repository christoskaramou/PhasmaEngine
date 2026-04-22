float4 VSMain(uint vertexIndex : SV_VertexID) : SV_Position
{
    float2 pos[3] = {
        float2( 0.0f,  0.5f),
        float2(-0.5f, -0.5f),
        float2( 0.5f, -0.5f),
    };
    // Flip Y for Vulkan Y-down NDC (HLSL->SPIRV via DXC skips naga's coord flip).
    return float4(pos[vertexIndex].x, -pos[vertexIndex].y, 0.0f, 1.0f);
}
