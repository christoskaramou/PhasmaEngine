#ifndef REFLECTION_COMMON_H
#define REFLECTION_COMMON_H
// Lumen-style reflections (ReflectionPass): one ray per pixel (Lumen's DownsampleFactor 1); ReflectionResolve.hlsl
// regenerates any pixel's ray direction and pdf with the same functions, so neither has to be stored.
#include "ProbeVolume.hlsl"

#define REFLECTION_MAX_ROUGHNESS 0.4 // Lumen's MaxRoughnessToTrace: rougher pixels take the screen probes' radiance
#define REFLECTION_MIN_ROUGHNESS 0.04 // sampling floor, so mirror pixels still share rays with their neighbours

#ifdef REFLECTIONS
[[vk::binding(6, 2)]] Texture2D<float> lr_traceDepth;
[[vk::binding(7, 2)]] Texture2D<float4> lr_traceNormal;
[[vk::binding(8, 2)]] Texture2D<float4> lr_traceMaterial; // srm target: x occlusion, y roughness, z metallic
#endif

float2 ReflectionRandom(uint2 pixel, uint frame)
{
    uint seed = ProbeHash(pixel.x * 73856093u ^ pixel.y * 19349663u ^ frame * 83492791u);
    return float2(ProbeRandom(seed), ProbeRandom(seed * 747796405u + 2891336453u));
}

float ReflectionG1(float NoX, float alpha)
{
    float a2 = alpha * alpha;
    return 2.0 * NoX / (NoX + sqrt(a2 + (1.0 - a2) * NoX * NoX));
}

float ReflectionD(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// A GGX visible-normal sample (Heitz 2018) reflected about the normal: xyz direction, w its pdf over the sphere
// (D(H) G1(V) / (4 N.V)). Roughness is perceptual (alpha = roughness^2).
float4 ReflectionSample(float3 N, float3 V, float roughness, float2 u)
{
    const float alpha = max(roughness, REFLECTION_MIN_ROUGHNESS) * max(roughness, REFLECTION_MIN_ROUGHNESS);
    const float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    const float3 T = normalize(cross(up, N));
    const float3 B = cross(N, T);
    const float3 Ve = float3(dot(V, T), dot(V, B), max(dot(V, N), 1e-4));
    const float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    const float lengthSq = Vh.x * Vh.x + Vh.y * Vh.y;
    const float3 T1 = lengthSq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) * rsqrt(lengthSq) : float3(1, 0, 0);
    const float3 T2 = cross(Vh, T1);
    const float r = sqrt(u.x);
    const float phi = 2.0 * PI * u.y;
    const float t1 = r * cos(phi);
    const float s = 0.5 * (1.0 + Vh.z);
    const float t2 = (1.0 - s) * sqrt(max(1.0 - t1 * t1, 0.0)) + s * r * sin(phi);
    const float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    const float3 He = normalize(float3(alpha * Nh.x, alpha * Nh.y, max(Nh.z, 1e-6)));
    const float3 H = He.x * T + He.y * B + He.z * N;
    const float3 L = reflect(-V, H);
    const float pdf = ReflectionD(saturate(dot(N, H)), alpha) * ReflectionG1(Ve.z, alpha) / (4.0 * Ve.z);
    return float4(L, pdf);
}
#endif
