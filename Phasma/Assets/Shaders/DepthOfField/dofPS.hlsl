#include "../Common/Structures.hlsl"
#include "DOF.hlsl"

[[vk::push_constant]] PushConstants_f2 pc;

TexSamplerDecl(0, 0, Color)
TexSamplerDecl(1, 0, Depth)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    output.color.xyz = depthOfField(Color, sampler_Color, Depth, sampler_Depth, input.uv, pc.values.x, pc.values.y);
    output.color.w = Color.Sample(sampler_Color, input.uv).w;

    return output;
}
