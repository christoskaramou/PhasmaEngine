// Translated from webgpu-samples reversedZ/fragmentPrecisionErrorPass.wgsl.
// Samples the pre-pass depth texture at the current pixel and amplifies the
// difference vs the fragment's own clip-space z/w so precision loss becomes
// visible as grayscale output.

[[vk::binding(0, 1)]] Texture2D<float> depthTexture : register(t0, space1);

struct PSInput
{
    float4 coord                       : SV_Position;
    [[vk::location(0)]] float4 clipPos : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    int2 pixel       = int2(floor(input.coord.xy));
    float depthValue = depthTexture.Load(int3(pixel, 0));
    float v          = abs(input.clipPos.z / input.clipPos.w - depthValue) * 2000000.0f;
    return float4(v, v, v, 1.0f);
}
