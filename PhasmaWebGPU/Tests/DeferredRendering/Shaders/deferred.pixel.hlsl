[[vk::binding(0, 0)]] Texture2D<float4> gBufferNormal : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> gBufferAlbedo : register(t1, space0);
[[vk::binding(2, 0)]] Texture2D<float>  gBufferDepth  : register(t2, space0);

struct LightData
{
    float4 position;
    float3 color;
    float  radius;
};

[[vk::binding(0, 1)]] StructuredBuffer<LightData> lightsBuffer : register(t0, space1);
[[vk::binding(1, 1)]] cbuffer Config : register(b0, space1)
{
    uint  numLights;
    float _dtScale;
    uint2 _configPad;
};
struct Camera
{
    column_major float4x4 viewProjectionMatrix;
    column_major float4x4 invViewProjectionMatrix;
};
[[vk::binding(2, 1)]] cbuffer CCB : register(b1, space1) { Camera camera; }

// Screen coord uses Vulkan y-down NDC since the forward pass was rendered with
// a y-flipped projection (proj[1][1]*=-1) into a positive-height viewport; the
// invViewProjectionMatrix inverts that same flipped viewProj.
float3 world_from_screen_coord(float2 coordUV, float depthSample)
{
    float4 posClip   = float4(coordUV.x * 2.0f - 1.0f,
                              coordUV.y * 2.0f - 1.0f,
                              depthSample, 1.0f);
    float4 posWorldW = mul(camera.invViewProjectionMatrix, posClip);
    return posWorldW.xyz / posWorldW.www;
}

float4 PSMain(float4 coord : SV_Position) : SV_Target0
{
    int2 pix     = int2(floor(coord.xy));
    float depth  = gBufferDepth.Load(int3(pix, 0)).x;
    if (depth >= 1.0f)
        discard;

    uint2 dim;
    gBufferDepth.GetDimensions(dim.x, dim.y);
    float2 coordUV = coord.xy / float2(dim);

    float3 position = world_from_screen_coord(coordUV, depth);
    float3 normal   = gBufferNormal.Load(int3(pix, 0)).xyz;
    float3 albedo   = gBufferAlbedo.Load(int3(pix, 0)).rgb;

    float3 result = float3(0, 0, 0);
    for (uint i = 0; i < numLights; i++)
    {
        float3 L    = lightsBuffer[i].position.xyz - position;
        float  dist = length(L);
        if (dist > lightsBuffer[i].radius)
            continue;
        float lambert = max(dot(normal, normalize(L)), 0.0f);
        float atten   = pow(1.0f - dist / lightsBuffer[i].radius, 2.0f);
        result += lambert * atten * lightsBuffer[i].color * albedo;
    }

    result += float3(0.2f, 0.2f, 0.2f);
    return float4(result, 1.0f);
}
