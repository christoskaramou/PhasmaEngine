// Cornell shared definitions — ported 1:1 from webgpu-samples/cornell/common.wgsl
// Included by radiosity.comp.hlsl, rasterizer.vert/pixel.hlsl, raytracer.comp.hlsl.
// The tonemapper does NOT include this file (it uses its own minimal bindings).

#ifndef CORNELL_COMMON_INC_HLSL
#define CORNELL_COMMON_INC_HLSL

static const float pi = 3.14159265359;

struct Quad
{
    float4 plane;  // plane equation of the surface
    float4 right;  // u-direction plane (dot with vec4(pos,1) in [-1..1] inside)
    float4 up;     // v-direction plane
    float3 color;  // diffuse color
    float emissive; // 0 or 1
};

struct Ray
{
    float3 start;
    float3 dir;
};

#define kNoHit 0xffffffff

struct HitInfo
{
    float dist;
    uint quad;
    float3 pos;
    float2 uv;
};

struct CommonUniforms
{
    column_major float4x4 mvp;
    column_major float4x4 inv_mvp;
    uint3 seed;
    uint quad_count;
};

// group(0) binding(0): CommonUniforms
// group(0) binding(1): Quad[]
[[vk::binding(0, 0)]] cbuffer CommonUniformsCB : register(b0, space0)
{
    CommonUniforms common_uniforms;
};

[[vk::binding(1, 0)]] ByteAddressBuffer quads : register(t0, space0);

Quad load_quad(uint quadIdx)
{
    uint base = quadIdx * 64u;
    uint4 colorEmissive = quads.Load4(base + 48u);

    Quad q;
    q.plane = asfloat(quads.Load4(base + 0u));
    q.right = asfloat(quads.Load4(base + 16u));
    q.up = asfloat(quads.Load4(base + 32u));
    q.color = asfloat(colorEmissive.xyz);
    q.emissive = asfloat(colorEmissive.w);
    return q;
}

// Intersect a ray with the quad at 'quadIdx'. Return the closer of 'closest'
// and the new intersection. Matches WGSL intersect_ray_quad exactly.
HitInfo intersect_ray_quad(Ray r, uint quadIdx, HitInfo closest)
{
    Quad q = load_quad(quadIdx);
    float plane_dist = dot(q.plane, float4(r.start, 1.0));
    float ray_dist = plane_dist / -dot(q.plane.xyz, r.dir);
    float3 pos = r.start + r.dir * ray_dist;
    float2 uv = float2(dot(float4(pos, 1.0), q.right),
                       dot(float4(pos, 1.0), q.up)) * 0.5 + 0.5;
    bool hit = (plane_dist > 0.0) &&
               (ray_dist > 0.0) &&
               (ray_dist < closest.dist) &&
               all(uv > float2(0, 0)) && all(uv < float2(1, 1));

    HitInfo outHit;
    outHit.dist = hit ? ray_dist : closest.dist;
    outHit.quad = hit ? quadIdx : closest.quad;
    outHit.pos = hit ? pos : closest.pos;
    outHit.uv = hit ? uv : closest.uv;
    return outHit;
}

HitInfo raytrace(Ray ray)
{
    HitInfo hit;
    hit.dist = 1e20;
    hit.quad = kNoHit;
    hit.pos = float3(0, 0, 0);
    hit.uv = float2(0, 0);

    for (uint q = 0u; q < common_uniforms.quad_count; q++)
    {
        hit = intersect_ray_quad(ray, q, hit);
    }
    return hit;
}

// PCG-ish RNG — state is local to each invocation.
static uint3 g_rnd;

void init_rand(uint3 invocation_id)
{
    const uint3 A = uint3(1741651u * 1009u,
                          140893u * 1609u * 13u,
                          6521u * 983u * 7u * 2u);
    g_rnd = (invocation_id * A) ^ common_uniforms.seed;
}

float rand()
{
    const uint3 C = uint3(60493u * 9377u,
                          11279u * 2539u * 23u,
                          7919u * 631u * 5u * 3u);
    g_rnd = (g_rnd * C) ^ (g_rnd.yzx >> uint3(4u, 4u, 4u));
    return float(g_rnd.x ^ g_rnd.y) / float(0xffffffffu);
}

float3 rand_unit_sphere()
{
    float u = rand();
    float v = rand();
    float theta = u * 2.0 * pi;
    float phi = acos(2.0 * v - 1.0);
    float r = pow(rand(), 1.0 / 3.0);
    float sin_theta = sin(theta);
    float cos_theta = cos(theta);
    float sin_phi = sin(phi);
    float cos_phi = cos(phi);
    return float3(r * sin_phi * sin_theta,
                  r * sin_phi * cos_theta,
                  r * cos_phi);
}

float2 rand_concentric_disk()
{
    float2 u = float2(rand(), rand());
    float2 uOffset = 2.0 * u - float2(1, 1);

    if (uOffset.x == 0.0 && uOffset.y == 0.0)
        return float2(0, 0);

    float theta = 0.0;
    float r = 0.0;
    if (abs(uOffset.x) > abs(uOffset.y))
    {
        r = uOffset.x;
        theta = (pi / 4.0) * (uOffset.y / uOffset.x);
    }
    else
    {
        r = uOffset.y;
        theta = (pi / 2.0) - (pi / 4.0) * (uOffset.x / uOffset.y);
    }
    return r * float2(cos(theta), sin(theta));
}

float3 rand_cosine_weighted_hemisphere()
{
    float2 d = rand_concentric_disk();
    float z = sqrt(max(0.0, 1.0 - d.x * d.x - d.y * d.y));
    return float3(d.x, d.y, z);
}

#endif // CORNELL_COMMON_INC_HLSL
