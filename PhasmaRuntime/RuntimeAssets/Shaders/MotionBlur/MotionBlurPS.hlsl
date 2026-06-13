#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_MotionBlur pc;

TexSamplerDecl(0, 0, frameTex)
TexSamplerDecl(1, 0, depthTex)
TexSamplerDecl(2, 0, velocityTex)

static const int max_samples        = 8;
static const float inv_max_samples  = 1.0f / max_samples;
static const float const_frame_time = 1.0 / 60.0; // Keeps the effect consistent at 60 fps with different frame rates

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float depth = depthTex.Sample(sampler_depthTex, input.uv).x;
    if (depth == 0.0f)
    {
        output.color = frameTex.Sample(sampler_frameTex, input.uv);
        return output;
    }
    
    float2 uv;
    DilateDepth3X3_UV(depthTex, sampler_depthTex, input.uv, uv);

    float2 velocity = velocityTex.Sample(sampler_velocityTex, uv).xy * 0.5f;
    velocity       *= const_frame_time * pc.oneOverDelta;
    velocity       *= pc.strength;

    // Early out if velocity is below threshold
    if (length2(velocity) < FLT_EPSILON)
    {
        output.color = frameTex.Sample(sampler_frameTex, input.uv);
        return output;
    }

    float3 accumColor = 0.0f;
    float totalWeight = 0.0f;
    float2 sampleUV   = input.uv + velocity * 0.5f; // Center samples around current pixel

    const int samples       = pc.samples;
    const float inv_samples = 1.0f / samples;
    for (int i = 0; i < samples; ++i)
    {
        sampleUV -= velocity * inv_samples;
        sampleUV = saturate(sampleUV); // Clamp to avoid sampling outside of texture bounds

        float4 sampleColor = frameTex.Sample(sampler_frameTex, sampleUV);

        // Skip nearly transparent samples
        if (sampleColor.a < 0.001f)
            continue;

        // Tent filter weights
        float sampleWeight = 1.0f - abs(i - samples / 2) * inv_samples;
        accumColor        += sampleColor.rgb * sampleWeight;
        totalWeight       += sampleWeight;
    }

    if (totalWeight > 0.0f)
    {
        output.color.rgb = accumColor / totalWeight;
        output.color.a   = frameTex.Sample(sampler_frameTex, input.uv).a; // Maintain original alpha
    }
    else
    {
        output.color = frameTex.Sample(sampler_frameTex, input.uv); // Fallback to original color if no contribution from motion blur
    }

    return output;
}
