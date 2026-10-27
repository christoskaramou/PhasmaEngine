#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"
#include "../Common/Structures.hlsl"
#include "../Gbuffer/PBR.hlsl"
#include "../Common/IBL.hlsl"

struct MeshInfoGPU
{
    uint indexOffset;
    uint vertexOffset;
    uint positionsOffset;
    uint renderType;   // 1: Opaque, 2: AlphaCut, 3: AlphaBlend, 4: Transmission
    uint constantsIndex;
    int textures[5];
};

struct HitPayload
{
    float3 radiance;
    float  t;
    uint   hitKind;
    uint   rayType; // 0: Primary, 1: Shadow
    uint   depth;
    uint   isRefractive;
    float  alpha;   // Alpha channel for blending
    float  opaqueTMax;
};

struct Vertex
{
    float3 position;
    float2 uv;
    float3 normal;
    float4 tangent;
    float4 color;
    uint4 joints;
    float4 weights;
};

// --- ByteAddressBuffer data ---
// PerFrameData                 -> 4 * sizeof(mat4)
// Constant Buffer indices      -> num of draw calls * sizeof(uint)
// for (num of draw calls)
//      MeshData               -> sizeof(mat4) * 2
//      MeshConstants          -> sizeof(mat4)
// --- ByteAddressBuffer data ---

[[vk::push_constant]] PushConstants_RayTracing pc;

// Set 0
[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
#ifdef HDR_SCENE
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> output;
#else
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> output;
#endif
[[vk::binding(2, 0)]] ByteAddressBuffer data;
[[vk::binding(3, 0)]] StructuredBuffer<Mesh_Constants> constants;
[[vk::binding(4, 0)]] SamplerState material_sampler;
[[vk::binding(6, 0)]] ByteAddressBuffer geometry;
[[vk::binding(7, 0)]] StructuredBuffer<MeshInfoGPU> meshInfos;
[[vk::binding(8, 0)]] TextureCube skybox;
[[vk::binding(9, 0)]] Texture2D LutIBL;
[[vk::binding(10, 0)]] Texture2D<float> depthBuffer;
[[vk::binding(11, 0)]] StructuredBuffer<MaterialGpuData> materialTable;
[[vk::binding(12, 0)]] Texture2D pe_textures[] : register(t0, space100);

// Set 1
[[vk::binding(0, 1)]] cbuffer LightSystemUBO
{

    uint cb_numDirectionalLights;
    uint cb_numPointLights;
    uint cb_numSpotLights;
    uint cb_numAreaLights;
    uint cb_offsetDirectionalLights;
    uint cb_offsetPointLights;
    uint cb_offsetSpotLights;
    uint cb_offsetAreaLights;
};
[[vk::binding(1, 1)]] cbuffer PassUBO
{
    float4x4 cb_invViewProj;
    float4x4 cb_invView;
    float4x4 cb_invProj;
    float4 cb_camPos;
    uint cb_shadows;
    uint cb_use_Disney_PBR;
    float cb_iblIntensity;
    uint cb_IBL;
    uint cb_renderMode;  // 0=Raster, 1=Hybrid, 2=RayTracing
    uint cb_orthographicCamera;
    uint cb_linearColor; // decode authored colours at hits
    uint cb_envSamples;  // MaterialX hits: openpbr_env_samples
    uint cb_frame;       // frame counter: TraceSunShadow's sample sequence
    float cb_sunTanHalfAngle; // tan(sun_angular_diameter / 2); 0 = hard sun shadows
};

// Set 2
[[vk::binding(2, 1)]] ByteAddressBuffer LightsBuffer;

// Primary-hit device depth for RTDepthResolvePass (full RT). Set 1 because set 0 ends with the
// unbounded texture array (binding 12), which must stay the last binding of its set.
[[vk::image_format("r32f")]][[vk::binding(3, 1)]] RWTexture2D<float> rtDepth;

// 1 when nothing blocks the ray up to dist, the any-hit's transmittance through alpha-blended surfaces, 0 when opaque
// geometry does.
float TraceVisibility(float3 origin, float3 dir, float dist)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = 0.001; // Reduced bias to avoid leaks
    ray.TMax      = dist;

    HitPayload shadowPayload;
    shadowPayload.radiance = 1.0.xxx; // Init as visible
    shadowPayload.t        = 0.0; // Init as Hit -> Miss will set to -1.0
    shadowPayload.hitKind  = 0;
    shadowPayload.rayType  = 1; // Mark as Shadow Ray

    TraceRay(
        tlas,
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        0, 0, 0,
        ray,
        shadowPayload
    );

    if (shadowPayload.t != -1.0)
        return 0.0;

    return shadowPayload.radiance.x; // Assumes monochromatic for now or use .rgb in DirectLight
}

