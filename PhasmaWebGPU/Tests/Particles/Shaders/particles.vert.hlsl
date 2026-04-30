struct RenderParams
{
    column_major float4x4 mvp;
    float3 right;
    float  _p0;
    float3 up;
    float  _p1;
};

[[vk::binding(0, 0)]] cbuffer Params : register(b0, space0)
{
    RenderParams render_params;
}

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color    : COLOR0;
    [[vk::location(2)]] float2 quad_pos : TEXCOORD0;
};

struct VSOutput
{
    float4 svPos   : SV_Position;
    [[vk::location(0)]] float4 color    : COLOR0;
    [[vk::location(1)]] float2 quad_pos : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    float3 offset  = render_params.right * input.quad_pos.x
                   + render_params.up    * input.quad_pos.y;
    float3 worldPos = input.position + offset * 0.01f;

    VSOutput o;
    o.svPos   = mul(render_params.mvp, float4(worldPos, 1.0f));
    o.color   = input.color;
    o.quad_pos = input.quad_pos;
    return o;
}
