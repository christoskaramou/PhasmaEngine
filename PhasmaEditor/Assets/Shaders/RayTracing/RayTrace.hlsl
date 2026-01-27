#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"
#include "../Common/Structures.hlsl"
#include "../Gbuffer/PBR.hlsl"
#include "../Common/IBL.hlsl"

struct MeshInfoGPU
{
    uint indexOffset;
    uint vertexOffset;
    int textures[5];
    int padding;
};

struct HitPayload
{
    float3 radiance;
    float  t;
    uint   hitKind;
    uint   rayType; // 0: Primary, 1: Shadow
    uint   depth;
};

struct Vertex
{
    float3 position;
    float2 uv;
    float3 normal;
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
[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(1, 0)]] RWTexture2D<float4> output;
[[vk::binding(2, 0)]] ByteAddressBuffer data;
[[vk::binding(3, 0)]] StructuredBuffer<Mesh_Constants> constants;
[[vk::binding(4, 0)]] SamplerState material_sampler;
[[vk::binding(5, 0)]] Texture2D textures[];
[[vk::binding(6, 0)]] ByteAddressBuffer geometry;
[[vk::binding(7, 0)]] StructuredBuffer<MeshInfoGPU> meshInfos;
[[vk::binding(8, 0)]] TextureCube skybox;
[[vk::binding(9, 0)]] Texture2D LutIBL;

[[vk::binding(0, 1)]] cbuffer Lights
{
    float4 cb_camPos; // .w is unused
    DirectionalLight cb_sun;
    PointLight cb_pointLights[MAX_POINT_LIGHTS];
    SpotLight  cb_spotLights[MAX_SPOT_LIGHTS];
};

[[vk::binding(1, 1)]] cbuffer LightBuffer
{
    float4x4 ubo_invViewProj;
    float4x4 ubo_invView;
    float4x4 ubo_invProj;
    float ubo_lightsIntensity;
    float ubo_lightsRange;
    uint ubo_shadows;
    uint ubo_use_Disney_PBR;
};

static const uint MATRIX_SIZE = 64u;
static const uint MESH_DATA_SIZE = MATRIX_SIZE * 2u;

// Utilities
uint GetConstantBufferID(uint instanceID)
{
    return data.Load(256 + instanceID * 4);
}

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    
    return result;
}

float4 SampleArray(float2 uv, int index)
{
    if (index < 0)
        return float4(1.0, 1.0, 1.0, 1.0);

    return textures[NonUniformResourceIndex(index)].SampleLevel(material_sampler, uv, 0);
}

// Matrices
float4x4 GetViewProjection()                  { return LoadMatrix(0); }
float4x4 GetPreviousViewProjection()          { return LoadMatrix(64); }
float4x4 GetMeshMatrix(uint id)               { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetMeshPreviousMatrix(uint id)       { return LoadMatrix(constants[id].meshDataOffset + MATRIX_SIZE); }
float4x4 GetJointMatrix(uint id, uint index)  { return LoadMatrix(constants[id].meshDataOffset + MESH_DATA_SIZE + index * MATRIX_SIZE); }

// Factors
uint GetMeshConstantsOffset(uint id)   { return constants[id].meshDataOffset + MESH_DATA_SIZE; }
float4 GetBaseColorFactor(uint id)     { const uint offset = GetMeshConstantsOffset(id); return asfloat(data.Load4(offset + 0)); }
float3 GetEmissiveFactor(uint id)      { const uint offset = GetMeshConstantsOffset(id); return asfloat(data.Load3(offset + 16)); }
float4 GetMetRoughAlphacutOcl(uint id) { const uint offset = GetMeshConstantsOffset(id); return asfloat(data.Load4(offset + 32)); }

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
    uint offset = vertexOffset + vertexIndex * 80;
    
    Vertex v;
    v.position = asfloat(geometry.Load3(offset + 0));
    v.uv = asfloat(geometry.Load2(offset + 12));
    v.normal = asfloat(geometry.Load3(offset + 20));
    v.color = asfloat(geometry.Load4(offset + 32));

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
float3 ComputeIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float2 envBRDF)
{
    return ComputeIBL_Common(
        N, V, albedo, metallic, roughness, F0, 1.0,
        skybox, material_sampler, envBRDF
    );
}

