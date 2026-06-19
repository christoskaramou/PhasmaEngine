// Fixed screen-space-sized point (1:1 with upstream fixed-size-points.vert.wgsl):
// offset is pre-multiplied by clipPos.w so the subsequent /w leaves a constant
// pixel-size quad regardless of depth.

[[vk::binding(0, 0)]]
cbuffer Uniforms : register(b0, space0)
{
    column_major float4x4 uMatrix;
    float2 uResolution;
    float uSize;
    float uPad;
};

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(VSInput input, uint vNdx : SV_VertexID)
{
    float2 points[6] = {
        float2(-1.0f, -1.0f),
        float2(1.0f, -1.0f),
        float2(-1.0f, 1.0f),
        float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f),
        float2(1.0f, 1.0f),
    };

    VSOutput o;
    float2 pos = points[vNdx];
    float4 clipPos = mul(uMatrix, float4(input.position, 1.0f));

    float2 offset = pos * uSize / uResolution * clipPos.w;
    float4 pointPos = float4(offset, 0.0f, 0.0f);

    o.svPos = clipPos + pointPos;
    o.texcoord = pos * 0.5f + 0.5f;
    return o;
}
