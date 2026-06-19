// Port of webgpu-samples/sample/skinnedMesh/grid.wgsl (fragment stage).

struct GeneralUniforms
{
    uint renderMode;
    uint skinMode;
};

[[vk::binding(0, 1)]] cbuffer GeneralCB : register(b0, space1)
{
    GeneralUniforms generalUniforms;
}

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] float4 jointsF : TEXCOORD1;
    [[vk::location(2)]] float4 weights : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_Target0
{
    if (generalUniforms.renderMode == 1)
        return input.jointsF;
    if (generalUniforms.renderMode == 2)
        return input.weights;
    return float4(255.0f, 0.0f, 1.0f, 1.0f);
}
