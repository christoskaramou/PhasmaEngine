// PE prelude for MaterialX modules in material pass 2: the pe_* hooks the generated code calls, implemented on
// LightingPS's own data (Lighting.hlsl: light buffer, CSM, probe GI, screen probes, reflections, IBL) so a MaterialX
// pixel sees exactly the lights, shadows and environment a legacy pixel does. Include after Lighting.hlsl and
// VisibilityCommon.slang, before the generated module. The resolve sets the pe_* statics per lane.
#ifndef PE_PRELUDE
#define PE_PRELUDE

#define PE_REFRACTION_TWO_SIDED false

static float3 pe_P;      // world position of this lane's sample
static float3 pe_N;      // interpolated geometric normal (shadow bias, as LightingPS's G-buffer normal)
static uint2 pe_pixel;   // output pixel (GI screen textures)
static float pe_ssao;    // SSAO, 1 when off
static float pe_occlusion; // material AO, outside the graph

float3 pe_view_position()
{
    return cb_camPos.xyz;
}

// Directional, point, spot, then area (LightSystemUBO counts: enabled lights only). MaterialX has no area light:
// an area light is its centre point with the point-light window (the spec's representative-point limit).
uint pe_light_count()
{
    return cb_numDirectionalLights + cb_numPointLights + cb_numSpotLights + cb_numAreaLights;
}

float PeWindow(float dist, float range)
{
    float attenuation = max(0.0, 1.0 - (dist / range) * (dist / range));
    return attenuation * attenuation;
}

// L points to the light; radiance excludes N.L (MaterialX's BSDF responses include it). Mirrors LightingPS's
// DirectLight / ComputePointLight / ComputeSpotLight attenuation, and its CSM for the first directional light.
void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)
{
    occlusion = 1.0;
    if (i < cb_numDirectionalLights)
    {
        const DirectionalLight light = LoadDirectionalLight(i);
        L = -RotateVectorByQuat(float3(0, 0, -1), light.rotation);
        radiance = light.color.rgb * light.color.a;
        if (i == 0 && cb_shadows != 0)
        {
            const float viewDepth = max(0.0f, dot(P - cb_camPos.xyz, cb_camForward.xyz));
            occlusion = CalculateShadows(P, viewDepth, dot(pe_N, L), pe_N);
        }
        return;
    }

    i -= cb_numDirectionalLights;
    if (i < cb_numPointLights)
    {
        const PointLight light = LoadPointLight(i);
        const float3 toSurface = P - light.position.xyz;
        const float range = light.position.w;
        const float dist = max(0.1, length(toSurface));
        float attenuation = PeWindow(dist, range);
        if (cb_physicalPointFalloff > 0.5)
            attenuation /= max(dist * dist, 1e-4);
        L = normalize(-toSurface);
        radiance = dist > range ? float3(0.0, 0.0, 0.0) : light.color.xyz * attenuation * light.color.w;
        return;
    }

    i -= cb_numPointLights;
    if (i < cb_numSpotLights)
    {
        const SpotLight light = LoadSpotLight(i);
        const float3 toSurface = P - light.position.xyz;
        const float range = light.position.w;
        const float dist = max(0.1, length(toSurface));
        L = normalize(-toSurface);
        const float3 spotDir = normalize(RotateVectorByQuat(float3(0, 0, -1), light.rotation));
        const float theta = dot(-L, spotDir);
        const float cutoffCos = cos(radians(light.params.x));
        const float outerCutoffCos = cos(radians(light.params.x + light.params.y));
        const float cone = theta < outerCutoffCos ? 0.0 : smoothstep(outerCutoffCos, cutoffCos, theta);
        radiance = dist > range ? float3(0.0, 0.0, 0.0) : light.color.xyz * PeWindow(dist, range) * cone * light.color.w;
        return;
    }

    i -= cb_numSpotLights;
    const AreaLight light = LoadAreaLight(i);
    const float3 toSurface = P - light.position.xyz;
    const float range = light.position.w;
    const float dist = max(0.1, length(toSurface));
    L = normalize(-toSurface);
    radiance = dist > range ? float3(0.0, 0.0, 0.0) : light.color.xyz * PeWindow(dist, range) * light.color.w;
}

