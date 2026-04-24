[[vk::binding(0, 0)]] Texture2D<float4> tex : register(t0, space0);

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texelCoord : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint mipLevel : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target0
{
    uint2 coord = uint2(input.texelCoord);
    return tex.Load(int3(int2(coord), int(input.mipLevel)));
}
