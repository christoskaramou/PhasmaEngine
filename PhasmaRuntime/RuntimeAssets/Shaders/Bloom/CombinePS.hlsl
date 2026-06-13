#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

TexSamplerDecl(0, 0, Frame)
TexSamplerDecl(1, 0, GaussianVertical)

[[vk::push_constant]] PushConstants_Bloom_Combine pc;

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float4 sceneColor = Frame.Sample(sampler_Frame, input.uv);
    float4 bloom = float4(GaussianVertical.Sample(sampler_GaussianVertical, input.uv).xyz, 0.0);

    output.color = sceneColor + bloom * pc.strength;

    return output;
}
