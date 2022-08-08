static const int MAX_DATA_SIZE = 2048; // TODO: calculate on init
static const int MAX_NUM_JOINTS = 128;

struct PushConstants
{
    float4x4 mvp;
    uint meshIndex;
    uint meshJointCount;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]] cbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };

#define meshJointMatrix(x) data[pc.meshIndex + 2 + x]

static const float4x4 identity_mat = {  1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f};

struct VS_INPUT {
    float3 position     : POSITION;
    int4 jointIndices   : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
};
struct VS_OUTPUT {
    float4 position     : SV_POSITION;
};

VS_OUTPUT mainVS(VS_INPUT input)
{
    VS_OUTPUT output;

    float4x4 boneTransform = identity_mat;
    if (pc.meshJointCount > 0.0)
    {
        boneTransform = mul(meshJointMatrix(input.jointIndices[0]), input.jointWeights[0]) +
                        mul(meshJointMatrix(input.jointIndices[1]), input.jointWeights[1]) +
                        mul(meshJointMatrix(input.jointIndices[2]), input.jointWeights[2]) +
                        mul(meshJointMatrix(input.jointIndices[3]), input.jointWeights[3]);
    }

    output.position = mul(float4(input.position, 1.0), mul(boneTransform, pc.mvp));

    return output;
}
