struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 fragUV       : TEXCOORD0;
    [[vk::location(1)]] float4 fragPosition : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return input.fragPosition;
}
