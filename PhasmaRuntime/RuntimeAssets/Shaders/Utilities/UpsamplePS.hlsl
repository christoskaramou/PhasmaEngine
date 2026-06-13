#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

TexSamplerDecl(0, 0, Frame)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;
    output.color = Frame.Sample(sampler_Frame, input.uv);
    return output;
}
