#include "dof.hlsl"

struct PushConstants
{ 
    float4 values;
};
[[vk::push_constant]] PushConstants pc;

TexSamplerDecl(0, 0, Color)
TexSamplerDecl(1, 0, Depth)

struct PS_INPUT {
    float2 uv : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 color : SV_Target0;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    output.color.xyz = depthOfField(Color, sampler_Color, Depth, sampler_Depth, input.uv, pc.values.x, pc.values.y);
    output.color.w = Color.Sample(sampler_Color, input.uv).w;

    return output;
}
