struct Uniforms
{
    column_major float4x4 worldViewProjection;
    column_major float4x4 world;
    float4 color;
};

[[vk::binding(0, 0)]] cbuffer UCB : register(b0, space0)
{
    Uniforms uni;
}

float4 PSMain() : SV_Target0
{
    return uni.color + float4(0.5f, 0.5f, 0.5f, 0.0f);
}
