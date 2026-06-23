#include "FXAA.hlsl"
#include "../Common/Structures.hlsl"

struct PushConstants_FXAABlend
{
    float blend;
};
[[vk::push_constant]] ConstantBuffer<PushConstants_FXAABlend> pc;

TexSamplerDecl(0, 0, Frame)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float3 raw = Frame.Sample(sampler_Frame, input.uv).xyz;
    output.color.xyz = lerp(raw, FXAA(Frame, sampler_Frame, input.uv), saturate(pc.blend));
    output.color.w = Frame.Sample(sampler_Frame, input.uv).w;

    return output;
}
