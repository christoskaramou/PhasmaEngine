struct Uniforms
{
    column_major float4x4 mvp;
};

[[vk::binding(0, 0)]] cbuffer Params : register(b0, space0)
{
    Uniforms uniforms;
}

struct VSInput
{
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 svPos                        : SV_Position;
    [[vk::location(0)]] float2 fragUV   : TEXCOORD0;
    [[vk::location(1)]] float4 fragPos  : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.svPos   = mul(uniforms.mvp, input.position);
    o.fragUV  = input.uv;
    o.fragPos = 0.5f * (input.position + float4(1.0f, 1.0f, 1.0f, 1.0f));
    return o;
}