#if defined(GI_SHADING) || defined(REFLECTIONS)
// Ray-traced shading and reflection hits trace each world probe's visibility (ProbeVolume.hlsl); probe and screen-probe
// rays keep the distance moments alone, as up to 8 more rays per hit would multiply their cost.
#define GI_TRACED_VISIBILITY 1
#endif
#if defined(GI_PROBES) || defined(GI_SHADING)
#include "../GlobalIllumination/ProbeVolume.hlsl"
#endif
#ifdef SCREEN_PROBES
#include "../GlobalIllumination/ScreenProbeCommon.hlsl"
#endif
#ifdef REFLECTIONS
#include "../GlobalIllumination/ReflectionCommon.hlsl"
#endif

// Helper functions for loading lights
DirectionalLight LoadDirectionalLight(uint index)
{
    uint offset = cb_offsetDirectionalLights + index * 48;
    DirectionalLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    light.rotation = asfloat(LightsBuffer.Load4(offset + 32));
    return light;
}

PointLight LoadPointLight(uint index)
{
    uint offset = cb_offsetPointLights + index * 32;
    PointLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    return light;
}

SpotLight LoadSpotLight(uint index)
{
    uint offset = cb_offsetSpotLights + index * 64;
    SpotLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    light.rotation = asfloat(LightsBuffer.Load4(offset + 32));
    light.params = asfloat(LightsBuffer.Load4(offset + 48));
    return light;
}

AreaLight LoadAreaLight(uint index)
{
    uint offset = cb_offsetAreaLights + index * 64;
    AreaLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    light.rotation = asfloat(LightsBuffer.Load4(offset + 32));
    light.size = asfloat(LightsBuffer.Load4(offset + 48));
    return light;
}

static const uint MATRIX_SIZE = 64u;
static const uint MESH_DATA_SIZE = MATRIX_SIZE * 2u + 16u; // +16B: NodeGpuData render-visible/flags tail

// Utilities

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;

    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));

    return result;
}

MaterialGpuData LoadMaterial(uint id)
{
    return materialTable[constants[id].materialId];
}

bool IsSelfLitMaterial(float3 emissive, MaterialGpuData mat)
{
    float emissiveStrength = max(emissive.r, max(emissive.g, emissive.b));
    return emissiveStrength > 0.001 && mat.attenuationColor.w < 0.5;
}

float SelfLitMaterialScale(float3 emissive, MaterialGpuData mat)
{
    float emissiveStrength = max(emissive.r, max(emissive.g, emissive.b));
    float baseStrength = max(mat.baseColorFactor.r, max(mat.baseColorFactor.g, mat.baseColorFactor.b));
    return clamp(emissiveStrength / max(baseStrength, 0.001), 0.0, 1.5);
}

float4 SampleArray(float2 uv, uint index)
{
    if (index == 0xFFFFFFFF)
        return float4(1.0, 1.0, 1.0, 1.0);

    return pe_textures[NonUniformResourceIndex(index)].SampleLevel(material_sampler, uv, 0);
}

