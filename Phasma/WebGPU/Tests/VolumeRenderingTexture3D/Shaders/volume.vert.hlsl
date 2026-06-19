struct Uniforms
{
    column_major float4x4 inverseModelViewProjectionMatrix;
};

[[vk::binding(0, 0)]] cbuffer UBO : register(b0, space0)
{
    Uniforms uniforms;
}

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 near : TEXCOORD0;
    [[vk::location(1)]] float3 step : TEXCOORD1;
};

static const uint kNumSteps = 64u;

// Full-screen covering triangle. Matches upstream volume.wgsl vertex_main().
VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    float2 pos[3] = {
        float2(-1.0f, 3.0f),
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
    };
    float2 xy = pos[vertexIndex];

    float4 nearH = mul(uniforms.inverseModelViewProjectionMatrix, float4(xy, 0.0f, 1.0f));
    float4 farH = mul(uniforms.inverseModelViewProjectionMatrix, float4(xy, 1.0f, 1.0f));
    float3 nearW = nearH.xyz / nearH.w;
    float3 farW = farH.xyz / farH.w;

    VSOutput o;
    // Vulkan NDC is Y-down vs WebGPU Y-up - flip Y on the clip position.
    o.svPos = float4(xy.x, -xy.y, 0.0f, 1.0f);
    o.near = nearW;
    o.step = (farW - nearW) / (float)kNumSteps;
    return o;
}
