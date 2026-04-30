[[vk::binding(2, 0)]]
cbuffer Uniforms : register(b2, space0)
{
    column_major float4x4 uMatrix;
};

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    float2 pos[6] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
    };

    VSOutput o;
    float2 xy = pos[vertexIndex];
    o.svPos = mul(uMatrix, float4(xy, 0.0f, 1.0f));
    o.texcoord = xy;
    return o;
}
