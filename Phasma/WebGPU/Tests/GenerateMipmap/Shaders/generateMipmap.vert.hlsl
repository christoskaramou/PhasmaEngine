struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint baseArrayLayer : BASE_LAYER;
};

VSOutput VSMain(uint vertexIndex : SV_VertexID, uint baseArrayLayer : SV_InstanceID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0),
    };

    VSOutput o;
    float2 xy = positions[vertexIndex];
    o.position = float4(xy, 0.0, 1.0);
    o.texcoord = xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    o.baseArrayLayer = baseArrayLayer;
    return o;
}