// Matrices
float4x4 GetViewProjection()                  { return LoadMatrix(0); }
float4x4 GetPreviousViewProjection()          { return LoadMatrix(64); }
float4x4 GetMeshMatrix(uint id)               { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetMeshPreviousMatrix(uint id)       { return LoadMatrix(constants[id].meshDataOffset + MATRIX_SIZE); }
float4x4 GetJointMatrix(uint id, uint index)  { return LoadMatrix(constants[id].meshDataOffset + MESH_DATA_SIZE + index * MATRIX_SIZE); }

// Textures - Passing defaults matching PBR expectations
float4 GetBaseColor(uint id, float2 uv)          { return SampleArray(uv, constants[id].meshImageIndex[0]); }
float4 GetMetallicRoughness(uint id, float2 uv)  { return SampleArray(uv, constants[id].meshImageIndex[1]); } // M=0, R=1
float4 GetNormal(uint id, float2 uv)             { return SampleArray(uv, constants[id].meshImageIndex[2]); }
float4 GetOcclusion(uint id, float2 uv)          { return SampleArray(uv, constants[id].meshImageIndex[3]); }
float4 GetEmissive(uint id, float2 uv)           { return SampleArray(uv, constants[id].meshImageIndex[4]); }



uint3 GetIndices(uint meshId, uint primitiveId)
{
    uint indexOffset = meshInfos[meshId].indexOffset;
    uint offset = indexOffset + primitiveId * 3 * 4; // 3 indices * 4 bytes
    return geometry.Load3(offset);
}

Vertex GetVertex(uint meshId, uint vertexIndex)
{
    uint vertexOffset = meshInfos[meshId].vertexOffset;
    uint offset = vertexOffset + vertexIndex * 96;

    Vertex v;
    v.position = asfloat(geometry.Load3(offset + 0));
    v.uv = asfloat(geometry.Load2(offset + 12));
    v.normal = asfloat(geometry.Load3(offset + 20));
    v.tangent = asfloat(geometry.Load4(offset + 32));
    v.color = asfloat(geometry.Load4(offset + 48));
    v.joints = geometry.Load4(offset + 64);
    v.weights = asfloat(geometry.Load4(offset + 80));

    return v;
}

// Apply skeletal skinning to a vertex using joint matrices from the storage buffer.
// The meshId is used to look up per-node joint matrices via GetJointMatrix.
Vertex SkinVertex(Vertex v, uint meshId)
{
    if (pc.jointsCount == 0)
        return v; // No skeleton in scene

    float weightSum = v.weights.x + v.weights.y + v.weights.z + v.weights.w;
    // Non-skinned nodes allocate no joint tail; zero weights keep them from loading past NodeGpuData.
    if (weightSum < 0.001f)
        return v; // Not skinned

    float4x4 boneTransform = mul(GetJointMatrix(meshId, v.joints.x), v.weights.x)
                            + mul(GetJointMatrix(meshId, v.joints.y), v.weights.y)
                            + mul(GetJointMatrix(meshId, v.joints.z), v.weights.z)
                            + mul(GetJointMatrix(meshId, v.joints.w), v.weights.w);

    v.position = mul(float4(v.position, 1.0f), boneTransform).xyz;
    v.normal = normalize(mul(v.normal, (float3x3)boneTransform));

    return v;
}

// Calculate Tangent from triangle geometry and UVs
float3 GetTriangleTangent(float3 v0, float3 v1, float3 v2, float2 uv0, float2 uv1, float2 uv2)
{
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float2 deltaUV1 = uv1 - uv0;
    float2 deltaUV2 = uv2 - uv0;

    float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
    float f = 1.0f / det;
    if (isinf(f) || isnan(f))
        return float3(1,0,0);

    float3 tangent;
    tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
    tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
    tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

    return length(tangent) > 0.0f ? normalize(tangent) : float3(1, 0, 0);
}

// Improved Single-Scatter Energy Compensation (Kulla-Conty approx)
float3 ComputeIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float2 envBRDF, float diffuseWeight = 1.0)
{
    return ComputeIBL_Common(
        N, V, albedo, metallic, roughness, F0, 1.0,
        skybox, material_sampler, envBRDF, diffuseWeight
    );
}

