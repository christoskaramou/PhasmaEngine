#include "../Common/Common.hlsl"

struct TAAConstants
{
    float2 resolution;
    float2 jitter; // Current frame jitter
};

[[vk::push_constant]] ConstantBuffer<TAAConstants> PC : register(b0);

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
#if 1    
    // Variance clipping (approximate AABB)
    float3 p_clip = 0.5 * (aabbMax + aabbMin);
    float3 e_clip = 0.5 * (aabbMax - aabbMin);

    float3 v_clip = prevSample - p_clip;
    float3 v_unit = v_clip.xyz / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return prevSample;
#else
    // Anchor clip
    return clamp(prevSample, aabbMin, aabbMax);
#endif
}

// Tonemap to reduce flickering (Reinhard)
float3 Tonemap(float3 x) { return x / (1.0 + Luminance(x)); }
float3 InverseTonemap(float3 x) { return x / (1.0 - Luminance(x)); }

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 coord = dispatchThreadID.xy;
    if (coord.x >= (uint)PC.resolution.x || coord.y >= (uint)PC.resolution.y)
        return;

    float2 uv = (float2(coord) + 0.5) / PC.resolution; // Center of pixel

    // Sample current color
    // We should sample neighborhood to build AABB for clamping
    // 3x3 neighborhood
    float3 centerColor = in_color.Load(int3(coord, 0)).rgb;
    float3 colorMin = centerColor;
    float3 colorMax = centerColor;
    float3 colorAvg = centerColor;

    [unroll]
    for(int x = -1; x <= 1; ++x)
    {
        [unroll]
        for(int y = -1; y <= 1; ++y)
        {
            if (x == 0 && y == 0) continue;
            
            float3 neighbor = in_color.Load(int3(coord + int2(x, y), 0)).rgb;
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

    float2 velocity = in_velocity.Load(int3(coord, 0)).xy;
    
    // Velocity is stored as (PrevNDC - CurrNDC) in GBuffer
    // Convert to UV space (factor 0.5) and add it to current UV to get previous UV
    float2 historyUV = uv + velocity * 0.5;

    // Out of bounds
    if (any(historyUV < 0.0) || any(historyUV > 1.0))
    {
        out_color[coord] = float4(centerColor, 1.0);
        return;
    }

    // Sample history
    float3 historyColor = in_history.SampleLevel(sampler_linear_clamp, historyUV, 0).rgb;

    // Clip history
    // Clipping in YCoCg or other spaces is better, but RGB is fine for basic TAA
    historyColor = ClipAABB(colorMin, colorMax, historyColor, colorAvg);

    // Feedback factor
    float velocityLength = length(velocity * PC.resolution); 
    
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
