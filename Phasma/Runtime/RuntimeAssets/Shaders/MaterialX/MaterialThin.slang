// Material pass 1: the thin G-buffer for visibility pixels of MaterialX materials, so SSAO, GI, screen probes,
// reflections, SSR and TAA read MaterialX's normal, albedo, roughness and metalness. Velocity and transparency stay
// the raster G-buffer's (Phase 2 still rasterises these meshes into it); emissive is cleared because pass 2's
// MaterialX colour includes emission.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "VisibilityCommon.slang"

struct PushConstants_MaterialThin
{
    uint verticesOffset;
};

[[vk::push_constant]] PushConstants_MaterialThin pc;

[[vk::binding(0, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(1, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(2, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(3, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(4, 0)]] ByteAddressBuffer pe_materialBytes;

#ifdef PE_NORMAL_RGBA32F
[[vk::image_format("rgba32f")]][[vk::binding(5, 0)]] RWTexture2D<float4> pe_normalOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(5, 0)]] RWTexture2D<float4> pe_normalOut;
#endif
[[vk::image_format("rgba8")]][[vk::binding(6, 0)]] RWTexture2D<float4> pe_albedoOut;
[[vk::image_format("rgba8")]][[vk::binding(7, 0)]] RWTexture2D<float4> pe_srmOut;
#ifdef PE_EMISSIVE_RGBA8
[[vk::image_format("rgba8")]][[vk::binding(8, 0)]] RWTexture2D<float4> pe_emissiveOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(8, 0)]] RWTexture2D<float4> pe_emissiveOut;
#endif

[[vk::binding(0, 1)]] Texture2D pe_textures[];
[[vk::binding(1, 1)]] SamplerState pe_samplers[];

// Pass 1 reads inputs only; the module's shading hooks are never called here.
#define PE_REFRACTION_TWO_SIDED false

float3 pe_view_position()
{
    return float3(0.0, 0.0, 0.0);
}

uint pe_light_count()
{
    return 0u;
}

void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)
{
    L = float3(0.0, 0.0, 1.0);
    radiance = float3(0.0, 0.0, 0.0);
    occlusion = 1.0;
}

float3 pe_env_lookup(float3 dir, float lod)
{
    return float3(0.0, 0.0, 0.0);
}

int pe_env_samples()
{
    return 32;
}

float pe_env_max_mip()
{
    return 1.0;
}

float pe_env_lod(float3 dir, float pdf, float maxMip, int samples)
{
    return 0.0;
}

bool pe_traced_reflection(out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    return false;
}

float3 pe_env_irradiance(float3 N)
{
    return float3(0.0, 0.0, 0.0);
}

#include "Generated/PeOpenPBRDefault.slang"

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    const uint2 pixel = tid.xy >> 1;
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane;

    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    if (any(pixel >= size))
        return;

    const uint4 vis = pe_visibility[pixel];
    if (vis.x == 0u || (pe_constants[vis.x - 1u].editorFlags & PE_EDITOR_FLAG_MATERIALX) == 0u)
        return;

    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_constants, pc.verticesOffset, vis);
    const PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));

    pe_load_material(pe_materialBytes, pe_constants[vis.x - 1u].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    const PeThin thin = pe_material_thin(sp);

    if (!all(lane == (pixel & 1u)))
        return;

    // Never overwrite the raster G-buffer with a non-finite result: keep its value for this texel.
    if (any(isnan(thin.normal)) || any(isinf(thin.normal)) ||
        any(isnan(thin.albedo)) || any(isinf(thin.albedo)) ||
        isnan(thin.roughness) || isinf(thin.roughness) ||
        isnan(thin.metalness) || isinf(thin.metalness))
        return;

    // GBufferPS's encodings: normal * 0.5 + 0.5; albedo as the colour authored for the display (sRGB-encoded in both
    // colour modes: linear_color decodes it in lighting, the legacy pipeline lights it as is); srm = (AO, roughness,
    // metalness, transmission flag); emissive alpha 1 (not night-emissive).
    pe_normalOut[pixel] = float4(thin.normal * 0.5 + 0.5, 1.0);

    const float3 albedo = saturate(thin.albedo);
    pe_albedoOut[pixel] = float4(LinearToSrgb(albedo), 1.0);

    pe_srmOut[pixel] = float4(1.0, saturate(thin.roughness), saturate(thin.metalness), 0.0);
    pe_emissiveOut[pixel] = float4(0.0, 0.0, 0.0, 1.0);
}
