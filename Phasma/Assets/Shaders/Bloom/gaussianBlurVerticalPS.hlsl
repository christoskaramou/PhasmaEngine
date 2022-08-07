#include "../Common/common.hlsl"

TexSamplerDecl(0, 0, GaussianBlurH);

struct PushConstants
{
    float brightness;
    float intensity;
    float range;
    float exposure;
};
[[vk::push_constant]] PushConstants pc;

struct PS_INPUT {
    float2 uv : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 color : SV_Target0;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float3 texDim = TextureSize(0, GaussianBlurH);
    float texelSize = pc.range / texDim.y;

    float2 coords[11];
    for (int i = -5; i <= 5; i++)
        coords[i+5] = input.uv + float2(0.0, texelSize * i);

    output.color = 0.0;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[0]) * 0.0093;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[1]) * 0.028002;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[2]) * 0.065984;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[3]) * 0.121703;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[4]) * 0.175713;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[5]) * 0.198596;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[6]) * 0.175713;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[7]) * 0.121703;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[8]) * 0.065984;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[9]) * 0.028002;
    output.color += GaussianBlurH.Sample(sampler_GaussianBlurH, coords[10]) * 0.0093;
    output.color.w = 1.0;

    return output;
}
