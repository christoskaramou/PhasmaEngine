struct PSInput
{
    float4 svPos   : SV_Position;
    [[vk::location(0)]] float4 color    : COLOR0;
    [[vk::location(1)]] float2 quad_pos : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float4 color = input.color;
    color.a = color.a * max(1.0f - length(input.quad_pos), 0.0f);
    return color;
}
