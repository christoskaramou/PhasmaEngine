#include "../Common/common.hlsl"

struct PushConstants { float4 values; };
[[vk::push_constant]] PushConstants pc;

TexSamplerDecl(0, 0, frameTex)
TexSamplerDecl(1, 0, depthTex)
TexSamplerDecl(2, 0, velocityTex)

struct PS_INPUT {
    float2 uv : TEXCOORD0;
};
struct PS_OUTPUT {
    float4 color : SV_Target0;
};

static const int max_samples = 16;

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float depth = depthTex.Sample(sampler_depthTex, input.uv).x;
    if (depth == 0.0)
    {
        output.color = frameTex.Sample(sampler_frameTex, input.uv);
        return output;
    }
    
    float2 uv;
    DilateDepth3X3_UV(depthTex, sampler_depthTex, input.uv, uv);
    float2 velocity = velocityTex.Sample(sampler_velocityTex, uv).xy;
    velocity *= pc.values.y; // strength
    velocity *= pc.values.x; // fix for low and high fps giving different velocities
    velocity *= 0.01666666f; // scale the effect 1/60

    if (length2(velocity) < FLT_EPS)
    {
        output.color = frameTex.Sample(sampler_frameTex, input.uv);
        return output;
    }

    float3 color = frameTex.Sample(sampler_frameTex, input.uv).xyz;
    float totalWeight = 0.0;
    float weight = 1.0;
    float factor = 1.0 / max_samples;
    float2 step = velocity * factor;
    float2 UV = input.uv;
    UV += velocity * 0.5; // make samples centered from (UV+velocity/2) to (UV-velocity/2) instead of (UV) to (UV-velocity)
    for (int i = 0; i < max_samples; i++, UV -= step)
    {
        if (!IsSaturated(UV))
        {
            continue;
        }
        float4 texCol = frameTex.Sample(sampler_frameTex, UV);
        if (texCol.a > 0.001) // ignore transparent samples
        {
            weight -= factor * 0.5; // weight can go negative, but most of the times must stay higher than zero
            color += texCol.xyz * weight;
            totalWeight += weight;
        }
    }
    output.color = float4(color / (totalWeight + 1), frameTex.Sample(sampler_frameTex, input.uv).w);

    return output;
}
