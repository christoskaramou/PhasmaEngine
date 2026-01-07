#include "../Common/Structures.hlsl"

[[vk::push_constant]] PushConstants_Particle pc;
[[vk::binding(0, 0)]] RWStructuredBuffer<Particle> particles;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> emitters;

// Random number generator
float rand(inout float2 seed)
{
    seed += float2(-0.6, 0.4);
    return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}

// Reset particle
void Reset(inout Particle p, uint index, ParticleEmitter emitter, uint emitterIndex)
{
    float2 seed = float2(index * 1.37, pc.totalTime * (index + 1));
    
    // --- 1. Position ---
    float3 center = emitter.position.xyz;
    float spawnRadius = emitter.physics.y;
    
    // Random point in sphere/circle
    float theta = rand(seed) * 6.2831853;
    float phi = acos(2.0 * rand(seed) - 1.0);
    float r = pow(rand(seed), 0.33) * spawnRadius;
    
    float3 offset;
    offset.x = r * sin(phi) * cos(theta);
    offset.y = r * sin(phi) * sin(theta);
    offset.z = r * cos(phi);
    
    p.position.xyz = center + offset;
    
    // --- 2. Lifetime ---
    float lifeMin = emitter.sizeLife.z;
    float lifeMax = emitter.sizeLife.w;
    float life = lerp(lifeMin, lifeMax, rand(seed));
    p.position.w = life;
    p.extra.y = life; // Store max life in extra.y for ratio calculation
    
    // --- 3. Velocity ---
    float3 baseDirection = emitter.velocity.xyz;
    float noiseStrength = emitter.physics.z;
    
    // Random direction for noise
    float3 noise;
    noise.x = rand(seed) - 0.5;
    noise.y = rand(seed) - 0.5;
    noise.z = rand(seed) - 0.5;
    
    p.velocity.xyz = baseDirection + noise * noiseStrength;
    
    // --- 4. Size & Texture ---
    // Start size
    p.velocity.w = emitter.sizeLife.x; 
    
    p.extra.x = float(emitterIndex); // Store Emitter Index
    p.extra.z = rand(seed); // Random variation factor
    p.extra.w = rand(seed); // Random offset
    
    // --- 5. Color ---
    p.color = emitter.colorStart;
}

[numthreads(256, 1, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    
    if (index >= pc.particleCount)
        return;

    // Find which emitter owns this particle
    ParticleEmitter emitter;
    uint emitterIndex = 0;
    bool found = false;
    
    for (uint i = 0; i < pc.emitterCount; i++)
    {
        ParticleEmitter e = emitters[i];
        if (index >= e.offset && index < (e.offset + e.count))
        {
            emitter = e;
            emitterIndex = i;
            found = true;
            break;
        }
    }
    
    if (!found)
        return; // Should not happen if pc.particleCount matches

    // Update Life
    particles[index].position.w -= pc.deltaTime;
    float currentLife = particles[index].position.w;
    
    if (currentLife <= 0.0)
    {
        // Respawn check
        float2 seed = float2(index * 1.37, pc.totalTime * (index + 1));
        
        float spawnRate = emitter.physics.x;
        float spawnProbability = saturate(spawnRate * pc.deltaTime);
        
        if (rand(seed) < spawnProbability)
        {
            Reset(particles[index], index, emitter, emitterIndex);
        }
        else
        {
            particles[index].position.w = 0.0;
        }
    }
    else
    {
        // Alive Particle Update
        float lifeRatio = 1.0 - saturate(currentLife / max(particles[index].extra.y, 0.0001));
        
        // --- Physics ---
        float drag = emitter.physics.w;
        float3 gravity = emitter.gravity.xyz;
        
        // Apply Gravity
        particles[index].velocity.xyz += gravity * pc.deltaTime;
        
        // Apply Drag
        particles[index].velocity.xyz *= (1.0 - drag * pc.deltaTime);
        
        // Update Position
        particles[index].position.xyz += particles[index].velocity.xyz * pc.deltaTime;
        
        // --- Visuals ---
        // Color Interpolation
        particles[index].color = lerp(emitter.colorStart, emitter.colorEnd, lifeRatio);
        
        // Size Interpolation
        float sizeStart = emitter.sizeLife.x;
        float sizeEnd = emitter.sizeLife.y;
        
        particles[index].velocity.w = lerp(sizeStart, sizeEnd, lifeRatio);
        
        // Update Instance Data
        particles[index].extra.x = float(emitterIndex);
    }
}
