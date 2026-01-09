#include "../Common/Common.hlsl"

struct PushConstants
{
    float sharpness;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::binding(0)]] Texture2D<float4> in_color : register(t0);
[[vk::binding(1)]] RWTexture2D<float4> out_color : register(u0);

// CAS Algorithm
// R.C.A.S. - Robust Contrast Adaptive Sharpening
// Based on AMD FSR1
void RCAS(int2 coord, float sharpness)
{
    // Fetch 3x3 neighborhood
    // a b c 
    // d e f
    // g h i
    float3 e = in_color.Load(int3(coord, 0)).rgb;
    float3 b = in_color.Load(int3(coord + int2(0, -1), 0)).rgb;
    float3 d = in_color.Load(int3(coord + int2(-1, 0), 0)).rgb;
    float3 f = in_color.Load(int3(coord + int2(1, 0), 0)).rgb;
    float3 h = in_color.Load(int3(coord + int2(0, 1), 0)).rgb;
    
    // Soft min and max.
    float3 mnRGB = min(min(min(d, e), min(f, b)), h);
    float3 mxRGB = max(max(max(d, e), max(f, b)), h);
    
    // Smooth minimum distance to signal limit divided by smooth max.
    // Amp-factor.
    float3 ampRGB = saturate(min(mnRGB, 2.0 - mxRGB) / (mxRGB + 0.0001)); // epsilon for div0
    
    // Sharpening amount.
    // Map sharpness [0..1] to [0..-0.2].
    // Note: RCAS weights are negative. 
    // -0.2 is a safe limit to avoid divide by zero (4w+1 -> 0.2).
    float base_w = sharpness * -0.2; 
    
    // Shaping amount of sharpening.
    float3 wRGB = ampRGB * base_w;
    
    // Filter shape.
    float3 rcasRGB = (b + d + f + h) * wRGB + e;
    
    // Normalization.
    rcasRGB /= (4.0 * wRGB + 1.0);
    
    out_color[coord] = float4(rcasRGB, 1.0);
}

// Simple version using fixed weights if the above is too complex or different
// But implementing the "Robust" logic correctly:

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 coord = int2(id.xy);
    RCAS(coord, pc.sharpness);
}
