// Translated from webgpu-samples reversedZ/fragment.wgsl.
// Passes through the interpolated vertex color.

struct PSInput
{
    float4 svPos                         : SV_Position;
    [[vk::location(0)]] float4 fragColor : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return input.fragColor;
}
