#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "VisibilityCommon.slang"

[[vk::push_constant]] PushConstants_OpenPBRMask pc;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;
[[vk::binding(4, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(5, 0)]] ByteAddressBuffer pe_materialBytes;
[[vk::binding(6, 0)]] SamplerState pe_samplers[18];
[[vk::binding(0, 1)]] Texture2D pe_textures[] : register(t0, space100);

// Compile the shared input evaluator without the surface closure; -O3 removes unrelated inputs.
#define PE_MATERIAL_THIN 1
#define PE_REFRACTION_TWO_SIDED false

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
