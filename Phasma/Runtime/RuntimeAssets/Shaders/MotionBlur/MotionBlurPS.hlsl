#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_MotionBlur pc;

TexSamplerDecl(0, 0, frameTex)
TexSamplerDecl(1, 0, depthTex)
TexSamplerDecl(2, 0, velocityTex)

static const float const_frame_time = 1.0 / 60.0; // Keeps the effect consistent at 60 fps with different frame rates

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;
    float2 texelSize = TexelSize(0, frameTex);
    float2 uvMin = texelSize * 0.5f;
    float2 uvMax = 1.0f - uvMin;
    float4 sharp = frameTex.SampleLevel(sampler_frameTex, clamp(input.uv, uvMin, uvMax), 0);
    output.color = sharp;

    const int sampleBudget = clamp(pc.samples, 1, 32);
    const float blend = saturate(pc.blend);
    if (sampleBudget == 1 || blend == 0.0f || pc.strength <= 0.0f)
        return output;

    float depth = DepthAt(depthTex, sampler_depthTex, input.uv);
    float2 velocity;
    if (depth == 0.0f)
    {
        float2 ndc = UvToNdc(input.uv);
        float4 previous = mul(float4(ndc, 0.0f, 1.0f), pc.skyReprojection);
        if (previous.w <= FLT_EPSILON)
            return output;
        velocity = (previous.xy / previous.w - ndc) * 0.5f;
    }
    else
    {
        float2 uv;
        DilateDepth3X3_UV(depthTex, sampler_depthTex, input.uv, uv);
        velocity = velocityTex.SampleLevel(sampler_velocityTex, uv, 0).xy * 0.5f;
    }

    velocity       *= const_frame_time * pc.oneOverDelta;
    velocity       *= pc.strength;

    float lengthPixels = length(velocity / texelSize);
    // A bit test, not isfinite: WGSL has no isnan or isinf. An all-ones exponent is an infinity or a NaN.
    const bool finite = (asuint(lengthPixels) & 0x7F800000u) != 0x7F800000u;
    if (!finite || lengthPixels <= 1.0f)
        return output;

    // Integrate the whole exposure. Each tap filters the footprint between taps, so a thin
    // silhouette contributes even when its texel falls between two widely spaced midpoints.
    const int samples = int(ceil(min(lengthPixels, float(sampleBudget))));
    const float inv_samples = 1.0f / samples;
    const float2 direction = (velocity / texelSize) / lengthPixels;
    const float2 along = direction * max(1.0f, lengthPixels * inv_samples) * texelSize;
    const float2 across = float2(-direction.y, direction.x) * texelSize;

    float3 accumColor = 0.0f;
    float totalWeight = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        // Midpoints cover both sides equally, including even sample counts.
        float offset = (float(i) + 0.5f) * inv_samples - 0.5f;
        float2 sampleUV = clamp(input.uv + velocity * offset, uvMin, uvMax);
        float4 sampleColor = frameTex.SampleGrad(sampler_frameTex, sampleUV, along, across);

        // Skip nearly transparent samples
        if (sampleColor.a < 0.001f)
            continue;

        // Tent filter weights
        float sampleWeight = 1.0f - abs(offset) * 2.0f;
        accumColor        += sampleColor.rgb * sampleWeight;
        totalWeight       += sampleWeight;
    }

    if (totalWeight > 0.0f)
        output.color.rgb = lerp(sharp.rgb, accumColor / totalWeight, blend);

    return output;
}
