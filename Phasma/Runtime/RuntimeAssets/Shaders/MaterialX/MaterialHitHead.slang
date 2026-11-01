// RT hits on MaterialX materials: a shape group's generated graphs at a ray hit, under the lights, shadow rays, probes and
// sky the legacy hit shader uses (RTCommon.hlsl), through the pe_* hooks PePrelude.slang implements for material pass 2.
// Order: this head, the group's combined module, MaterialHitMain.slang; compiled per RT pass with its defines and
// PE_MATERIAL_HIT.
#include "../RayTracing/RTCommon.hlsl"
#include "VisibilityCommon.slang"

// Set 1 after rtDepth (binding 3); ProbeVolume, ScreenProbeCommon and ReflectionCommon use sets 0, 2 and 3. DX12 maps
// binding N to register N and an array to N..N+17, so the single sampler comes before the 18 graph samplers (s6-s23).
[[vk::binding(4, 1)]] ByteAddressBuffer pe_materialBytes;
[[vk::binding(5, 1)]] SamplerState pe_envSampler;
[[vk::binding(6, 1)]] SamplerState pe_samplers[18];

// OpenPBR's environment: lat-long radiance pyramid and irradiance (Skybox::BuildOpenPBREnvironment).
[[vk::binding(7, 1)]] Texture2D<float4> pe_envRadiance;
[[vk::binding(8, 1)]] Texture2D<float4> pe_envIrradiance;

// Each module's slot in its shape group, by registry index (RayTracingPass's slot table): a wave of hits can mix shapes,
// so the slot comes from the hit's record, not from a push constant.
[[vk::binding(9, 1)]] StructuredBuffer<uint> pe_shapeSlots;

#define PE_REFRACTION_TWO_SIDED false

static float3 pe_P;              // world position of the hit
static float3 pe_N;              // interpolated geometric normal
static float3 pe_spec_N;         // the graph's shading normal (geometry_normal): area lights' specular representative point
static float pe_occlusion;       // material AO, outside the graph (the legacy hit's occlusion texture)
static float pe_hit_curvature;   // the hit triangle's curvature: subsurface's fwidth ratio has no derivatives here
static float3 pe_shadow_offset;  // world offset of shadow ray origins onto the smooth surface (ShadowTerminatorOffset)
static float pe_hit_lod_base;    // RT_RayConeLodBase of the hit, without a texture's own size; -1000 samples mip 0

// Forward meshes (alpha-blend, Transmission) in the camera pass: the hit's continuation state (pe_hit_closest).
static uint pe_hit_depth;            // the hit's payload depth: rays it traces go one deeper
static float pe_hit_cone_width;      // the ray cone where it hit, carried on through the surface
static float pe_hit_cone_spread;
static bool pe_hit_refracts;         // pe_traced_refraction may trace (a forward mesh with budget left)
static bool pe_refraction_traced;    // one refraction ray per hit, whatever calls the seam
static float3 pe_refraction_radiance;

// The sky along the ray, as the legacy hit falls back to once its bounce budget is spent.
float3 PeSkyAlongRay()
{
    return skybox.SampleLevel(material_sampler, WorldRayDirection(), 0).rgb * cb_iblIntensity;
}

#ifndef GI_PROBES
// A ray on from the hit through or off its surface, as the legacy closest hit continues one: its radiance in the hit
// output's space (display-referred unless linear_color). As there, a refracted ray sees everything, a pass-through ray
// only transparent geometry in Hybrid (where the raster frame stands behind it).
float3 PeTraceContinuation(float3 dir, bool refractive)
{
    RayDesc ray;
    ray.Origin = pe_P;
    ray.Direction = dir;
    ray.TMin = 0.01;
    ray.TMax = 10000.0;

    HitPayload next = (HitPayload)0;
    next.t = -1.0;
    next.alpha = 1.0;
    next.depth = pe_hit_depth + 1u;
    next.isRefractive = refractive ? 1u : 0u;
    next.opaqueTMax = ray.TMax;
    next.coneWidth = pe_hit_cone_width;
    next.coneSpread = pe_hit_cone_spread;
    TraceRay(tlas, RAY_FLAG_NONE, refractive || cb_renderMode == 2u ? 0xFF : 0x80, 0, 0, 0, ray, next);

    return next.radiance;
}
#endif