// Shadow rays leave from the smooth surface the vertex normals describe (Hanika, "Hacking the Shadow Terminator", Ray
// Tracing Gems II, ch. 4): from the flat triangle, a point the interpolated normal still lights near the terminator
// shadows itself on the neighbouring facets, a hard faceted arc. The offset from the triangle to that surface, in the
// vertices' space; zero on flat and concave parts.
float3 ShadowTerminatorOffset(float3 p0, float3 p1, float3 p2, float3 n0, float3 n1, float3 n2, float3 barycentrics)
{
    float3 p = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
    float3 below = min(0.0, dot(p - p0, n0)) * n0 * barycentrics.x;
    below += min(0.0, dot(p - p1, n1)) * n1 * barycentrics.y;
    below += min(0.0, dot(p - p2, n2)) * n2 * barycentrics.z;
    return -below;
}

float TraceShadowRay(float3 origin, float3 dir, float dist)
{
    if (cb_shadows == 0)
        return 1.0;

    return TraceVisibility(origin, dir, dist);
}

// The sun is a disk sun_angular_diameter wide: each frame the shadow ray aims at another point of it (a per-pixel offset
// into the R2 sequence), and the passes' temporal filters (reflection history, TAA, probe averages) turn the hits into a
// penumbra. L points to the sun's centre.
float TraceSunShadow(float3 origin, float3 L)
{
    float3 dir = L;
    if (cb_sunTanHalfAngle > 0.0)
    {
        uint2 pixel = DispatchRaysIndex().xy;
        uint seed = pixel.x + 65536u * pixel.y;
        float2 offset = float2(ProbeRandom(seed), ProbeRandom(seed ^ 0x9E3779B9u));
        float2 u = frac(offset + float(cb_frame % 4096u) * float2(0.7548776662, 0.5698402910));

        float3 helper = abs(L.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3 tangent = normalize(cross(helper, L));
        float3 bitangent = cross(L, tangent);
        float radius = sqrt(u.x) * cb_sunTanHalfAngle;
        float angle = 6.28318530718 * u.y;
        dir = normalize(L + (tangent * cos(angle) + bitangent * sin(angle)) * radius);
    }

    return TraceShadowRay(origin, dir, 10000.0);
}

float3 RT_DirectLight(DirectionalLight light, float3 worldPos, float3 materialNormal, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion, float shadow, float3 energyCompensation)
{
    roughness = max(roughness, 0.04);

    float3 lightDir = RotateVectorByQuat(float3(0, 0, -1), light.rotation); // Ray direction (from light)
    float3 L        = normalize(-lightDir); // To-light vector for PBR
    float3 H        = normalize(V + L);

    float NoV_clamped = clamp(dot(materialNormal, V), 0.001, 1.0);
    float NoL_raw = dot(materialNormal, L);
    float NoL_clamped = clamp(NoL_raw, 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);

    float intensity = light.color.w;

    float3 specularFresnel = Fresnel(F0, HoV);

    float NoL_sat = saturate(NoL_raw);

    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = light.color.rgb * intensity * NoL_sat * shadow * CookTorranceSpecular_Disney(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);
    else
        specRef = light.color.rgb * intensity * NoL_sat * shadow * CookTorranceSpecular_GSchlick(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);

    specRef *= energyCompensation;

    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(albedo, NoV_clamped, NoL_clamped, LoH, roughness);
        diffRef = light.color.rgb * intensity * NoL_sat * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = light.color.rgb * intensity * NoL_sat * shadow * (1.0 - specularFresnel) * (albedo / PI);
    }

    float3 diffuseLight = diffRef * (1.0 - metallic) * occlusion;

    return diffuseLight + specRef;
}

