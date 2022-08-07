static const int MAX_DATA_SIZE = 2048; // TODO: calculate on init
static const int MAX_NUM_JOINTS = 128;

struct PushConstants
{
    float2 projJitter;
    float2 prevProjJitter;
    uint modelIndex;
    uint meshIndex;
    uint meshJointCount;
    uint primitiveIndex;
    uint primitiveImageIndex;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]] cbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };

#define meshMatrix data[pc.meshIndex]
#define meshPreviousMatrix data[pc.meshIndex + 1]
#define meshJointMatrix(x) data[pc.meshIndex + 2 + x]

#define primitiveBaseColorFactor data[pc.primitiveIndex][0]
#define primitiveEmissiveFactor data[pc.primitiveIndex][1]
#define primitiveMetallicFactor data[pc.primitiveIndex][2][0]
#define primitiveRoughnessFactor data[pc.primitiveIndex][2][1]
#define primitiveAlphaCutoff data[pc.primitiveIndex][2][2]
#define primitiveOcclusionlMetalRoughness data[pc.primitiveIndex][2][3]
#define primitiveHasBones data[pc.primitiveIndex][3][0]

#define modelMatrix data[pc.modelIndex]
#define modelMvp data[pc.modelIndex + 1]
#define modelPreviousMvp data[pc.modelIndex + 2]

struct VS_INPUT
{
    float3 position         : POSITION;
    float2 texCoord         : TEXCOORD;
    float3 normal           : NORMAL;
    float4 color            : COLOR;
    int4 jointIndices       : BLENDINDICES;
    float4 jointWeights     : BLENDWEIGHT;
};

struct VS_OUTPUT
{
    float2 uv                   : TEXCOORD0;
    float3 normal               : NORMAL;
    float4 color                : COLOR;
    float4 baseColorFactor      : TEXCOORD1;
    float3 emissiveFactor       : TEXCOORD2;
    float4 metRoughAlphacutOcl  : TEXCOORD3;
    float4 positionCS           : POSITION0;
    float4 previousPositionCS   : POSITION1;
    float4 positionWS           : POSITION2;
    float4 position             : SV_POSITION;
};

static const float4x4 identity_mat = {  1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f};

float3x3 invert_scale3x3(float3x3 m)
{
    float isx = 1.0f / length(m[0]);
    float isy = 1.0f / length(m[1]);
    float isz = 1.0f / length(m[2]);

    return float3x3(m[0] * isx,
                    m[1] * isy,
                    m[2] * isz);
}

VS_OUTPUT mainVS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.uv = input.texCoord.xy;

    float4x4 boneTransform = identity_mat;
    if (pc.meshJointCount > 0.0)
    {
        boneTransform = mul(meshJointMatrix(input.jointIndices[0]), input.jointWeights[0]) +
                        mul(meshJointMatrix(input.jointIndices[1]), input.jointWeights[1]) +
                        mul(meshJointMatrix(input.jointIndices[2]), input.jointWeights[2]) +
                        mul(meshJointMatrix(input.jointIndices[3]), input.jointWeights[3]);
    }

    float4 inPos = float4(input.position, 1.0f);

    float3x3 mNormal = (float3x3)mul(boneTransform, mul(meshMatrix, modelMatrix));
    mNormal = invert_scale3x3(mNormal);

    // Normal in world space
    output.normal = normalize(mul(input.normal, mNormal));

    // Color
    output.color = input.color;

    // Factors
    output.baseColorFactor = primitiveBaseColorFactor;
    output.emissiveFactor = primitiveEmissiveFactor.xyz;
    output.metRoughAlphacutOcl = float4(primitiveMetallicFactor, primitiveRoughnessFactor, primitiveAlphaCutoff, primitiveOcclusionlMetalRoughness);

    output.positionWS = mul(inPos, mul(boneTransform, mul(meshMatrix, modelMatrix)));

    // Velocity
    output.positionCS = mul(inPos, mul(boneTransform, mul(meshMatrix, modelMvp)));
    output.previousPositionCS = mul(inPos, mul(boneTransform, mul(meshPreviousMatrix, modelPreviousMvp)));

    output.position = output.positionCS;

    return output;
}
