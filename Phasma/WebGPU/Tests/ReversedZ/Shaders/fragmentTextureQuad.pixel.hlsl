// Translated from webgpu-samples reversedZ/fragmentTextureQuad.wgsl.
// Visualizes the depth buffer by sampling it as a single-channel texture and
// writing the raw value as grayscale.

[[vk::binding(0, 0)]] Texture2D<float> depthTexture : register(t0, space0);

struct PSInput
{
    float4 coord : SV_Position;
};

float4 PSMain(PSInput input) : SV_Target0
{
    int2 pixel       = int2(floor(input.coord.xy));
    float depthValue = depthTexture.Load(int3(pixel, 0));
    return float4(depthValue, depthValue, depthValue, 1.0f);
}
