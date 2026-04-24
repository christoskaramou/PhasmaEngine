struct Uniforms
{
    column_major float4x4 worldViewProjection;
    column_major float4x4 world;
    float4 color;
};

struct LineUniforms
{
    uint stride;
    float thickness;
    float alphaThreshold;
    float _pad;
};

[[vk::binding(0, 0)]] cbuffer UCB : register(b0, space0)
{
    Uniforms uni;
}
[[vk::binding(1, 0)]] StructuredBuffer<float> positions : register(t0, space0);
[[vk::binding(2, 0)]] StructuredBuffer<uint> indices : register(t1, space0);
[[vk::binding(3, 0)]] cbuffer LCB : register(b1, space0)
{
    LineUniforms line_;
}

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 bary : TEXCOORD0;
};

// Triangle-list; one barycentric per corner.
VSOutput VSMain(uint vNdx : SV_VertexID)
{
    uint vertNdx = vNdx % 3u;
    uint index = indices[vNdx];

    uint pNdx = index * line_.stride;
    float4 position = float4(positions[pNdx], positions[pNdx + 1u], positions[pNdx + 2u], 1.0f);

    VSOutput o;
    o.svPos = mul(uni.worldViewProjection, position);
    float3 b = float3(0.0f, 0.0f, 0.0f);
    if (vertNdx == 0u)
        b.x = 1.0f;
    else if (vertNdx == 1u)
        b.y = 1.0f;
    else
        b.z = 1.0f;
    o.bary = b;
    return o;
}