// Direction -> lat-long uv in PE's equirect convention (EquirectangularToCubemap.hlsl); OpenPBREnvPyramid.hlsl fills the
// radiance pyramid through the inverse, with the same exact constants. A pole direction has no azimuth and atan2(0, 0)
// differs between SPIR-V and DXIL, so it gets 0.
float2 PeLatLongUv(float3 dir)
{
    const float azimuth = (dir.x == 0.0 && dir.z == 0.0) ? 0.0 : atan2(dir.z, dir.x);

    return float2(azimuth / (2.0 * PI) + 0.5, 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI);
}

// The irradiance map's rows run from the zenith to the nadir inclusive (OpenPBREnvIrradiance.hlsl), so v = 0 and v = 1 are
// the exact poles.
float2 PeIrradianceUv(float3 dir)
{
    uint width;
    uint height;
    pe_envIrradiance.GetDimensions(width, height);

    const float2 uv = PeLatLongUv(dir);

    return float2(uv.x, (uv.y * float(height - 1u) + 0.5) / float(height));
}

// MaterialX's irradiance is radiance-equivalent (its diffuse multiplies it by the albedo, no 1/pi): LightingPS's
// probe term E * gi_intensity * weight / pi, with the cosine-convolved IBL filling 1 - weight.
float3 pe_env_irradiance(float3 N)
{
    float4 indirect = ProbeLighting(pe_P, N);
    if (cb_screenProbes != 0)
    {
        uint width, height;
        gi_screenIrradiance.GetDimensions(width, height);

        const int2 texel = int2((float2(pe_pixel) + 0.5) * float2(width, height) / pc.framebufferSize);
        const float4 screen = gi_screenIrradiance.Load(int3(texel, 0));
        const float weight = lerp(indirect.a, 1.0, screen.a);
        indirect.rgb = weight > 0.0 ? lerp(indirect.rgb * indirect.a, screen.rgb, screen.a) / weight : 0.0.xxx;
        indirect.a = weight;
    }

    const float indirectWeight = gi_intensity > 0.0 ? indirect.a : 0.0;
    float3 result = indirect.rgb * (gi_intensity * indirectWeight / PI) * (cb_screenProbes != 0 ? 1.0 : pe_ssao);

    if (cb_IBL != 0)
    {
        const float3 ibl = pe_envIrradiance.SampleLevel(pe_envSampler, PeIrradianceUv(N), 0).rgb;
        result += ibl * cb_IBL_intensity * (1.0 - indirectWeight) * pe_ssao;
    }

    return result * pe_occlusion;
}

// Reflections replace every lobe at the G-buffer roughness pass 1 wrote, as in the prefilter override.
bool pe_traced_reflection(out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    if (cb_reflections != 0)
    {
        uint width, height;
        gi_reflections.GetDimensions(width, height);

        const int2 texel = int2((float2(pe_pixel) + 0.5) * float2(width, height) / pc.framebufferSize);
        const float4 reflection = gi_reflections.Load(int3(texel, 0));
        if (reflection.a > 0.0)
        {
            radiance = reflection.rgb * pe_ssao * pe_occlusion;
            return true;
        }
    }

    return false;
}

// 0 skips the integral (and its cost) when there is no environment to integrate.
int pe_env_samples()
{
    return cb_IBL == 0 ? 0 : int(pc.env_samples);
}

float pe_env_max_mip()
{
    uint width;
    uint height;
    uint levels;
    pe_envRadiance.GetDimensions(0, width, height, levels);
    return float(levels - 1u);
}

// MaterialX's own mx_latlong_compute_lod (lib/mx_microfacet_specular.glsl), on a lat-long pyramid built like its mipmaps.
float pe_env_lod(float3 dir, float pdf, float maxMip, int samples)
{
    const float mipLevelOffset = 1.5;
    const float distortion = sqrt(1.0 - dir.y * dir.y);

    return max(maxMip - mipLevelOffset - 0.5 * log2(float(samples) * pdf * distortion), 0.0);
}

float3 pe_env_lookup(float3 dir, float lod)
{
    if (cb_IBL == 0)
        return float3(0.0, 0.0, 0.0);

    return pe_envRadiance.SampleLevel(pe_envSampler, PeLatLongUv(dir), lod).rgb *
           cb_IBL_intensity * pe_ssao * pe_occlusion;
}

#endif
