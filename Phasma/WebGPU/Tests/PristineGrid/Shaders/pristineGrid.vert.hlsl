struct Camera
{
    column_major float4x4 projection;
    column_major float4x4 view;
};

[[vk::binding(0, 0)]] cbuffer CameraCB : register(b0, space0)
{
    Camera camera;
}

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float2 fragUV : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 worldPos = float4(input.position, 1.0f);
    o.svPos         = mul(camera.projection, mul(camera.view, worldPos));
    o.fragUV        = input.uv;
    return o;
}
