RaytracingAccelerationStructure g_tlas : register(t0);
RWTexture2D<float4> g_output : register(u1);

struct SceneData
{
    float4x4 viewProjection;
    float4x4 previousViewProjection;
    float4x4 invView;
    float4x4 invProjection;
};
StructuredBuffer<SceneData> g_scene : register(t2);

struct HitPayload
{
    float4 color;
};

[shader("raygeneration")]
void main()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    const float2 pixelCenter = float2(launchIndex.xy) + float2(0.5, 0.5);
    const float2 inUV = pixelCenter / float2(launchDim.xy);
    float2 d = inUV * 2.0 - 1.0;

    float4 origin = mul(g_scene[0].invView, float4(0, 0, 0, 1));
    float4 target = mul(g_scene[0].invProjection, float4(d.x, d.y, 1, 1));
    float4 direction = mul(g_scene[0].invView, float4(normalize(target.xyz), 0));

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    HitPayload payload;
    payload.color = float4(0, 0, 0, 0);

    TraceRay(
        g_tlas, 
        RAY_FLAG_NONE, 
        0xFF, 
        0, 
        0, 
        0, 
        ray, 
        payload);

    g_output[launchIndex.xy] = payload.color;
}