// The mx_environment_fis override's hit seam (module ABI 9): MaterialX's refraction lobe sees the scene through one ray
// along the refracted mean direction instead of the environment, so transmission_color tints what lies behind. N faces V.
// Linear radiance; false (the environment) where the hit may not trace.
bool pe_traced_refraction(float3 N, float3 V, float ior, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
#ifdef GI_PROBES
    return false;
#else
    if (!pe_hit_refracts)
        return false;

    if (!pe_refraction_traced)
    {
        float3 dir = refract(-V, N, 1.0 / max(ior, 1e-3));
        if (!(dot(dir, dir) > 0.0))
            dir = reflect(-V, N); // total internal reflection

        const float3 traced = PeTraceContinuation(normalize(dir), true);
        pe_refraction_radiance = cb_linearColor == 0u ? SrgbToLinear(traced) : traced;
        pe_refraction_traced = true;
    }

    radiance = pe_refraction_radiance;
    return true;
#endif
}

// The LOD a graph texture samples at this hit (the mx_texture override's PE_MATERIAL_HIT branch, module ABI 9).
float pe_hit_texture_lod(uint textureIndex)
{
    uint width;
    uint height;
    pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(width, height);
    return max(pe_hit_lod_base + 0.5 * log2(float(width) * float(height)), 0.0);
}

// The hit's shape slot, or 0xffffffff (the dispatchers' zero value) for a record outside the table.
uint PeHitShape(uint constantsId)
{
    const uint module = PeModuleOf(pe_materialBytes, constants[constantsId]);
    uint count;
    uint stride;
    pe_shapeSlots.GetDimensions(count, stride);

    return module < count ? pe_shapeSlots[module] : 0xffffffffu;
}

// The viewer of a hit is the ray's origin, so the generated V is the reversed ray direction.
float3 pe_view_position()
{
    return WorldRayOrigin();
}

uint pe_light_count()
{
    return cb_numDirectionalLights + cb_numPointLights + cb_numSpotLights + cb_numAreaLights;
}

// A shadow ray's occlusion keeps its strongest channel; glass's tint moves into the light's radiance.
float PeShadowOcclusion(float3 transmittance, inout float3 radiance)
{
    const float occlusion = max(transmittance.r, max(transmittance.g, transmittance.b));
    radiance *= occlusion > 0.0 ? transmittance / occlusion : float3(0.0, 0.0, 0.0);
    return occlusion;
}

