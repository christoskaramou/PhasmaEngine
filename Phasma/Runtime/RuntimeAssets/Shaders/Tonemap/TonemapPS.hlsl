#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "Tonemap.hlsl"

struct PushConstants_TonemapBlend
{
    float blend;
};
[[vk::push_constant]] ConstantBuffer<PushConstants_TonemapBlend> pc;

TexSamplerDecl(0, 0, Color)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float4 color = Color.Sample(sampler_Color, input.uv);

    output.color.rgb = lerp(color.rgb, ACESFitted(color.rgb), saturate(pc.blend));
    output.color.a = color.a;

    return output;
}
