#include "../Common/Structures.hlsl"

[[vk::push_constant]] PushConstants_Particle pc;
[[vk::binding(0, 0)]] RWStructuredBuffer<Particle> particles;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> emitters;

// PCG hash (Jarzynski & Olano 2020): integer state, so the stream stays uniform however large totalTime grows.
uint Hash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Random number in [0, 1)
float rand(inout uint seed)
{
    seed = Hash(seed);
    return float(seed >> 8) * (1.0 / 16777216.0);
}

// Reset particle
void Reset(inout Particle p, inout uint seed, ParticleEmitter emitter, uint emitterIndex)
{
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
    p.extra.w = emitter.physics.x < 0.0 ? emitter.position.w : rand(seed); // Burst token or random offset
    
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
    float spawnRate = emitter.physics.x;
    bool oneShot = spawnRate < 0.0;
    if (oneShot && currentLife > 0.0 && abs(particles[index].extra.w - emitter.position.w) > 0.5)
    {
        currentLife = 0.0;
        particles[index].position.w = 0.0;
    }
    
    if (currentLife <= 0.0)
    {
        // Respawn check. Reset continues this stream: restarting it made theta repeat the spawn draw, which
        // only passes below spawnRate * dt, and flattened every emitter's spawn sphere into a disc.
        uint seed = Hash(index + Hash(asuint(pc.totalTime)));

        bool shouldSpawn = oneShot ? abs(particles[index].extra.w - emitter.position.w) > 0.5
                                   : rand(seed) < saturate(spawnRate * pc.deltaTime);

        if (shouldSpawn)
        {
            Reset(particles[index], seed, emitter, emitterIndex);
        }
        else
        {
            // Dead particles keep their last alive-frame color, which at 60fps
            // can still be well above the PS alpha-discard threshold. Force
            // alpha to 0 so the corpse stops rendering immediately.
            particles[index].position.w = 0.0;
            particles[index].color.a = 0.0;
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
