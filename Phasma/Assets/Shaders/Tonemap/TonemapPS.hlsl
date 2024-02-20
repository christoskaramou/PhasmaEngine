#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "Tonemap.hlsl"

TexSamplerDecl(0, 0, Color)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float4 color = Color.Sample(sampler_Color, input.uv);

    output.color.rgb = ACESFitted(color.rgb);
    output.color.a = color.a;

    return output;
}