#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"

// Pixel shader for the on-demand object-id visibility pass (spatial perception,
// "decode_camera_view", Phase 2c). It is paired with the UNCHANGED depth-prepass vertex
// shader (Depth/DepthVS.hlsl mainVS), so the rasterized clip-space depth is bit-identical
// to the depth buffer the prepass already produced. The pass binds that existing depth
// buffer with depth-test GEQUAL (reverse-Z) and depth-write OFF, so ONLY the frontmost
// visible surface survives the test and writes here -> exact, occlusion-correct per-pixel
// object id. The alpha-cut discard mirrors DepthPS exactly so cutout silhouettes match the
// depth buffer texel-for-texel.
//
// Output target is R32G32_UINT:
//   .x = draw id + 1   (0 == empty; the target clears to 0, which is format-safe unlike a
//                       0xFFFFFFFF clear. The CPU subtracts 1 to recover the Mesh_Constants
//                       draw index, then inverts it to a scene node.)
//   .y = asuint(SV_Position.z)  (NDC reverse-Z depth, so the reduction can take the nearest
//                                visible depth per object without sampling the depth buffer.)

[[vk::push_constant]] PushConstants_DepthPass pc;
[[vk::binding(0, 1)]] StructuredBuffer<Mesh_Constants> constants : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState material_sampler : register(s1, space1);
[[vk::binding(2, 1)]] Texture2D textures[] : register(t2, space1);

float4 SampleArray(float2 uv, uint index)
{
    if (index == 0xFFFFFFFF)
        return float4(1.0, 1.0, 1.0, 1.0);
    return textures[NonUniformResourceIndex(index)].Sample(material_sampler, uv);
}

float4 GetBaseColor(uint id, float2 uv)
{
    return SampleArray(uv, (uint)constants[id].meshImageIndex[0]);
}

uint2 mainPS(PS_INPUT_Position_Uv_ID input) : SV_TARGET0
{
    const uint id = input.id;

    // Exact depth-prepass alpha-cut silhouette: discard the same texels DepthPS would.
    float4 sampledBase = HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT)
                             ? GetBaseColor(id, input.uv)
                             : float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float spriteBlend = saturate(input.spriteBlend);
    if (spriteBlend > 0.0f && HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT))
        sampledBase = lerp(sampledBase, GetBaseColor(id, input.nextUv), spriteBlend);
    float alpha = sampledBase.a * input.alphaFactor;
    if (alpha < constants[id].alphaCut)
        discard;

    return uint2(id + 1u, asuint(input.position.z));
}
