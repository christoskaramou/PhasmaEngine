#include "../Common/Structures.hlsl"

[[vk::push_constant]] PushConstants_Particle pc;
[[vk::binding(0, 0)]] RWStructuredBuffer<Particle> particles;

// Random number generator
float rand(inout float2 seed)
{
    seed += float2(-0.6, 0.4);
    return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}

// Reset particle
void Reset(inout Particle p, uint index)
{
    float2 seed = float2(index, pc.deltaTime * (index + 1));
    
    // Random position around a center point (e.g., 0,0,5)
    float3 center = float3(0.0, 0.0, 5.0);
    float3 offset = float3(rand(seed) - 0.5, rand(seed) - 0.5, rand(seed) - 0.5) * 5.0; // 5m spread
    p.position.xyz = center + offset;
    
    // Random Life
    p.position.w = 2.0 + rand(seed) * 3.0; // 2-5 seconds
    
    // Random Velocity (Upward tendency?)
    p.velocity.xyz = float3(rand(seed) - 0.5, rand(seed) * 2.0, rand(seed) - 0.5) * 2.0;
    p.velocity.w = 10.0 + rand(seed) * 40.0; // Size 10-50
    
    // Random Color
    p.color = float4(rand(seed), rand(seed), rand(seed), 1.0);
}

[numthreads(256, 1, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    
    // Check bounds
    if (index >= pc.particleCount) return;

    // Update Life
    particles[index].position.w -= pc.deltaTime;
    
    // If dead, reset
    if (particles[index].position.w <= 0.0)
    {
        Reset(particles[index], index);
    }
    else
    {
        // Update Position
        particles[index].position.xyz += particles[index].velocity.xyz * pc.deltaTime;
        
        // Simple Gravity
        particles[index].velocity.y -= 1.0 * pc.deltaTime;
        
        // Color Fade
        particles[index].color.a = clamp(particles[index].position.w, 0.0, 1.0);
    }
}
