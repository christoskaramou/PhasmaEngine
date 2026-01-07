#include "../Common/Structures.hlsl"

[[vk::push_constant]] ConstantBuffer<PerFrameData_Particle> pc;
[[vk::binding(0, 0)]] StructuredBuffer<Particle> particles;

VS_OUTPUT_Particle mainVS(uint vertexID : SV_VertexID)
{
    VS_OUTPUT_Particle output;
    
    uint particleIndex = vertexID / 6;
    uint cornerIndex = vertexID % 6;

    Particle p = particles[particleIndex];

    float3 right = pc.cameraRight.xyz;
    float3 up = pc.cameraUp.xyz;

    float lifeRatio = saturate(p.position.w / max(p.extra.y, 0.0001));
    float age = 1.0 - lifeRatio;
    float size = p.velocity.w * lerp(0.6, 1.8, age);
    float3 center = p.position.xyz;

    float3 position = center;
    float2 uv = float2(0.0, 1.0);

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

    output.pos = mul(float4(position, 1.0), pc.viewProjection);
    output.color = p.color;
    output.uv = uv;
    output.textureIndex = p.extra.x;

    return output;
}
