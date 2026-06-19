// Port of webgpu-samples/sample/skinnedMesh/gltf.wgsl (fragment stage).
// render_mode: 0 = NORMAL, 1 = JOINTS, 2 = WEIGHTS.

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
    [[vk::location(0)]] float3 normal : TEXCOORD0;
    [[vk::location(1)]] float4 jointsF : TEXCOORD1;
    [[vk::location(2)]] float4 weights : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_Target0
{
    if (generalUniforms.renderMode == 1)
        return input.jointsF;
    if (generalUniforms.renderMode == 2)
        return input.weights;
    return float4(input.normal, 1.0f);
}
