#include "../Common/Structures.hlsl"
#include "DOF.hlsl"

struct PushConstants_DOFBlend { float focusScale; float blurRange; float blend; };
[[vk::push_constant]] ConstantBuffer<PushConstants_DOFBlend> pc;

TexSamplerDecl(0, 0, Color)
TexSamplerDecl(1, 0, Depth)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float3 sharp = Color.Sample(sampler_Color, input.uv).xyz;
    float3 dof = depthOfField(Color, sampler_Color, Depth, sampler_Depth, input.uv, pc.focusScale, pc.blurRange);
    output.color.xyz = lerp(sharp, dof, saturate(pc.blend));
    output.color.w = Color.Sample(sampler_Color, input.uv).w;

    return output;
}
