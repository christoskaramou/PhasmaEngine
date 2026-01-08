#include "../Common/Common.hlsl"

struct PushConstants
{
    float2 resolution; // Input resolution
    float2 displaySize; // Output resolution
    float2 jitter;
    float2 padding;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;
[[vk::binding(0)]] Texture2D<float4> in_color : register(t0);
[[vk::binding(1)]] Texture2D<float4> in_history : register(t1);
[[vk::binding(2)]] Texture2D<float2> in_velocity : register(t2);
[[vk::binding(3)]] Texture2D<float> in_depth : register(t3);
[[vk::binding(4)]] SamplerState sampler_linear_clamp : register(s0);
[[vk::binding(5)]] RWTexture2D<float4> out_color : register(u0);

// Simple Catmull-Rom sampling for history could be added later for better quality
// For now using bilinear

float3 ClipAABB(float3 aabbMin, float3 aabbMax, float3 prevSample, float3 avg)
{
    // Clip history to the neighborhood of the current frame
    // This reduces ghosting by rejecting history that is too different from current
    float3 p_clip = 0.5 * (aabbMax + aabbMin);
    float3 e_clip = 0.5 * (aabbMax - aabbMin);

    float3 v_clip = prevSample - p_clip;
    float3 v_unit = v_clip.xyz / e_clip.xyz;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return prevSample;
}

// Tonemap to reduce flickering (Reinhard)
float3 Tonemap(float3 x) { return x / (1.0 + Luminance(x)); }
float3 InverseTonemap(float3 x) { return x / (1.0 - Luminance(x)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 coord = int2(id.xy);
    float2 uv = (float2(coord) + 0.5) / pc.displaySize;

    if (any(uv > 1.0)) return;
    
    // Sample current color with handling for upscaling
    // Since we are upscaling, we sample the low-res input at high-res UVs
    float3 centerColor = in_color.SampleLevel(sampler_linear_clamp, uv, 0).rgb;
    
    // Calculate AABB in the low-res input space
    // We want the 3x3 block of texels in the input image that surround this UV
    float3 colorMin = 10000.0;
    float3 colorMax = -10000.0;
    float3 colorAvg = 0.0;

    int2 lowResCenter = int2(floor(uv * pc.resolution));

    [unroll]
    for(int x = -1; x <= 1; ++x)
    {
        [unroll]
        for(int y = -1; y <= 1; ++y)
        {
            float3 neighbor = in_color.Load(int3(lowResCenter + int2(x, y), 0)).rgb;
            colorMin = min(colorMin, neighbor);
            colorMax = max(colorMax, neighbor);
            colorAvg += neighbor;
        }
    }
    colorAvg /= 9.0;
    
    // Simple Unsharp Mask on input
    // Sharpening input helps to keep the detail after temporal blending
    float sharpenStrength = 0.5; // 0.0 - 1.0
    centerColor = centerColor + (centerColor - colorAvg) * sharpenStrength;
    centerColor = max(0.0, centerColor);

    float2 velocity = in_velocity.SampleLevel(sampler_linear_clamp, uv, 0).xy;
    
    // Convert to UV space (factor 0.5) and add it to current UV to get previous UV
    // Velocity is from GBuffer (Low Res), stored as (PrevNDC - CurrNDC)
    // Works correctly regardless of resolution as it is screen relative
    float2 historyUV = uv + velocity * 0.5;

    // Out of bounds
    if (any(historyUV < 0.0) || any(historyUV > 1.0))
    {
        out_color[coord] = float4(centerColor, 1.0);
        return;
    }

    float3 historyColor = in_history.SampleLevel(sampler_linear_clamp, historyUV, 0).rgb;

    // Anti-flicker: clamp history
    historyColor = ClipAABB(colorMin, colorMax, historyColor, colorAvg);

    // Feedback factor
    // Scale velocity by Display Resolution for pixel-accurate motion check in high res
    float velocityLength = length(velocity * pc.displaySize); 
    
    // Base feedback
    float feedback = 0.96;
    
    // Only reduce feedback on very fast motion to minimize ghosting trails
    // But keep it high enough to suppress jitter
    float velocityFactor = saturate(1.0 - velocityLength * 0.01); 
    feedback *= velocityFactor;
    
    // Never drop low, jitter becomes visible
    feedback = max(feedback, 0.85);
    
    // Tonemap before blending to reduce fireflies
    float3 tc = Tonemap(centerColor);
    float3 th = Tonemap(historyColor);
    
    // Blend
    float3 result = lerp(tc, th, feedback);
    
    result = InverseTonemap(result);

    out_color[coord] = float4(result, 1.0);
}
