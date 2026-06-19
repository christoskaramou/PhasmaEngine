struct VSOutput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float2 fragUV : TEXCOORD0;
};

VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    float2 pos[6] = {
        float2( 1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2(-1.0f, -1.0f),
        float2( 1.0f,  1.0f),
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
    };
    float2 uv[6] = {
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
    };

    VSOutput o;
    float2 clipPos = pos[vertexIndex];
    clipPos.y = -clipPos.y;
    o.svPos  = float4(clipPos, 0.0f, 1.0f);
    o.fragUV = uv[vertexIndex];
    return o;
}