float3 RT_ComputePointLight(int index, float3 worldPos, float3 shadowOrigin, float3 materialNormal, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion, float3 energyCompensation)
{
    roughness = max(roughness, 0.04);

    PointLight light = LoadPointLight(index);
    float3 lightPos = light.position.xyz; // .xyz
    float3 lightDirFull = worldPos - lightPos;
    float dist = length(lightDirFull);
    float range = light.position.w; // radius is .w

    if (dist > range) return 0.0;

    float3 L = normalize(-lightDirFull);
    float3 H = normalize(V + L);

    float attenuation = 1.0 - (dist / range);
    attenuation *= attenuation; // Quadratic

    // Shadow
    float shadow = TraceShadowRay(shadowOrigin, L, dist);

    float NoV_clamped = clamp(dot(materialNormal, V), 0.001, 1.0);
    float NoL_raw = dot(materialNormal, L);
    float NoL_clamped = clamp(NoL_raw, 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);

    float intensity = light.color.w;

    float3 specularFresnel = Fresnel(F0, HoV);

    float NoL_sat = saturate(NoL_raw);

    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = light.color.rgb * intensity * attenuation * NoL_sat * shadow * CookTorranceSpecular_Disney(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);
    else
        specRef = light.color.rgb * intensity * attenuation * NoL_sat * shadow * CookTorranceSpecular_GSchlick(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);

    specRef *= energyCompensation;

    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(albedo, NoV_clamped, NoL_clamped, LoH, roughness);
        diffRef = light.color.rgb * intensity * attenuation * NoL_sat * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = light.color.rgb * intensity * attenuation * NoL_sat * shadow * (1.0 - specularFresnel) * (albedo / PI);
    }

    float3 diffuseLight = diffRef * (1.0 - metallic) * occlusion;

    return diffuseLight + specRef;
}

float3 RT_ComputeSpotLight(int index, float3 worldPos, float3 shadowOrigin, float3 materialNormal, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion, float3 energyCompensation)
{
    roughness = max(roughness, 0.04);

    SpotLight light = LoadSpotLight(index);
    float3 lightPos = light.position.xyz; // .xyz
    float3 lightDirFull = worldPos - lightPos;
    float dist = length(lightDirFull);
    float range = light.position.w; // range is .w

    if (dist > range) return 0.0;

    // Convert to Direction Vector using Quaternion
    float3 spotDir = normalize(RotateVectorByQuat(float3(0, 0, -1), light.rotation));
    float3 L = normalize(-lightDirFull);
    float theta = dot(-L, spotDir);

    float cutoffCos = cos(radians(light.params.x)); // angle is params.x
    float outerCutoffCos = cos(radians(light.params.x + light.params.y)); // falloff is params.y

    if (theta < outerCutoffCos) return 0.0;

    float spotIntensity = smoothstep(outerCutoffCos, cutoffCos, theta);

    float attenuation = 1.0 - (dist / range);
    attenuation *= attenuation;
    attenuation = max(0.0, attenuation);

    // Shadow
    float shadow = TraceShadowRay(shadowOrigin, L, dist);

    float intensity = light.color.w; // intensity is .w

    // PBR calc
    float3 H = normalize(V + L);
    float NoV_clamped = clamp(dot(materialNormal, V), 0.001, 1.0);
    float NoL_raw = dot(materialNormal, L);
    float NoL_clamped = clamp(NoL_raw, 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);

    float3 specularFresnel = Fresnel(F0, HoV);
    float NoL_sat = saturate(NoL_raw);

    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = light.color.rgb * intensity * attenuation * spotIntensity * NoL_sat * shadow * CookTorranceSpecular_Disney(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);
    else
        specRef = light.color.rgb * intensity * attenuation * spotIntensity * NoL_sat * shadow * CookTorranceSpecular_GSchlick(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);

    specRef *= energyCompensation;

    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(albedo, NoV_clamped, NoL_clamped, LoH, roughness);
        diffRef = light.color.rgb * intensity * attenuation * spotIntensity * NoL_sat * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = light.color.rgb * intensity * attenuation * spotIntensity * NoL_sat * shadow * (1.0 - specularFresnel) * (albedo / PI);
    }

    float3 diffuseLight = diffRef * (1.0 - metallic) * occlusion;

    return diffuseLight + specRef;
}

