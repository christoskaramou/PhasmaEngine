#include "../Common/common.hlsl"
#include "../Common/tonemapping.hlsl"

TexSamplerDecl(0, 0, Frame)
TexSamplerDecl(1, 0, GaussianVertical)

struct PushConstants
{
    float brightness;
    float intensity;
    float range;
    float exposure;
    float useTonemap;
};
[[vk::push_constant]] PushConstants pc;

struct PS_INPUT {
    float2 uv : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 color : SV_Target0;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 sceneColor = Frame.Sample(sampler_Frame, input.uv);
    float4 bloom = float4(GaussianVertical.Sample(sampler_GaussianVertical, input.uv).xyz, 0.0);

    output.color = sceneColor + bloom * pc.intensity;
    
    if (pc.useTonemap > 0.5)
        output.color.xyz = SRGBtoLINEAR(TonemapFilmic(output.color.xyz, pc.exposure));

    return output;
}
