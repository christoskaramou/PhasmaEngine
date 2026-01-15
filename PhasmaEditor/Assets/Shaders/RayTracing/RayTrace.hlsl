#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"
#include "../Common/Structures.hlsl"

struct MeshInfoGPU
{
    uint indexOffset;
    uint vertexOffset;
    uint positionsOffset;
    int textures[5];
};

struct HitPayload
{
    float3 radiance;
    float  t;
    uint   hitKind;
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

float4 SampleArray(float2 uv, uint index)
{
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
    // Layout of Vertex struct matches C++ (pos, uv, normal, color, joints, weights, id?? no id in struct usually)
    // 3+2+3+4+4+4 = 20 floats = 80 bytes?
    // pos(12) + uv(8) + normal(12) + color(16) + joints(16) + weights(16) = 80 bytes. Correct.
    
    v.position = asfloat(geometry.Load3(offset + 0));
    v.uv = asfloat(geometry.Load2(offset + 12));
    v.normal = asfloat(geometry.Load3(offset + 20));
    v.color = asfloat(geometry.Load4(offset + 32));
    // joints/weights ignored for static mesh shading usually, but read if needed.
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
    if (isinf(f) || isnan(f)) return float3(1,0,0);

    float3 tangent;
    tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
    tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
    tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
    
    return normalize(tangent);
}

// Simple PBR IBL
float3 ComputeIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0)
{
    float NdotV = max(dot(N, V), 0.0);
    float3 R = reflect(-V, N);

    // Fresnel Schlick
    float3 kS = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    // Irradiance (Diffuse)
    float3 irradiance = skybox.SampleLevel(material_sampler, N, 6).rgb; 
    float3 diffuse = irradiance * albedo;

    // Specular (Reflection)
    float mip = roughness * 8.0; 
    float3 prefilteredColor = skybox.SampleLevel(material_sampler, R, mip).rgb;
    float2 envBRDF = float2(1.0, 0.0); // Simplified
    float3 specular = prefilteredColor * (kS * envBRDF.x + envBRDF.y); 
    
    return kD * diffuse + specular; 
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

    // Force bindings to be visible in RayGen reflection
    if (launchIndex.x == 0xFFFFFFFF) 
    {
        uint dummyId = 0;
        float4 d1 = GetBaseColor(dummyId, float2(0,0)); // Uses constants, textures, sampler
        float d2 = asfloat(GetMeshConstantsOffset(dummyId)); // Uses constants
        // Dummy use of geometry and meshInfos
        uint3 dummyIndices = GetIndices(dummyId, 0); 
        float3 d3 = skybox.SampleLevel(material_sampler, float3(0,0,1), 0).rgb;
        payload.radiance += d1.rgb + d2 + float3(dummyIndices) * 0.00001 + d3;
    }

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
    float3x3 worldRotation3x3 = remove_scale3x3((float3x3)objToWorld); // Remove scale for normal/tangent transform
    
    //float3 normalWorld = normalize(mul(normalObj, transpose(worldRotation3x3)));
    float3 normalWorld = normalize(mul(worldRotation3x3, normalObj));
    float3 positionWorld = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    // Tangent Space
    float3 tangentObj = GetTriangleTangent(v0.position, v1.position, v2.position, v0.uv, v1.uv, v2.uv);
    //float3 tangentWorld = normalize(mul(tangentObj, transpose(worldRotation3x3)));
    float3 tangentWorld = normalize(mul(worldRotation3x3, tangentObj));
    
    // GBuffer Logic Adaptation
    uint textureMask = constants[id].textureMask;
    
    // 1. Base Color
    float4 baseColorFactor = GetBaseColorFactor(id);
    float4 sampledBaseColor = HasTexture(textureMask, TEX_BASE_COLOR_BIT) ? GetBaseColor(id, uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 combinedColor = sampledBaseColor * color * baseColorFactor;
    
    // Alpha Test
    if (combinedColor.a < constants[id].alphaCut)
    {
        // Ideally we should ignore this hit (AnyHit shader needed for full correctness), 
        // but for ClosestHit we just render it or treat as transparent. 
        // Real alpha testing requires AnyHit.
    }

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
    // Explicit TBN
    tangentWorld = normalize(tangentWorld - dot(tangentWorld, normalWorld) * normalWorld);
    float3 bitangentWorld = cross(normalWorld, tangentWorld); 
    float3x3 TBN = float3x3(tangentWorld, bitangentWorld, normalWorld);
    float3 N = normalize(mul(tangentNormalSample * 2.0 - 1.0, TBN));

    // 4. Lighting (IBL)
    float3 V = normalize(-WorldRayDirection());
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, combinedColor.rgb, metallic);

    float3 lighting = ComputeIBL(N, V, combinedColor.rgb, metallic, roughness, F0);
    lighting = lighting * occlusion + emissive;

    payload.radiance = lighting;
    payload.t        = RayTCurrent();
    payload.hitKind  = HitKind();
}

[shader("miss")]
void miss(inout HitPayload payload)
{
    // Simple sky gradient based on ray direction (works even without other resources)
    float3 d = normalize(WorldRayDirection());
    
    // Sample skybox
    payload.radiance = skybox.SampleLevel(material_sampler, d, 0).rgb;
    payload.t        = -1.0;
    payload.hitKind  = 0;
}
