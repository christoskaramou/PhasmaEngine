#include "FXAA.hlsl"

TexSamplerDecl(0, 0, Frame)

struct PS_INPUT {
    float2 uv : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 color : SV_Target0;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    output.color.xyz = FXAA(Frame, sampler_Frame, input.uv);
    output.color.w = Frame.Sample(sampler_Frame, input.uv).w;

    return output;
}
