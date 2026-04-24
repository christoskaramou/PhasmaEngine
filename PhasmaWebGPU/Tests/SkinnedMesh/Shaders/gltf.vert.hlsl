// Port of webgpu-samples/sample/skinnedMesh/gltf.wgsl (vertex stage).
// Whale.glb vertex attributes: POSITION(f32x3) NORMAL(f32x3) TEXCOORD_0(f32x2)
// JOINTS_0(u8x4) WEIGHTS_0(f32x4). TEXCOORD_0 is not consumed by the shader so we
// skip it; four separate vertex buffers are bound in slots 0..3.

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

struct NodeUniforms
{
    column_major float4x4 worldMatrix;
};

[[vk::binding(0, 0)]] cbuffer CameraCB : register(b0, space0)
{
    CameraUniforms cameraUniforms;
}

[[vk::binding(0, 1)]] cbuffer GeneralCB : register(b0, space1)
{
    GeneralUniforms generalUniforms;
}

[[vk::binding(0, 2)]] cbuffer NodeCB : register(b0, space2)
{
    NodeUniforms nodeUniforms;
}

[[vk::binding(0, 3)]] StructuredBuffer<column_major float4x4> jointMatrices : register(t0, space3);
[[vk::binding(1, 3)]] StructuredBuffer<column_major float4x4> inverseBindMatrices : register(t1, space3);

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] uint4 joints : TEXCOORD0;
    [[vk::location(3)]] float4 weights : TEXCOORD1;
};

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float3 normal : TEXCOORD0;
    [[vk::location(1)]] float4 jointsF : TEXCOORD1;
    [[vk::location(2)]] float4 weights : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // jointMatrix[i] = bone world transform in mesh-local space (already includes
    // globalWorldInverse * bone.worldMatrix from the skin's update()).
    // inverseBindMatrices[i] = bone inverse bind pose.
    float4x4 joint0 = mul(jointMatrices[input.joints[0]], inverseBindMatrices[input.joints[0]]);
    float4x4 joint1 = mul(jointMatrices[input.joints[1]], inverseBindMatrices[input.joints[1]]);
    float4x4 joint2 = mul(jointMatrices[input.joints[2]], inverseBindMatrices[input.joints[2]]);
    float4x4 joint3 = mul(jointMatrices[input.joints[3]], inverseBindMatrices[input.joints[3]]);

    float4x4 skinMatrix =
        joint0 * input.weights[0] +
        joint1 * input.weights[1] +
        joint2 * input.weights[2] +
        joint3 * input.weights[3];

    float4 worldPosition = float4(input.position, 1.0f);

    // Upstream WGSL:
    //   skinned = model * skin * node * world
    //   rotated = model * world
    float4 skinned = mul(cameraUniforms.modelMatrix,
                         mul(skinMatrix, mul(nodeUniforms.worldMatrix, worldPosition)));
    float4 rotated = mul(cameraUniforms.modelMatrix, worldPosition);

    // general_uniforms.skin_mode == 0 means SkinMode.ON.
    float4 transformed = (generalUniforms.skinMode == 0) ? skinned : rotated;

    output.svPos = mul(cameraUniforms.projMatrix,
                       mul(cameraUniforms.viewMatrix, transformed));
    output.normal = input.normal;
    output.jointsF = float4((float)input.joints[0],
                            (float)input.joints[1],
                            (float)input.joints[2],
                            (float)input.joints[3]);
    output.weights = input.weights;
    return output;
}
