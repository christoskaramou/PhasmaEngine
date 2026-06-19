static const float2 kPositions[6] = {
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f),
};

[[vk::binding(0, 0)]]
cbuffer UniformsCB : register(b0, space0)
{
    float2 uOffset;
    float2 _pad;
};

struct VSOutput
{
    float4 svPos              : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    const float2 webGpuClip = kPositions[vertexIndex] * float2(1.0f, -1.0f) + uOffset;

    VSOutput output;
    output.svPos = float4(webGpuClip.x, -webGpuClip.y, 0.0f, 1.0f);
    output.uv = kPositions[vertexIndex];
    return output;
}
