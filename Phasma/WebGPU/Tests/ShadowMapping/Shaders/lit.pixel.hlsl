struct Scene
{
    column_major float4x4 lightViewProj;
    column_major float4x4 cameraViewProj;
    float3 lightPos;
    float _pad0;
};

[[vk::binding(0, 0)]] cbuffer SceneCB : register(b0, space0)
{
    Scene scene;
}
[[vk::binding(1, 0)]] Texture2D<float> shadowMap : register(t1, space0);
[[vk::binding(2, 0)]] SamplerComparisonState shadowSampler : register(s2, space0);

static const float kShadowTextureSize = 1024.f;

struct PSInput
{
    float4 svPos                         : SV_Position;
    [[vk::location(0)]] float3 shadowPos : TEXCOORD0;
    [[vk::location(1)]] float3 fragPos   : TEXCOORD1;
    [[vk::location(2)]] float3 fragNorm  : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float visibility = 0.f;
    float oneOver = 1.f / kShadowTextureSize;
    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            float2 off = float2((float)x, (float)y) * oneOver;
            visibility += shadowMap.SampleCmp(shadowSampler,
                input.shadowPos.xy + off,
                input.shadowPos.z - 0.007f);
        }
    }
    visibility /= 9.f;

    float3 lightDir = normalize(scene.lightPos - input.fragPos);
    float lambert = max(dot(lightDir, normalize(input.fragNorm)), 0.f);
    float ambient = 0.2f;
    float lighting = min(ambient + visibility * lambert, 1.f);

    float3 albedo = float3(0.9f, 0.9f, 0.9f);
    return float4(lighting * albedo, 1.f);
}