float TraceShadowRay(float3 origin, float3 dir, float dist)
{
    if (ubo_shadows == 0)
        return 1.0;
    
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

float3 RT_DirectLight(float3 worldPos, float3 materialNormal, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion, float shadow, float3 energyCompensation)
{
    float3 lightDir = cb_sun.direction.xyz;
    float3 L        = normalize(lightDir);
    float3 H        = normalize(V + L);

    float NoV_clamped = clamp(dot(materialNormal, V), 0.001, 1.0);
    float NoL_raw = dot(materialNormal, L);
    float NoL_clamped = clamp(NoL_raw, 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    
    float intensity = cb_sun.color.w;

    float3 specularFresnel = Fresnel(F0, HoV);
    
    float NoL_sat = saturate(NoL_raw);
    
    float3 specRef;
    if (ubo_use_Disney_PBR)
        specRef = cb_sun.color.rgb * intensity * NoL_sat * shadow * CookTorranceSpecular_Disney(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);
    else
        specRef = cb_sun.color.rgb * intensity * NoL_sat * shadow * CookTorranceSpecular_GSchlick(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);
    
    specRef *= energyCompensation;
    
    float3 diffRef;
    if (ubo_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(albedo, NoV_clamped, NoL_clamped, LoH, roughness);
        diffRef = cb_sun.color.rgb * intensity * NoL_sat * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = cb_sun.color.rgb * intensity * NoL_sat * shadow * (1.0 - specularFresnel) * (albedo / PI);
    }
    
    float3 diffuseLight = diffRef * (1.0 - metallic) * occlusion;
    
    return diffuseLight + specRef;
}

float3 RT_ComputePointLight(int index, float3 worldPos, float3 materialNormal, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion, float3 energyCompensation)
{
    float3 lightPos = cb_pointLights[index].position.xyz;
    float3 lightDirFull = worldPos - lightPos;
    float dist = length(lightDirFull);
    
    if (dist > ubo_lightsRange) return 0.0;
    
    float3 L = normalize(-lightDirFull);
    float3 H = normalize(V + L);
    
    float attenuation = 1.0 - (dist / ubo_lightsRange);
    attenuation *= attenuation; // Quadratic
    
    // Shadow
    float shadow = TraceShadowRay(worldPos, L, dist);

    float NoV_clamped = clamp(dot(materialNormal, V), 0.001, 1.0);
    float NoL_raw = dot(materialNormal, L);
    float NoL_clamped = clamp(NoL_raw, 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    
    float intensity = cb_pointLights[index].color.w * ubo_lightsIntensity;

    float3 specularFresnel = Fresnel(F0, HoV);
    
    float NoL_sat = saturate(NoL_raw);
    
    float3 specRef;
    if (ubo_use_Disney_PBR)
        specRef = cb_pointLights[index].color.rgb * intensity * attenuation * NoL_sat * shadow * CookTorranceSpecular_Disney(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);
    else
        specRef = cb_pointLights[index].color.rgb * intensity * attenuation * NoL_sat * shadow * CookTorranceSpecular_GSchlick(materialNormal, H, NoL_clamped, NoV_clamped, specularFresnel, roughness);

    specRef *= energyCompensation;

    float3 diffRef;
    if (ubo_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(albedo, NoV_clamped, NoL_clamped, LoH, roughness);
        diffRef = cb_pointLights[index].color.rgb * intensity * attenuation * NoL_sat * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = cb_pointLights[index].color.rgb * intensity * attenuation * NoL_sat * shadow * (1.0 - specularFresnel) * (albedo / PI);
    }
    
    float3 diffuseLight = diffRef * (1.0 - metallic) * occlusion;
    
    return diffuseLight + specRef;
}

[shader("raygeneration")]
void raygeneration()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim   = DispatchRaysDimensions();

    // Pixel center in NDC (-1..1)
    float2 pixelCenter = (float2)launchIndex.xy + 0.5.xx;
    float2 uv          = pixelCenter / (float2)launchDim.xy;
    float2 ndc         = uv * 2.0 - 1.0;

    float4x4 invView = LoadMatrix(128); // Offset 128 is invView (64*2)
    float4x4 invProjection = LoadMatrix(192); // Offset 192 is invProjection (64*3)

    // Camera origin in world space = invView * (0,0,0,1)
    float3 originWorld = mul(float4(0, 0, 0, 1), invView).xyz;

    // Unproject an NDC point to view space.
    float4 pView = mul(float4(ndc.x, ndc.y, 1.0, 1.0), invProjection);
    pView.xyz /= pView.w;

    // View-space direction (camera is at view-space origin)
    float3 dirView = normalize(pView.xyz);

    // Transform direction to world space (no translation)
    float3 dirWorld = normalize(mul(dirView, (float3x3)invView));

    RayDesc ray;
    ray.Origin    = originWorld;
    ray.Direction = dirWorld;
    ray.TMin      = 0.001;
    ray.TMax      = 10000.0;

    // Payload init
    HitPayload payload;
    payload.radiance = 0.0.xxx;
    payload.t        = -1.0;
    payload.hitKind  = 0;
    payload.rayType  = 0;
    payload.depth    = 0;

    // (AS, RayFlags, InstanceMask, RayContributionToHitGroupIndex, MultiplierForGeomContribution, MissShaderIndex, Ray, Payload)
    TraceRay(
        tlas,
        RAY_FLAG_NONE,
        0xFF,
        0,  // hit group index base (ray type 0)
        0,  // geometry contribution multiplier
        0,  // miss shader index
        ray,
        payload
    );

    output[launchIndex.xy] = float4(payload.radiance, 1.0);
}

[shader("anyhit")]
void anyhit(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint id = InstanceID();
    uint primitiveId = PrimitiveIndex();

    uint textureMask = constants[id].textureMask;
    
    if (!HasTexture(textureMask, TEX_BASE_COLOR_BIT))
        return;

    uint3 indices = GetIndices(id, primitiveId);
    Vertex v0 = GetVertex(id, indices.x);
    Vertex v1 = GetVertex(id, indices.y);
    Vertex v2 = GetVertex(id, indices.z);

    float3 barycentricCoords = float3(1.0f - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;

    float4 baseColor = GetBaseColor(id, uv);
    if (baseColor.a < constants[id].alphaCut)
    {
        IgnoreHit();
    }
    else if (payload.rayType == 1 && baseColor.a < 1.0)
    {
        payload.radiance *= (1.0 - baseColor.a);
        IgnoreHit();
    }
}

[shader("closesthit")]
void closesthit(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint id = InstanceID(); // Linear ID matching Constants/MeshInfos
    uint primitiveId = PrimitiveIndex();
    uint3 indices = GetIndices(id, primitiveId);

    // Interpolate vertex attributes
    Vertex v0 = GetVertex(id, indices.x);
    Vertex v1 = GetVertex(id, indices.y);
    Vertex v2 = GetVertex(id, indices.z);

    float3 barycentricCoords = float3(1.0f - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    float2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;
    float3 normalObj = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    float4 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;
    
    // World Space conversion
    float3x4 objToWorld = ObjectToWorld3x4();
    float3x3 worldRotation3x3 = remove_scale3x3((float3x3)objToWorld);
    float3 normalWorld = normalize(mul(worldRotation3x3, normalObj));
    float3 positionWorld = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    // Tangent Space
    float3 tangentObj = GetTriangleTangent(v0.position, v1.position, v2.position, v0.uv, v1.uv, v2.uv);
    float3 tangentWorld = normalize(mul(worldRotation3x3, tangentObj));
    
    // GBuffer Logic Adaptation
    uint textureMask = constants[id].textureMask;
    
    // 1. Base Color
    float4 baseColorFactor = GetBaseColorFactor(id);
    float4 sampledBaseColor = HasTexture(textureMask, TEX_BASE_COLOR_BIT) ? GetBaseColor(id, uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 combinedColor = sampledBaseColor * color * baseColorFactor;

    // 2. Material Properties
    float3 mrSample = HasTexture(textureMask, TEX_METAL_ROUGH_BIT) ? GetMetallicRoughness(id, uv).xyz : float3(0.0f, 1.0f, 1.0f);
    float3 tangentNormalSample = HasTexture(textureMask, TEX_NORMAL_BIT) ? GetNormal(id, uv).xyz : float3(0.5f, 0.5f, 1.0f);
    float occlusionSample = HasTexture(textureMask, TEX_OCCLUSION_BIT) ? GetOcclusion(id, uv).r : 1.0f;
    float3 emissiveSample = HasTexture(textureMask, TEX_EMISSIVE_BIT) ? GetEmissive(id, uv).xyz : float3(0.0f, 0.0f, 0.0f);
    
    // Factors
    float4 metRoughAlphacutOcl = GetMetRoughAlphacutOcl(id);
    float metallic = saturate(metRoughAlphacutOcl.x * mrSample.z);
    float roughness = saturate(metRoughAlphacutOcl.y * mrSample.y);
    float occlusion = lerp(1.0f, occlusionSample, metRoughAlphacutOcl.w);
    float3 emissive = emissiveSample * GetEmissiveFactor(id);

    // 3. Normal Mapping
    tangentWorld = normalize(tangentWorld - dot(tangentWorld, normalWorld) * normalWorld);
    float3 bitangentWorld = cross(normalWorld, tangentWorld); 
    float3x3 TBN = float3x3(tangentWorld, bitangentWorld, normalWorld);
    float3 N = normalize(mul(tangentNormalSample * 2.0 - 1.0, TBN));

    // 4. Lighting (IBL)
    float3 V = normalize(-WorldRayDirection());
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, combinedColor.rgb, metallic);
    
    // Energy Compensation
    float NdV = clamp(dot(N, V), 0.001, 1.0); 
    float2 envBRDF = LutIBL.SampleLevel(material_sampler, float2(NdV, roughness), 0).xy;
    float E = envBRDF.x + envBRDF.y;
    float3 energyCompensation = 1.0 + F0 * (1.0 / max(E, 0.001) - 1.0);

    float3 lighting = 0.0.xxx;

    // Shadow for Sun & IBL Specular
    float3 L = normalize(cb_sun.direction.xyz);
    float sunShadow = TraceShadowRay(positionWorld, L, 10000.0);

    lighting += ComputeIBL(N, V, combinedColor.rgb, metallic, roughness, F0, envBRDF);
    lighting *= occlusion;

    // Direct Lighting
    lighting += RT_DirectLight(positionWorld, N, V, combinedColor.rgb, metallic, roughness, F0, occlusion, sunShadow, energyCompensation);
    
    // Point Lights
    for (int i = 0; i < MAX_POINT_LIGHTS; i++)
    {
         if (cb_pointLights[i].color.w > 0.0)
             lighting += RT_ComputePointLight(i, positionWorld, N, V, combinedColor.rgb, metallic, roughness, F0, occlusion, energyCompensation);
    }

    lighting += emissive;

    // Transmission & Volume
    float transmissionFactor = asfloat(data.Load(GetMeshConstantsOffset(id) + 28));  // Offset 28 (row1 .w)
    float thicknessFactor = asfloat(data.Load(GetMeshConstantsOffset(id) + 64));     // Offset 64 (row4 .x)
    float attenuationDistance = asfloat(data.Load(GetMeshConstantsOffset(id) + 68)); // Offset 68 (row4 .y)
    float ior = asfloat(data.Load(GetMeshConstantsOffset(id) + 72));                 // Offset 72 (row4 .z)
    float3 attenuationColor = asfloat(data.Load3(GetMeshConstantsOffset(id) + 80));  // Offset 80 (row5 .xyz)

    // Handle Transmission
    bool isTransmissive = transmissionFactor > 0.01f;
    
    if ((isTransmissive || combinedColor.a < 1.0f) && payload.depth < 2) // 2 Bounces
    {
        RayDesc ray;
        ray.Origin = positionWorld;
        ray.TMin   = 0.01;
        ray.TMax   = 10000.0;
        
        float3 nextDir = WorldRayDirection();
        
        if (isTransmissive)
        {
            float r = 1.0 / ior;
            if (dot(WorldRayDirection(), N) > 0.0)
            {
                r = ior / 1.0; 
                N = -N;
            }
            
            float3 refracted = refract(WorldRayDirection(), N, r);
            if (length(refracted) > 0.0)
                nextDir = normalize(refracted);
            else
                nextDir = reflect(WorldRayDirection(), N);
        }

        ray.Direction = isTransmissive ? nextDir : WorldRayDirection(); // refract or pass through

        HitPayload transPayload;
        transPayload.radiance = 0.0.xxx;
        transPayload.t        = -1.0;
        transPayload.hitKind  = 0;
        transPayload.rayType  = 0;
        transPayload.depth    = payload.depth + 1;

        TraceRay(
            tlas,
            RAY_FLAG_NONE,
            0xFF,
            0, 0, 0,
            ray,
            transPayload
        );
        
        float3 transColor = transPayload.radiance;

        // volume attenuation (Beer's Law)
        if (isTransmissive)
        {
             transColor *= combinedColor.rgb;
             float dist = transPayload.t;
             
             if (thicknessFactor > 0.0)
                 dist = thicknessFactor;
             
             if (dist > 0.0 && attenuationDistance < 10000.0) 
             {
                 float3 density = -log(attenuationColor) / attenuationDistance;
                 float3 transmission = exp(-density * dist);
                 transColor *= transmission;
             }
        }
        
        if (isTransmissive)
             lighting = lerp(lighting, transColor, transmissionFactor);
        else
             lighting = lerp(transColor, lighting, combinedColor.a);
    }

    payload.radiance = lighting;
    payload.t        = RayTCurrent();
    payload.hitKind  = HitKind();
}

[shader("miss")]
void miss(inout HitPayload payload)
{
    // Shadow ray check
    if (payload.rayType == 1)
    {
        payload.t = -1.0; // Use t to signal miss
        return;
    }

    // sky gradient based on ray direction (works even without other resources)
    float3 direction = normalize(WorldRayDirection());
    payload.radiance = skybox.SampleLevel(material_sampler, direction, 0).rgb;
    payload.t        = -1.0;
    payload.hitKind  = 0;
}
