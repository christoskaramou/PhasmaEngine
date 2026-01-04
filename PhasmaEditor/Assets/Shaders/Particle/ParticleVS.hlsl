#include "../Common/Structures.hlsl"

[[vk::push_constant]] ConstantBuffer<PerFrameData_Particle> pc;
[[vk::binding(0, 0)]] StructuredBuffer<Particle> particles;

struct VS_OUTPUT_Particle
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    [[vk::builtin("PointSize")]] float pointSize : PSIZE;
};

VS_OUTPUT_Particle mainVS(uint vertexID : SV_VertexID)
{
    VS_OUTPUT_Particle output;
    
    Particle p = particles[vertexID];

    output.pos = mul(float4(p.position.xyz, 1.0), pc.viewProjection);
    output.pointSize = p.velocity.w * 0.5; 
    output.color = p.color;
    output.color.a = clamp(p.position.w, 0.0, 1.0);

    return output;
}
