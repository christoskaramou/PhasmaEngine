// Radiosity accumulation->lightmap copy pass — second entry in
// webgpu-samples/cornell/radiosity.wgsl ('accumulation_to_lightmap').

#include "common.inc.hlsl"

[[vk::binding(0, 1)]] RWStructuredBuffer<uint> accumulation : register(u0, space1);

[[vk::binding(1, 1)]] [[vk::image_format("rgba16f")]]
RWTexture2DArray<float4> lightmap : register(u1, space1);

struct RadiosityUniforms
{
    float accumulation_to_lightmap_scale;
    float accumulation_buffer_scale;
    float light_width;
    float light_height;
    float3 light_center;
    float _pad;
};

[[vk::binding(2, 1)]] cbuffer RadiosityUniformsCB : register(b0, space1)
{
    RadiosityUniforms uniforms;
};

#define AccumulationToLightmapWorkgroupSizeX 16
#define AccumulationToLightmapWorkgroupSizeY 16

uint accumulation_base_index(uint2 coord, uint quad)
{
    uint w, h, layers;
    lightmap.GetDimensions(w, h, layers);
    uint2 c = min(uint2(w - 1u, h - 1u), coord);
    return 3u * (c.x + w * c.y + w * h * quad);
}

[numthreads(AccumulationToLightmapWorkgroupSizeX, AccumulationToLightmapWorkgroupSizeY, 1)]
void CSMain(uint3 invocation_id : SV_DispatchThreadID, uint3 workgroup_id : SV_GroupID)
{
    uint w, h, layers;
    lightmap.GetDimensions(w, h, layers);
    uint quad = workgroup_id.z;
    uint2 coord = invocation_id.xy;

    if (coord.x < w && coord.y < h)
    {
        uint base_idx = accumulation_base_index(coord, quad);
        float3 color = float3(
            (float)accumulation[base_idx + 0u],
            (float)accumulation[base_idx + 1u],
            (float)accumulation[base_idx + 2u]);

        lightmap[uint3(coord, quad)] = float4(color * uniforms.accumulation_to_lightmap_scale, 1.0);

        if (uniforms.accumulation_buffer_scale != 1.0)
        {
            float3 scaled = color * uniforms.accumulation_buffer_scale + 0.5;
            accumulation[base_idx + 0u] = (uint)scaled.r;
            accumulation[base_idx + 1u] = (uint)scaled.g;
            accumulation[base_idx + 2u] = (uint)scaled.b;
        }
    }
}
