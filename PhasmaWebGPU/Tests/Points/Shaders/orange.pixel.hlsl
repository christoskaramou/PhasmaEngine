// Flat orange fill matching upstream orange.frag.wgsl.

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return float4(1.0f, 0.5f, 0.2f, 1.0f);
}
