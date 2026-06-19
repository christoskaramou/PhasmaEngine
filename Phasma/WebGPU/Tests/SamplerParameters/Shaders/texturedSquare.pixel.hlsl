struct Config
{
    column_major float4x4 viewProj;
    float2 animationOffset;
    float flangeSize;
    float highlightFlange;
};

[[vk::binding(0, 0)]] cbuffer ConfigCB : register(b0, space0)
{
    Config config;
}

[[vk::binding(2, 0)]] SamplerState samp : register(s2, space0);
[[vk::binding(3, 0)]] Texture2D<float4> tex : register(t3, space0);

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float2 uv = input.uv;
    float4 color = tex.Sample(samp, uv);

    bool outOfBounds = uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0;
    if (config.highlightFlange > 0.0 && outOfBounds)
        color += float4(0.7, 0.0, 0.0, 0.0);

    return color;
}
