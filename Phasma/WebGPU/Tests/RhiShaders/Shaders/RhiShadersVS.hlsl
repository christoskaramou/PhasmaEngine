// RhiShaders (WebGPU RHI spec decision 7): a vertex stage reading storage (read-only in WGSL) and push constants.
// Positions is shared with RhiShadersPS, so the two stages' layout facts merge into one entry.
struct Push
{
    float4 tint;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 1)]] StructuredBuffer<float4> Positions;
[[vk::push_constant]] Push pc;

VSOut mainVS(uint vertexId : SV_VertexID)
{
    VSOut output;
    output.position = float4(Positions[vertexId].xy, 0.0, 1.0);
    output.uv = Positions[vertexId].zw * pc.tint.xy;
    return output;
}
