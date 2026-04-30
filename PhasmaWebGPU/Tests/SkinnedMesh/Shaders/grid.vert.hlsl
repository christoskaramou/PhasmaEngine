// Port of webgpu-samples/sample/skinnedMesh/grid.wgsl (vertex stage).
// 2D grid vertex format: vec2f position, vec4u joints, vec4f weights.

struct CameraUniforms
{
    column_major float4x4 projMatrix;
    column_major float4x4 viewMatrix;
    column_major float4x4 modelMatrix;
};

struct GeneralUniforms
{
    uint renderMode;
    uint skinMode;
};

[[vk::binding(0, 0)]] cbuffer CameraCB : register(b0, space0)
{
    CameraUniforms cameraUniforms;
}

[[vk::binding(0, 1)]] cbuffer GeneralCB : register(b0, space1)
{
    GeneralUniforms generalUniforms;
}

[[vk::binding(0, 2)]] StructuredBuffer<column_major float4x4> jointMatrices : register(t0, space2);
[[vk::binding(1, 2)]] StructuredBuffer<column_major float4x4> inverseBindMatrices : register(t1, space2);

struct VSInput
{
    [[vk::location(0)]] float2 vertPos : POSITION;
    [[vk::location(1)]] uint4 joints : TEXCOORD0;
    [[vk::location(2)]] float4 weights : TEXCOORD1;
};

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] float4 jointsF : TEXCOORD1;
    [[vk::location(2)]] float4 weights : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 position = float4(input.vertPos.x, input.vertPos.y, 0.0f, 1.0f);

    float4x4 joint0 = mul(jointMatrices[input.joints[0]], inverseBindMatrices[input.joints[0]]);
    float4x4 joint1 = mul(jointMatrices[input.joints[1]], inverseBindMatrices[input.joints[1]]);
    float4x4 joint2 = mul(jointMatrices[input.joints[2]], inverseBindMatrices[input.joints[2]]);
    float4x4 joint3 = mul(jointMatrices[input.joints[3]], inverseBindMatrices[input.joints[3]]);

    float4x4 skinMatrix =
        joint0 * input.weights[0] +
        joint1 * input.weights[1] +
        joint2 * input.weights[2] +
        joint3 * input.weights[3];

    float4x4 mvp =
        mul(cameraUniforms.projMatrix,
            mul(cameraUniforms.viewMatrix, cameraUniforms.modelMatrix));

    float4 skinned = mul(mvp, mul(skinMatrix, position));
    float4 unskinned = mul(mvp, position);
    output.svPos = (generalUniforms.skinMode == 0) ? skinned : unskinned;

    output.worldPos = position.xyz;
    output.jointsF = float4((float)input.joints.x,
                            (float)input.joints.y,
                            (float)input.joints.z,
                            (float)input.joints.w);
    output.weights = input.weights;
    return output;
}
