// Material pass 1: the thin G-buffer for visibility pixels of MaterialX materials, so SSAO, GI, screen probes,
// reflections, SSR and TAA read MaterialX's normal, albedo, roughness and metalness. Velocity and transparency stay
// the raster G-buffer's (Phase 2 still rasterises these meshes into it); emissive is cleared because pass 2's
// MaterialX colour includes emission.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "VisibilityCommon.slang"

struct PushConstants_MaterialThin
{
    float4 cameraPosition;
    uint verticesOffset;
    uint moduleIndex;
};

[[vk::push_constant]] PushConstants_MaterialThin pc;

[[vk::binding(0, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(1, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(2, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(3, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(4, 0)]] ByteAddressBuffer pe_materialBytes;
[[vk::binding(10, 0)]] ByteAddressBuffer pe_deformed;

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

[[vk::binding(0, 1)]] Texture2D pe_textures[] : register(t0, space100);
[[vk::binding(9, 0)]] SamplerState pe_samplers[18];

// Pass 1 evaluates graph inputs with lighting disabled.
#define PE_REFRACTION_TWO_SIDED false
#define PE_MATERIAL_THIN 1 // the generated fragment returns after its pass-1 captures (MaterialXGen.cpp CaptureThinInputs)

float3 pe_view_position()
{
    return pc.cameraPosition.xyz;
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
    return 0;
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
