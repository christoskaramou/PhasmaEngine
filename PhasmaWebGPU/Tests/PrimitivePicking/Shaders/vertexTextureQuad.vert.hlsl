// Fullscreen triangle-pair vertex shader used by the primitive debug view.

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    float2 pos[6] = {
        float2(-1.0, -1.0),
        float2(1.0, -1.0),
        float2(-1.0, 1.0),
        float2(-1.0, 1.0),
        float2(1.0, -1.0),
        float2(1.0, 1.0),
    };

    VSOutput output;
    output.position = float4(pos[vertexIndex], 0.0, 1.0);
    return output;
}
