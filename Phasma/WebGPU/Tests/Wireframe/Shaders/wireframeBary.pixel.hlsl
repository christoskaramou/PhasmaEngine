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
[[vk::binding(3, 0)]] cbuffer LCB : register(b1, space0)
{
    LineUniforms line_;
}

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 bary : TEXCOORD0;
};

float edgeFactor(float3 bary, float thickness)
{
    float3 d = fwidth(bary);
    float3 a3 = smoothstep(float3(0.0f, 0.0f, 0.0f), d * thickness, bary);
    return min(min(a3.x, a3.y), a3.z);
}

float4 PSMain(PSInput input) : SV_Target0
{
    float a = 1.0f - edgeFactor(input.bary, line_.thickness);
    if (a < line_.alphaThreshold)
        discard;
    float3 rgb = (uni.color.rgb + 0.5f) * a;
    return float4(rgb, a);
}
