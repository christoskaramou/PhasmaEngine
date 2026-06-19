#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

TexSamplerDecl(0, 0, Color)

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;
    float4 color = Color.Sample(sampler_Color, input.uv);
    float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    output.color = float4(lum, lum, lum, color.a);
    return output;
}
