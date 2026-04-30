float4 VSMain(uint vertexID : SV_VertexID) : SV_Position
{
    float2 pos[6] = {
        float2(-1.0f, -1.0f),
        float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f),
    };
    return float4(pos[vertexID], 0.0f, 1.0f);
}
