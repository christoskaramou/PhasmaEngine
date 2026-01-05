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
    float2 seed = float2(index * 1.37, pc.totalTime * (index + 1));
    
    // Spawn at bottom center
    float3 center = float3(0.0, -1.0, 0.0);
    float radius = sqrt(rand(seed)) * 0.3;
    float angle = rand(seed) * 6.2831853;
    float2 ring = float2(cos(angle), sin(angle)) * radius;
    p.position.xyz = center + float3(ring.x, 0.0, ring.y);
    
    // Random Life (Short for fire)
    float life = 0.9 + rand(seed) * 0.7; // 0.9-1.6 seconds
    p.position.w = life;
    p.extra.y = life;
    
    // Upward Velocity with turbulence
    float swirl = 0.6 + rand(seed) * 0.6;
    p.velocity.xyz = float3(ring.x, 0.0, ring.y) * swirl + float3(0.0, 2.0 + rand(seed) * 2.0, 0.0);
    
    // Size
    p.velocity.w = 0.08 + rand(seed) * 0.16;
    
    // Random Texture (0, 1, 2)
    p.extra.x = floor(rand(seed) * 3.0);
    if (p.extra.x > 2.0) p.extra.x = 2.0;
    p.extra.z = rand(seed);
    p.extra.w = rand(seed);

    // Start Color (Bright Yellow/Orange)
    p.color = float4(1.2, 0.9, 0.5, 1.0);
}

[numthreads(256, 1, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    
    if (index >= pc.particleCount) return;

    // Update Life
    particles[index].position.w -= pc.deltaTime;
    
    if (particles[index].position.w <= 0.0)
    {
        Reset(particles[index], index);
    }
    else
    {
        float lifeRatio = saturate(particles[index].position.w / max(particles[index].extra.y, 0.0001));
        float age = 1.0 - lifeRatio;

        float t = pc.totalTime * 2.5 + particles[index].extra.z * 6.2831853;
        float2 swirl = float2(sin(t), cos(t));
        float swirlStrength = lerp(1.2, 0.2, age);
        particles[index].velocity.xz += swirl * (swirlStrength * pc.deltaTime);
        
        // Buoyancy/Gravity (Upward) with light damping
        particles[index].velocity.y += lerp(2.0, 0.6, age) * pc.deltaTime;
        particles[index].velocity.xz *= 1.0 - (0.6 * pc.deltaTime);
        
        // Update Position
        particles[index].position.xyz += particles[index].velocity.xyz * pc.deltaTime;
        
        // Color Fade: White/Yellow -> Orange -> Smoke
        float3 core = float3(1.2, 0.95, 0.6);
        float3 mid = float3(1.0, 0.45, 0.1);
        float3 smoke = float3(0.12, 0.1, 0.08);
        float3 color;
        
        if (lifeRatio > 0.6)
            color = lerp(mid, core, (lifeRatio - 0.6) / 0.4);
        else if (lifeRatio > 0.25)
            color = lerp(smoke, mid, (lifeRatio - 0.25) / 0.35);
        else
            color = lerp(smoke * 0.5, smoke, lifeRatio / 0.25);

        float fadeIn = smoothstep(0.0, 0.12, age);
        float fadeOut = smoothstep(0.0, 0.25, lifeRatio);
        float flicker = 0.85 + 0.3 * sin((pc.totalTime + particles[index].extra.w) * 18.0);
        float alpha = fadeIn * fadeOut * saturate(flicker);
        
        particles[index].color = float4(color, alpha);
    }
}