// The legacy hit's light terms (RT_DirectLight, RT_Compute{Point,Spot,Area}Light) without their BRDF: L points to the
// light, radiance excludes N.L, occlusion is the shadow ray (directional light 0 only, as the legacy hit shadows it).
void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)
{
    occlusion = 1.0;
    radiance = float3(0.0, 0.0, 0.0);
    L = pe_N;

    if (i < cb_numDirectionalLights)
    {
        const DirectionalLight light = LoadDirectionalLight(i);
        L = normalize(-RotateVectorByQuat(float3(0, 0, -1), light.rotation));
        radiance = light.color.rgb * light.color.w;
        if (i == 0)
            occlusion = PeShadowOcclusion(TraceSunShadow(P + pe_shadow_offset, L), radiance);

        return;
    }

    i -= cb_numDirectionalLights;
    if (i < cb_numPointLights)
    {
        const PointLight light = LoadPointLight(i);
        const float3 toSurface = P - light.position.xyz;
        const float dist = length(toSurface);
        const float range = light.position.w;
        if (light.color.w <= 0.0 || dist >= range)
            return;

        float attenuation = 1.0 - dist / range;
        attenuation = RT_DistanceFalloff(dist, range, attenuation * attenuation);

        L = normalize(-toSurface);
        radiance = light.color.rgb * light.color.w * attenuation;
        occlusion = PeShadowOcclusion(TraceShadowRay(P + pe_shadow_offset, L, dist), radiance);
        return;
    }

    i -= cb_numPointLights;
    if (i < cb_numSpotLights)
    {
        const SpotLight light = LoadSpotLight(i);
        const float3 toSurface = P - light.position.xyz;
        const float dist = length(toSurface);
        const float range = light.position.w;
        if (light.color.w <= 0.0 || dist > range)
            return;

        L = normalize(-toSurface);
        const float3 spotDir = normalize(RotateVectorByQuat(float3(0, 0, -1), light.rotation));
        const float theta = dot(-L, spotDir);
        const float cutoffCos = cos(radians(light.params.x));
        const float outerCutoffCos = cos(radians(light.params.x + light.params.y));
        if (theta < outerCutoffCos)
            return;

        float attenuation = 1.0 - dist / range;
        attenuation = RT_DistanceFalloff(dist, range, max(0.0, attenuation * attenuation));

        radiance = light.color.rgb * light.color.w * attenuation * smoothstep(outerCutoffCos, cutoffCos, theta);
        occlusion = PeShadowOcclusion(TraceShadowRay(P + pe_shadow_offset, L, dist), radiance);
        return;
    }

    i -= cb_numSpotLights;
    const AreaLight light = LoadAreaLight(i);
    if (light.color.w <= 0.0)
        return;

    const float3 forward = normalize(RotateVectorByQuat(float3(0, 0, -1), light.rotation));
    const float3 right = normalize(cross(forward, float3(0, 1, 0)));
    const float3 up = normalize(cross(right, forward));
    const float2 halfSize = light.size.xy * 0.5;

    // Range from the closest point of the rectangle, light from the specular representative point (Karis).
    const float3 onPlane = P - forward * dot(P - light.position.xyz, forward);
    const float2 closestUv = clamp(float2(dot(onPlane - light.position.xyz, right), dot(onPlane - light.position.xyz, up)),
                                   -halfSize,
                                   halfSize);
    const float3 closest = light.position.xyz + right * closestUv.x + up * closestUv.y;
    const float closestDist = length(closest - P);
    if (closestDist > light.position.w)
        return;

    float3 target = closest;
    const float3 R = reflect(normalize(WorldRayDirection()), pe_spec_N);
    const float denom = dot(R, forward);
    if (abs(denom) > 0.001)
    {
        const float t = dot(light.position.xyz - P, forward) / denom;
        if (t > 0.0)
        {
            const float3 hit = P + t * R;
            const float2 uv = clamp(float2(dot(hit - light.position.xyz, right), dot(hit - light.position.xyz, up)),
                                    -halfSize,
                                    halfSize);
            target = light.position.xyz + right * uv.x + up * uv.y;
        }
    }

    const float3 toTarget = target - P;
    const float distToTarget = length(toTarget);
    L = normalize(toTarget);

    const float ratio = closestDist / light.position.w;
    float attenuation = max(0.0, 1.0 - ratio * ratio);
    attenuation *= attenuation;
    const float distSq = distToTarget * distToTarget;
    attenuation /= cb_physicalFalloff != 0u ? max(distSq, 1e-4) : distSq + 1.0;

    const float outputCos = dot(-L, forward);
    if (outputCos <= 0.0)
        return;

    radiance = light.color.xyz * light.color.w * attenuation * outputCos;
    occlusion = PeShadowOcclusion(TraceAreaShadow(P + pe_shadow_offset, light.position.xyz), radiance);
}

#include "PeEnvironment.slang"

// MaterialX's irradiance is radiance-equivalent (no 1/pi): the legacy hit's probe term, with the OpenPBR sky irradiance
// filling 1 - weight. GI probe rays light their hits with full probe irradiance; the camera RT pass scales it by
// gi_intensity, as the legacy hit does.
float3 pe_env_irradiance(float3 N)
{
    float3 result = float3(0.0, 0.0, 0.0);
    float indirectWeight = 0.0;

    const float4 indirect = ProbeLighting(pe_P, N);
#ifdef GI_SHADING
    const float indirectScale = gi_intensity;
#else
    const float indirectScale = 1.0;
#endif
    if (indirect.a > 0.0 && indirectScale > 0.0)
    {
        result = indirect.rgb * (indirectScale * indirect.a / PI);
        indirectWeight = indirect.a;
    }

    if (cb_IBL != 0)
    {
        const float3 ibl = pe_envIrradiance.SampleLevel(pe_envSampler, PeIrradianceUv(N), 0).rgb;
        result += ibl * cb_iblIntensity * (1.0 - indirectWeight);
    }

    return result * pe_occlusion;
}

// A hit has no screen-space reflection data: every specular lobe integrates the sky.
bool pe_traced_reflection(out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    return false;
}

int pe_env_samples()
{
    return cb_IBL == 0 ? 0 : int(cb_envSamples);
}

float3 pe_env_lookup(float3 dir, float lod)
{
    if (cb_IBL == 0)
        return float3(0.0, 0.0, 0.0);

    return pe_envRadiance.SampleLevel(pe_envSampler, PeLatLongUv(dir), lod).rgb * cb_iblIntensity * pe_occlusion;
}
