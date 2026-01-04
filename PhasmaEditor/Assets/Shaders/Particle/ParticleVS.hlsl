#include "../Common/Structures.hlsl"

[[vk::push_constant]] ConstantBuffer<PerFrameData_Particle> pc;
[[vk::binding(0, 0)]] StructuredBuffer<Particle> particles;

VS_OUTPUT_Particle mainVS(uint vertexID : SV_VertexID)
{
    VS_OUTPUT_Particle output;
    
    Particle p = particles[vertexID];

    output.pos = mul(float4(p.position.xyz, 1.0), pc.viewProjection);
    output.pointSize = p.velocity.w; 
    output.color = p.color;
    output.color.a = clamp(p.position.w, 0.0, 1.0);

    return output;
}
