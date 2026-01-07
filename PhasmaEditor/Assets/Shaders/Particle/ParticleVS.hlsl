#include "../Common/Structures.hlsl"

[[vk::push_constant]] ConstantBuffer<PerFrameData_Particle> pc;
[[vk::binding(0, 0)]] StructuredBuffer<Particle> particles;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> emitters;

VS_OUTPUT_Particle mainVS(uint vertexID : SV_VertexID)
{
    VS_OUTPUT_Particle output;
    
    uint particleIndex = vertexID / 6;
    uint cornerIndex = vertexID % 6;

    Particle p = particles[particleIndex];

    float3 right = pc.cameraRight.xyz;
    float3 up = pc.cameraUp.xyz;
    float3 forward = pc.cameraForward.xyz; // Needed for cross products

    float lifeRatio = saturate(p.position.w / max(p.extra.y, 0.0001));
    float age = 1.0 - lifeRatio;
    float size = p.velocity.w;
    float3 center = p.position.xyz;
    
    // Animation
    uint emitterIndex = (uint)p.extra.x;
    ParticleEmitter emitter = emitters[emitterIndex];
    float rows = max(emitter.animation.x, 1.0);
    float cols = max(emitter.animation.y, 1.0);
    
    float3 position = center;
    float2 uv = float2(0.0, 1.0);
    float2 uv2 = float2(0.0, 1.0); // Next frame UV
    float blendFactor = 0.0;

    // Quad vertices (Triangle List):
    // 0 - 1
    // | / |
    // 2 - 3
    // Order: 0, 1, 2, 2, 1, 3 (or similar)
    
    // Using 0, 1, 2 for first tri, 2, 1, 3 for second
    // 0: (-1, -1) -> Bottom Left
    // 1: ( 1, -1) -> Bottom Right
    // 2: (-1,  1) -> Top Left
    // 3: ( 1,  1) -> Top Right
    
    // Orientation Logic
    // 0: Billboard (Camera Facing)
    // 1: Horizontal (e.g. valid for floor decals) -> Up is World Up (0,1,0), Normal is (0,1,0), so quad lies on XZ
    // 2: Vertical (e.g. trees/grass) -> Rotates around Y axis to face camera. Up is (0,1,0). Right is Cross(Forward, Up)
    // 3: Velocity -> Aligns with velocity. Up is Velocity, Right is Cross(Velocity, -CameraForward)? 
    
    if (emitter.orientation == 1) // Horizontal
    {
        right = float3(1.0, 0.0, 0.0);
        up = float3(0.0, 0.0, 1.0); // Extending in Z to make quad on XZ plane? 
        // If quad is (-right -up) to (right + up)
        // right=(1,0,0), up=(0,0,1) -> (-1, 0, -1) to (1, 0, 1) -> Flat Quad
    }
    else if (emitter.orientation == 2) // Vertical
    {
        up = float3(0.0, 1.0, 0.0);
        float3 viewDir = normalize(pc.cameraPosition.xyz - center);
        viewDir.y = 0.0; // Projected on XZ
        viewDir = normalize(viewDir);
        right = cross(up, viewDir);
    }
    else if (emitter.orientation == 3) // Velocity
    {
        float3 vel = p.velocity.xyz;
        if (length(vel) > 0.001)
        {
            up = normalize(vel);
            float3 viewDir = normalize(pc.cameraPosition.xyz - center);
            right = normalize(cross(up, viewDir));
        }
    }

    if (cornerIndex == 0) // Bottom Left
    {
        position += (-right - up) * size;
        uv = float2(1.0, 0.0);
    }
    else if (cornerIndex == 1) // Bottom Right
    {
        position += (right - up) * size;
        uv = float2(0.0, 0.0);
    }
    else if (cornerIndex == 2) // Top Left
    {
        position += (-right + up) * size;
        uv = float2(1.0, 1.0);
    }
    else if (cornerIndex == 3) // Top Left (Dup)
    {
        position += (-right + up) * size;
        uv = float2(1.0, 1.0);
    }
    else if (cornerIndex == 4) // Bottom Right (Dup)
    {
        position += (right - up) * size;
        uv = float2(0.0, 0.0);
    }
    else if (cornerIndex == 5) // Top Right
    {
        position += (right + up) * size;
        uv = float2(0.0, 1.0);
    }
    
    uv2 = uv; // Initialize uv2 with base UV

    // Apply UV Animation
    if (rows > 1.0 || cols > 1.0)
    {
        float totalFrames = rows * cols;
        float lifeRatio = saturate(p.position.w / max(p.extra.y, 0.0001));
        float age = 1.0 - lifeRatio; // 0 to 1
        
        // Loop animation over time (not life)
        float maxLife = max(p.extra.y, 0.0001);
        float timeAlive = max(maxLife - p.position.w, 0.0);
        
        // animation.z is now Loops Per Second
        float preciseFrameIndex = timeAlive * emitter.animation.z * totalFrames;
        
        uint currentFrame = (uint)preciseFrameIndex % (uint)totalFrames;
        uint nextFrame = (currentFrame + 1) % (uint)totalFrames;
        
        // Blend Factor: 0.0 = current, 1.0 = next
        // Only if interpolation is enabled (animation.w > 0.0)
        if (emitter.animation.w > 0.0)
            blendFactor = frac(preciseFrameIndex);
        
        // --- Current Frame UV ---
        uint row = currentFrame / (uint)cols;
        uint col = currentFrame % (uint)cols;
        
        uv.x = (uv.x + (float)col) / cols;
        uv.y = (uv.y + (float)row) / rows;
        
        // --- Next Frame UV ---
        uint rowNext = nextFrame / (uint)cols;
        uint colNext = nextFrame % (uint)cols;
        
        uv2.x = (uv2.x + (float)colNext) / cols;
        uv2.y = (uv2.y + (float)rowNext) / rows;
    }

    output.pos = mul(float4(position, 1.0), pc.viewProjection);
    output.color = p.color;
    output.uv = uv;
    output.uv2 = uv2;
    output.blendFactor = blendFactor;
    output.textureIndex = float(emitter.textureIndex); // Read from emitter now

    return output;
}
