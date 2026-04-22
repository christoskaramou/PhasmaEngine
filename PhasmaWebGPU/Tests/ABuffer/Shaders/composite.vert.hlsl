// Fullscreen 6-vertex triangle-list quad; positions derived from SV_VertexID.

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    float2 positions[6] =
    {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
        float2( 1.0,  1.0),
        float2(-1.0,  1.0),
    };

    return float4(positions[vertexId], 0.0, 1.0);
}
