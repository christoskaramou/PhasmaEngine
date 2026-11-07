#ifndef WEB_MATERIAL_TEXTURES_HLSL
#define WEB_MATERIAL_TEXTURES_HLSL

// Web profile (WebGPU RHI spec decision 9): WGSL has no texture arrays, so a mesh's five material textures sit at
// fixed set-2 bindings and each material binds its own group (Scene::UpdateMaterialSets). Empty slots hold default
// textures, white and a flat normal, so the helpers sample unconditionally. Every pass that binds the material set
// includes all five, which keeps their set-2 layouts equal and lets one group serve them all.
// Includers declare material_sampler first.
[[vk::binding(0, 2)]] Texture2D pe_material_base_color : register(t0, space2);
[[vk::binding(1, 2)]] Texture2D pe_material_metallic_roughness : register(t1, space2);
[[vk::binding(2, 2)]] Texture2D pe_material_normal : register(t2, space2);
[[vk::binding(3, 2)]] Texture2D pe_material_occlusion : register(t3, space2);
[[vk::binding(4, 2)]] Texture2D pe_material_emissive : register(t4, space2);

float4 GetBaseColor(uint id, float2 uv)
{
    return pe_material_base_color.Sample(material_sampler, uv);
}

float4 GetMetallicRoughness(uint id, float2 uv)
{
    return pe_material_metallic_roughness.Sample(material_sampler, uv);
}

float4 GetNormal(uint id, float2 uv)
{
    return pe_material_normal.Sample(material_sampler, uv);
}

float4 GetOcclusion(uint id, float2 uv)
{
    return pe_material_occlusion.Sample(material_sampler, uv);
}

float4 GetEmissive(uint id, float2 uv)
{
    return pe_material_emissive.Sample(material_sampler, uv);
}

#endif