float3 RT_ComputeAreaLight(int index, float3 worldPos, float3 shadowOrigin, float3 materialNormal, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion, float3 energyCompensation)
{
    roughness = max(roughness, 0.04);

    AreaLight light = LoadAreaLight(index);
    float3 lightPos = light.position.xyz;
    float  range    = light.position.w;
    float width     = light.size.x;
    float height    = light.size.y;

    // Light Vectors using Quaternion
    float3 lightForward = normalize(RotateVectorByQuat(float3(0, 0, -1), light.rotation));
    float3 lightRight = normalize(cross(lightForward, float3(0, 1, 0)));
    float3 lightUp    = normalize(cross(lightRight, lightForward));

    // 2. Find closest point on area light to WorldPos for Attenuation/Range
    float3 toLight = worldPos - lightPos;
    float distPlane = dot(toLight, lightForward);
    float3 pointOnPlane = worldPos - lightForward * distPlane;
    float3 localPoint = pointOnPlane - lightPos;

    float u = dot(localPoint, lightRight);
    float v = dot(localPoint, lightUp);

    u = clamp(u, -width * 0.5, width * 0.5);
    v = clamp(v, -height * 0.5, height * 0.5);

    float3 closestPoint = lightPos + lightRight * u + lightUp * v;
    float3 L_closest_vec = closestPoint - worldPos;
    float closestDist = length(L_closest_vec);

    if (closestDist > range)
        return 0.0;

    // 3. Specular Representative Point (Karis)
    float3 N = materialNormal;
    float3 R = reflect(-V, N);

    // Intersect reflection ray with plane
    float denom = dot(R, lightForward);
    float3 targetPoint = closestPoint; // Default to closest point

    if (abs(denom) > 0.001)
    {
        float t = dot(lightPos - worldPos, lightForward) / denom;
        if (t > 0)
        {
            float3 intersectP = worldPos + t * R;
            float3 localP = intersectP - lightPos;
            float u_spec = dot(localP, lightRight);
            float v_spec = dot(localP, lightUp);

            u_spec = clamp(u_spec, -width * 0.5, width * 0.5);
            v_spec = clamp(v_spec, -height * 0.5, height * 0.5);

            targetPoint = lightPos + lightRight * u_spec + lightUp * v_spec;
        }
    }

    // Use targetPoint for lighting vector L
    float3 L_vec = targetPoint - worldPos;
    float distToTarget = length(L_vec);
    float3 L = normalize(L_vec);

    // Attenuation
    // Use closestDist for range falloff to maintain shape
    float lightDistRatio = closestDist / range;
    float attenuation = (1.0 - lightDistRatio * lightDistRatio);
    attenuation = max(0.0, attenuation);
    attenuation *= attenuation;

    // Use distToTarget for inverse square to keep intensity correct at distance
    attenuation /= (distToTarget * distToTarget + 1.0);

    float lightOutputCos = dot(-L, lightForward);
    if (lightOutputCos <= 0.0) return 0.0;

    // Shadow (using L to target point)
    float shadow = TraceShadowRay(shadowOrigin, L, distToTarget);

    float3 areaColor = light.color.xyz * light.color.w * attenuation * lightOutputCos;

    // Standard PBR ...
    float3 H = normalize(V + L);

    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);

    float3 specularFresnel  = Fresnel(F0, HoV);

    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = NoL * shadow * CookTorranceSpecular_Disney(N, H, NoL, NoV, specularFresnel, roughness);
    else
        specRef = NoL * shadow * CookTorranceSpecular_GSchlick(N, H, NoL, NoV, specularFresnel, roughness);
    specRef *= energyCompensation;

    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(albedo, NoV, NoL, LoH, roughness);
        diffRef = NoL * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = NoL * shadow * (1.0 - specularFresnel) * (albedo / PI);
    }

    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * (1.0 - metallic) * occlusion;

    return areaColor * (reflectedLight + diffuseLight);
}

// MaterialX GI-probe hits (MaterialHitMain.slang) light their surface as the legacy closest-hit does. These helpers are a copy
// of its fold and lighting (RayTrace.hlsl), which keeps its inline code so legacy shader binaries stay unchanged: change both.

// GI and reflection rays light their hits as Lumen's surface cache does: diffuse only, the specular colour folded into the
// albedo as Unreal's EnvBRDFApproxFullyRough (diffuse + 0.45 x specular colour). A sharp highlight on a mirror would
// otherwise reach the probes as fireflies.
void RT_FoldSurfaceCache(inout float3 albedo, inout float metallic, inout float roughness, inout float3 F0)
{
    albedo = albedo * (1.0 - metallic) + F0 * 0.45;
    metallic = 0.0;
    roughness = 1.0;
    F0 = 0.0.xxx;
}

