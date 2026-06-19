struct PSInput
{
    float4 svPos                   : SV_Position;
    [[vk::location(0)]] float cell : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return float4(input.cell, input.cell, input.cell, 1.f);
}
