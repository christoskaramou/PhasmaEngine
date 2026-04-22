[[vk::binding(1, 1)]] SamplerState meshSampler : register(s1, space1);
[[vk::binding(2, 1)]] Texture2D<float4> meshTexture : register(t2, space1);

struct PSInput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float3 normal : TEXCOORD0;
    [[vk::location(1)]] float2 uv     : TEXCOORD1;
};

static const float3 kLightDir = float3(1.0f, 1.0f, 1.0f);
static const float3 kDirColor = float3(1.0f, 1.0f, 1.0f);
static const float3 kAmbientColor = float3(0.05f, 0.05f, 0.05f);

float4 PSMain(PSInput input) : SV_Target0
{
    float4 textureColor = meshTexture.Sample(meshSampler, input.uv);

    float3 lightDir = normalize(kLightDir);
    float ndotl = max(dot(normalize(input.normal), lightDir), 0.0f);
    float3 lightColor = saturate(kAmbientColor + ndotl * kDirColor);

    return float4(textureColor.rgb * lightColor, textureColor.a);
}
