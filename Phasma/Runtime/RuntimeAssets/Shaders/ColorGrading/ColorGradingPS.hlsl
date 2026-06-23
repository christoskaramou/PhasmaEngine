#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

TexSamplerDecl(0, 0, Color)

[[vk::push_constant]] PushConstants_ColorGrading pc;

PS_OUTPUT_Color mainPS(PS_INPUT_UV input)
{
    PS_OUTPUT_Color output;

    float3 color = Color.Sample(sampler_Color, input.uv).rgb;

    // Lift / Gamma / Gain (ASC CDL-style) applied in linear-ish display space.
    // pc.intensity mixes between the graded and untouched color.
    float3 lift = pc.lift.rgb;
    float3 gamma = max(pc.gamma.rgb, float3(0.01, 0.01, 0.01));
    float3 gain = pc.gain.rgb;

    float3 graded = pow(saturate(color * gain + lift), 1.0 / gamma);

    // Saturation around luminance.
    float luminance = dot(graded, float3(0.2126, 0.7152, 0.0722));
    graded = lerp(float3(luminance, luminance, luminance), graded, max(pc.saturation, 0.0));

    // Contrast pivot at 0.5.
    graded = saturate((graded - 0.5) * max(pc.contrast, 0.0) + 0.5);

    float3 finalColor = lerp(color, graded, saturate(pc.intensity));

    // Volume blend: fade the whole effect back toward the unprocessed input.
    finalColor = lerp(color, finalColor, saturate(pc.blend));

    output.color = float4(finalColor, 1.0);
    return output;
}
