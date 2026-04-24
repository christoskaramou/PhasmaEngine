// Raytracer compute shader — ports webgpu-samples/cornell/raytracer.wgsl.

#include "common.inc.hlsl"

[[vk::binding(0, 1)]] Texture2DArray<float4> lightmap : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState smpl : register(s0, space1);

[[vk::binding(2, 1)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> framebuffer : register(u0, space1);

#define WorkgroupSizeX 16
#define WorkgroupSizeY 16

static const int NumReflectionRays = 5;

float3 sample_hit(HitInfo hit)
{
    Quad q = quads[hit.quad];
    float3 lm = lightmap.SampleLevel(smpl, float3(hit.uv, (float)hit.quad), 0).rgb;
    return lm + q.emissive * q.color;
}

[numthreads(WorkgroupSizeX, WorkgroupSizeY, 1)]
void CSMain(uint3 invocation_id : SV_DispatchThreadID)
{
    uint w, h;
    framebuffer.GetDimensions(w, h);
    if (invocation_id.x < w && invocation_id.y < h)
    {
        init_rand(invocation_id);

        float2 uv = float2(invocation_id.xy) / float2(w, h);
        float2 ndcXY = (uv - 0.5) * float2(2.0, -2.0);

        float4 nearH = mul(common_uniforms.inv_mvp, float4(ndcXY, 0.0, 1.0));
        float4 farH = mul(common_uniforms.inv_mvp, float4(ndcXY, 1.0, 1.0));
        float3 nearP = nearH.xyz / nearH.w;
        float3 farP = farH.xyz / farH.w;

        Ray ray;
        ray.start = nearP;
        ray.dir = normalize(farP - nearP);
        HitInfo hit = raytrace(ray);

        float3 color = float3(0, 0, 0);
        if (hit.quad != kNoHit)
        {
            float3 hit_color = sample_hit(hit);
            float3 normal = quads[hit.quad].plane.xyz;
            float3 bounce = reflect(ray.dir, normal);

            float3 reflection = float3(0, 0, 0);
            [loop]
            for (int i = 0; i < NumReflectionRays; i++)
            {
                float3 reflection_dir = normalize(bounce + rand_unit_sphere() * 0.1);
                Ray reflection_ray;
                reflection_ray.start = hit.pos + bounce * 1e-5;
                reflection_ray.dir = reflection_dir;
                HitInfo reflection_hit = raytrace(reflection_ray);
                if (reflection_hit.quad != kNoHit)
                    reflection += sample_hit(reflection_hit);
            }
            color = lerp(reflection / float(NumReflectionRays), hit_color, 0.95);
        }

        framebuffer[invocation_id.xy] = float4(color, 1.0);
    }
}
