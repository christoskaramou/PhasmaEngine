#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/DeformedGeometry.hlsl"

struct PushConstants_Skinning
{
    uint verticesOffset;
    uint jobsOffset;
    uint jobBase;
    uint frustumCulling;
    float4 frustumPlanes[6]; // the camera cull's planes: skins it drops are drawn nowhere that reads this buffer
};

[[vk::push_constant]] PushConstants_Skinning pc;
[[vk::binding(0, 0)]] ByteAddressBuffer geometry;
[[vk::binding(1, 0)]] ByteAddressBuffer frameData;
[[vk::binding(2, 0)]] StructuredBuffer<Mesh_Constants> constants;
[[vk::binding(3, 0)]] RWByteAddressBuffer deformed;

float4x4 PeSkinMatrix(uint offset)
{
    float4x4 matrix;
    matrix[0] = asfloat(frameData.Load4(offset));
    matrix[1] = asfloat(frameData.Load4(offset + 16u));
    matrix[2] = asfloat(frameData.Load4(offset + 32u));
    matrix[3] = asfloat(frameData.Load4(offset + 48u));
    return matrix;
}

float4x4 PeSkinJoint(uint offset, uint joint, uint count)
{
    if (joint >= count)
        return identity_mat;

    return PeSkinMatrix(offset + joint * 64u);
}

float4x4 PeSkinBlend(uint offset, uint4 joints, float4 weights, uint count)
{
    precise float4x4 blend = mul(PeSkinJoint(offset, joints.x, count), weights.x) +
                            mul(PeSkinJoint(offset, joints.y, count), weights.y) +
                            mul(PeSkinJoint(offset, joints.z, count), weights.z) +
                            mul(PeSkinJoint(offset, joints.w, count), weights.w);
    return blend;
}

PeDeformedVertex PeSkinVertex(uint source, uint node)
{
    const uint o = pc.verticesOffset + source * 96u;
    const float4 position = float4(asfloat(geometry.Load3(o)), 1.0);
    const uint4 joints = geometry.Load4(o + 64u);
    const float4 weights = asfloat(geometry.Load4(o + 80u));
    const uint count = frameData.Load(node + 136u);
    float4x4 joint = identity_mat;
    float4x4 previousJoint = identity_mat;
    const float weightSum = weights.x + weights.y + weights.z + weights.w;
    if (joints.w != SPRITE_FRAME_BLEND_MARKER && weightSum > 0.0)
    {
        joint = PeSkinBlend(node + 144u, joints, weights, count);
        previousJoint = PeSkinBlend(node + 144u + count * 64u, joints, weights, count);
    }

    const float4x4 world = mul(joint, PeSkinMatrix(node));
    // The depth and EQUAL-tested G-buffer read this same value; keep DepthVS's association.
    precise float4 clip = mul(position, mul(world, PeSkinMatrix(0u)));
    PeDeformedVertex vertex;
    vertex.clip = clip;
    vertex.previousClip = mul(position, mul(previousJoint, mul(PeSkinMatrix(node + 64u), PeSkinMatrix(64u))));
    vertex.position = mul(position, world).xyz;
    const float3x3 rotation = remove_scale3x3((float3x3)world);
    vertex.normal = normalize(mul(asfloat(geometry.Load3(o + 20u)), rotation));
    const float4 tangent = asfloat(geometry.Load4(o + 32u));
    const float3 transformedTangent = mul(tangent.xyz, rotation);
    const float length2 = dot(transformedTangent, transformedTangent);
    const float3 direction = length2 > 1e-20 ? transformedTangent * rsqrt(length2) : float3(0.0, 0.0, 0.0);
    vertex.tangent = float4(direction, tangent.w);
    return vertex;
}

// Same test as CullingCS, so every skin the camera passes draw is still written.
bool PeSkinInFrustum(float3 aabbMin, float3 aabbMax)
{
    for (int i = 0; i < 6; i++)
    {
        float3 normal = pc.frustumPlanes[i].xyz;
        float d = pc.frustumPlanes[i].w;
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 halfSize = (aabbMax - aabbMin) * 0.5;
        float dist = dot(normal, center) + d;
        float radius = dot(abs(normal), halfSize);
        if (dist < -radius)
            return false;
    }
    return true;
}

bool PeSkinDrawn(Mesh_Constants mesh, uint node, uint dataBytes)
{
    const uint2 visibility = frameData.Load2(node + 128u);
    if (visibility.x == 0u)
        return false;

    // Generated-mask skins also feed the masked shadow replays, which every cascade draws.
    const bool skinBounds = visibility.y != 0u && visibility.y <= dataBytes - 28u;
    if (pc.frustumCulling == 0u || PeUsesGeneratedMask(mesh) || !skinBounds)
        return true;

    return PeSkinInFrustum(asfloat(frameData.Load3(visibility.y)), asfloat(frameData.Load3(visibility.y + 16u)));
}

[numthreads(64, 1, 1)]
void mainCS(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint2 job = deformed.Load2(pc.jobsOffset + (pc.jobBase + group.x) * 8u);
    const uint draw = job.x;
    const uint4 header = deformed.Load4((draw + 1u) * 16u);
    const uint local = job.y + thread.x;
    if (local >= header.y)
        return;

    uint geometryBytes;
    uint dataBytes;
    uint outputBytes;
    geometry.GetDimensions(geometryBytes);
    frameData.GetDimensions(dataBytes);
    deformed.GetDimensions(outputBytes);
    const uint node = constants[draw].meshDataOffset;
    const bool geometryRange = pc.verticesOffset <= geometryBytes && geometryBytes - pc.verticesOffset >= 96u;
    const bool nodeRange = dataBytes >= 144u && node <= dataBytes - 144u;
    const bool outputRange = header.z <= outputBytes && outputBytes - header.z >= PE_DEFORMED_VERTEX_SIZE;
    if (!geometryRange || !nodeRange || !outputRange)
    {
        return;
    }

    const uint sourceLimit = (geometryBytes - pc.verticesOffset - 96u) / 96u;
    if (header.x > sourceLimit || local > sourceLimit - header.x)
        return;
    if (frameData.Load(node + 136u) > (dataBytes - node - 144u) / 128u)
        return;
    if (local > (outputBytes - header.z - PE_DEFORMED_VERTEX_SIZE) / PE_DEFORMED_VERTEX_SIZE)
        return;
    if (!PeSkinDrawn(constants[draw], node, dataBytes))
        return;

    const PeDeformedVertex vertex = PeSkinVertex(header.x + local, node);
    const uint offset = header.z + local * PE_DEFORMED_VERTEX_SIZE;
    deformed.Store4(offset, asuint(vertex.clip));
    deformed.Store4(offset + 16u, asuint(vertex.previousClip));
    deformed.Store4(offset + 32u, asuint(float4(vertex.position, 1.0)));
    deformed.Store4(offset + 48u, asuint(float4(vertex.normal, 0.0)));
    deformed.Store4(offset + 64u, asuint(vertex.tangent));
}
