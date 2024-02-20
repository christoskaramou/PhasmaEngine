#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

TexSamplerDecl(0, 0, Composition)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float4 color = Composition.Sample(sampler_Composition, input.uv);
    float luminance = Luminance(color.rgb);
    output.color = float4(color.xyz * luminance, 1.0);

    return output;
}
