#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "Tonemap.hlsl"

struct PushConstants_TonemapBlend
{
    float blend;
    uint linearColor;
};
[[vk::push_constant]] ConstantBuffer<PushConstants_TonemapBlend> pc;

TexSamplerDecl(0, 0, Color)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float4 color = Color.Sample(sampler_Color, input.uv);

    output.color.rgb = lerp(color.rgb, ACESFitted(color.rgb), saturate(pc.blend));
    if (pc.linearColor != 0)
        output.color.rgb = LinearToSrgb(output.color.rgb); // the one display encode; later passes see display values
    output.color.a = color.a;

    return output;
}