// Single-scatter energy compensation from the split-sum LUT; envBRDF is its (scale, bias).
float3 RT_EnergyCompensation(float NdV, float roughness, float3 F0, out float2 envBRDF)
{
    envBRDF = LutIBL.SampleLevel(material_sampler, float2(NdV, roughness), 0).xy;
    float E = envBRDF.x + envBRDF.y;
    return 1.0 + F0 * (1.0 / max(E, 0.001) - 1.0);
}

// The hit lighting of an opaque, non-self-lit surface: probe and sky ambient, then every light with its shadow ray.
// Emission is the caller's.
float3 RT_LightOpaque(float3 P,
                      float3 shadowOrigin,
                      float3 N,
                      float3 V,
                      float3 albedo,
                      float metallic,
                      float roughness,
                      float3 F0,
                      float occlusion,
                      float2 envBRDF,
                      float3 energyCompensation)
{
    float3 lighting = 0.0.xxx;
    float diffuseIBL = 1.0;
#if defined(GI_PROBES) || defined(GI_SHADING)
    float4 indirect = ProbeLighting(P, N);
    float indirectScale = 1.0;
#ifdef GI_SHADING
    indirectScale = gi_intensity;
#endif
    if (indirect.a > 0.0 && indirectScale > 0.0)
    {
        float3 diffuseWeight = max(0.0, 1.0 - (F0 * envBRDF.x + envBRDF.y) * energyCompensation) * (1.0 - metallic);
        lighting += indirect.rgb * albedo * diffuseWeight * (indirectScale * indirect.a / PI);
        diffuseIBL = 1.0 - indirect.a;
    }
#endif
    if (cb_IBL != 0)
        lighting += ComputeIBL(N, V, albedo, metallic, roughness, F0, envBRDF, diffuseIBL) * cb_iblIntensity;
    lighting *= occlusion;

    // Direct Lighting
    // Assuming single sun for now, or loop for multiple
    DirectionalLight sun = LoadDirectionalLight(0);
    float3 sunDir = normalize(RotateVectorByQuat(float3(0, 0, -1), sun.rotation));
    float3 L = normalize(-sunDir); // Negate: direction is ray dir, L is to-light
    float sunShadow = TraceSunShadow(shadowOrigin, L);
    for (uint i = 0; i < cb_numDirectionalLights; i++)
    {
        // Only shadowing the first one for now as per previous logic (or optimize shadow tracing)
        float s = (i == 0) ? sunShadow : 1.0;
        lighting += RT_DirectLight(LoadDirectionalLight(i), P, N, V, albedo, metallic, roughness, F0, occlusion, s,
                                   energyCompensation);
    }

    // Point Lights
    for (uint p = 0; p < cb_numPointLights; p++)
    {
        if (LoadPointLight(p).color.w > 0.0) // intensity is .w
        {
            float dist = distance(P, LoadPointLight(p).position.xyz); // position is .xyz
            if (dist < LoadPointLight(p).position.w) // radius is .w
            {
                lighting += RT_ComputePointLight(p, P, shadowOrigin, N, V, albedo, metallic, roughness, F0, occlusion,
                                                 energyCompensation);
            }
        }
    }

    // Spot Lights
    for (uint j = 0; j < cb_numSpotLights; j++)
    {
        if (LoadSpotLight(j).color.w > 0.0) // intensity is .w
            lighting += RT_ComputeSpotLight(j, P, shadowOrigin, N, V, albedo, metallic, roughness, F0, occlusion,
                                            energyCompensation);
    }

    // Area Lights
    for (uint k = 0; k < cb_numAreaLights; k++)
    {
        if (LoadAreaLight(k).color.w > 0.0)
            lighting += RT_ComputeAreaLight(k, P, shadowOrigin, N, V, albedo, metallic, roughness, F0, occlusion,
                                            energyCompensation);
    }

    return lighting;
}
