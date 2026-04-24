// Distance-sized point (1:1 with upstream distance-sized-points.vert.wgsl):
// the offset is added in clip space WITHOUT the w-multiplier, so the
// perspective divide shrinks the quad as distance grows — farther points
// render smaller, near points render larger.

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

    float2 offset = pos * uSize / uResolution;
    float4 pointPos = float4(offset, 0.0f, 0.0f);

    o.svPos = clipPos + pointPos;
    o.texcoord = pos * 0.5f + 0.5f;
    return o;
}
