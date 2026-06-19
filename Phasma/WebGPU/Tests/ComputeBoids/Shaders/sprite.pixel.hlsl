struct PSInput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float4 color  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return input.color;
}
