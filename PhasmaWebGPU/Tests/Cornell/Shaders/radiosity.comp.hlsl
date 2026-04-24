// Radiosity photon-tracing compute pass — ports webgpu-samples/cornell/radiosity.wgsl
// (the 'radiosity' entry point). Pairs with radiosityToLightmap.comp.hlsl.

#include "common.inc.hlsl"

// group(1) binding(0): accumulation buffer (atomic u32 array, 3 per texel+quad)
// group(1) binding(1): lightmap 2D array storage texture (rgba16f, write-only)
// group(1) binding(2): Uniforms
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

// Upstream uses `override PhotonsPerWorkgroup` — we bake kPhotonsPerWorkgroup = 256
// on the C++ side to match radiosity.ts default (kPhotonsPerWorkgroup = 256).
#define PhotonsPerWorkgroup 256
// Maximum value added to 'accumulation' buffer per photon; matches radiosity.ts
// kPhotonEnergy = 100000.
#define PhotonEnergy 100000.0

static const int PhotonBounces = 4;
static const float LightAbsorbtion = 0.5;

uint accumulation_base_index(uint2 coord, uint quad)
{
    uint w, h, layers;
    lightmap.GetDimensions(w, h, layers);
    uint2 c = min(uint2(w - 1u, h - 1u), coord);
    return 3u * (c.x + w * c.y + w * h * quad);
}

void accumulate(float2 uv, uint quad, float3 color)
{
    uint w, h, layers;
    lightmap.GetDimensions(w, h, layers);
    uint2 coord = uint2(uv * float2(w, h));
    uint base_idx = accumulation_base_index(coord, quad);
    uint dummy;
    InterlockedAdd(accumulation[base_idx + 0u], (uint)(color.r + 0.5), dummy);
    InterlockedAdd(accumulation[base_idx + 1u], (uint)(color.g + 0.5), dummy);
    InterlockedAdd(accumulation[base_idx + 2u], (uint)(color.b + 0.5), dummy);
}

Ray new_light_ray()
{
    float3 center = uniforms.light_center;
    float3 pos = center + float3(uniforms.light_width * (rand() - 0.5),
                                 0.0,
                                 uniforms.light_height * (rand() - 0.5));
    float3 d = rand_cosine_weighted_hemisphere();
    // Upstream: var dir = rand_cosine_weighted_hemisphere().xzy; dir.y = -dir.y;
    float3 dir = float3(d.x, -d.z, d.y);
    Ray r;
    r.start = pos;
    r.dir = dir;
    return r;
}

void photon()
{
    Ray ray = new_light_ray();
    float3 color = PhotonEnergy * float3(1.0, 0.8, 0.6);

    [loop]
    for (int i = 0; i < (PhotonBounces + 1); i++)
    {
        HitInfo hit = raytrace(ray);
        if (hit.quad == kNoHit)
            break;
        Quad q = quads[hit.quad];

        ray.start = hit.pos + q.plane.xyz * 1e-5;
        ray.dir = normalize(reflect(ray.dir, q.plane.xyz) + rand_unit_sphere() * 0.75);

        color *= q.color;
        accumulate(hit.uv, hit.quad, color * LightAbsorbtion);
        color *= 1.0 - LightAbsorbtion;
    }
}

[numthreads(PhotonsPerWorkgroup, 1, 1)]
void CSMain(uint3 invocation_id : SV_DispatchThreadID)
{
    init_rand(invocation_id);
    photon();
}
