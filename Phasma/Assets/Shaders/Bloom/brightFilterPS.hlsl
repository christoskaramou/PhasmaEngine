#include "../Common/common.hlsl"

TexSamplerDecl(0, 0, Composition)

struct PushConstants
{
    float brightness;
    float intensity;
    float range;
    float exposure;
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

    float4 color = Composition.Sample(sampler_Composition, input.uv);
    float brightness = color.x * 0.2627 + color.y * 0.678 + color.z * 0.0593;
    output.color = float4(color.xyz * pow(brightness, pc.brightness), 1.0);

    return output;
}
