#include "FXAA.hlsl"
#include "../Common/Structures.hlsl"

TexSamplerDecl(0, 0, Frame)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    output.color.xyz = FXAA(Frame, sampler_Frame, input.uv);
    output.color.w = Frame.Sample(sampler_Frame, input.uv).w;

    return output;
}
