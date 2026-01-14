#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

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

// Textures
float4 GetBaseColor(uint id, float2 uv)          { return SampleArray(uv, constants[id].meshImageIndex[0]); }
float4 GetMetallicRoughness(uint id, float2 uv)  { return SampleArray(uv, constants[id].meshImageIndex[1]); }
float4 GetNormal(uint id, float2 uv)             { return SampleArray(uv, constants[id].meshImageIndex[2]); }
float4 GetOcclusion(uint id, float2 uv)          { return SampleArray(uv, constants[id].meshImageIndex[3]); }
float4 GetEmissive(uint id, float2 uv)           { return SampleArray(uv, constants[id].meshImageIndex[4]); }

struct HitPayload
{
    float3 radiance;
    float  t;
    uint   hitKind;
};

[shader("raygeneration")]
void raygeneration()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim   = DispatchRaysDimensions();

    // Pixel center in NDC (-1..1)
    float2 pixelCenter = (float2)launchIndex.xy + 0.5.xx;
    float2 uv          = pixelCenter / (float2)launchDim.xy;
    float2 ndc         = uv * 2.0 - 1.0;

    // Camera origin
    float4x4 invView = LoadMatrix(128); // Offset 128 is invView (64*2)
    float4x4 invProjection = LoadMatrix(192); // Offset 192 is invProjection (64*3)

    // Camera origin in world space = invView * (0,0,0,1)
    float3 originWorld = mul(float4(0, 0, 0, 1), invView).xyz;

    // Unproject an NDC point to view space.
    float4 pView = mul(float4(ndc.x, ndc.y, 1.0, 1.0), invProjection);
    pView.xyz /= pView.w;

    // View-space direction (camera is at view-space origin)
    float3 dirView = normalize(pView.xyz);

    // Transform direction to world space (no translation) (using 3x3 of invView)
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

    // Force bindings to be visible in RayGen reflection to ensure they are in the descriptor set layout
    if (launchIndex.x == 0xFFFFFFFF) 
    {
        uint dummyId = 0;
        float4 d1 = GetBaseColor(dummyId, float2(0,0)); // Uses constants, textures, sampler
        float d2 = asfloat(GetMeshConstantsOffset(dummyId)); // Uses constants
        payload.radiance += d1.rgb + d2;
    }

    // TraceRay params:
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
    // InstanceIndex() is equivalent to SV_InstanceID -> returns 0, 1, 2...
    // We map this index to our global data buffer
    uint id = GetConstantBufferID(InstanceIndex());

    // Retrieve material properties using the mapped ID
    float4 baseColor = GetBaseColorFactor(id);
    // float4 textureColor = GetBaseColor(id, ...); // If we had UVs here (need to interpolate attributes)

    payload.radiance = baseColor.rgb;
    payload.t        = RayTCurrent();
    payload.hitKind  = HitKind();
}

[shader("miss")]
void miss(inout HitPayload payload)
{
    // Simple sky gradient based on ray direction (works even without other resources)
    float3 d = normalize(WorldRayDirection());
    float t  = saturate(0.5 * (d.y + 1.0));

    float3 skyTop = float3(0.45, 0.65, 0.95);
    float3 skyBot = float3(0.05, 0.07, 0.10);

    payload.radiance = lerp(skyBot, skyTop, t);
    payload.t        = -1.0;
    payload.hitKind  = 0;
}
