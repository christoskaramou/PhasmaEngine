struct MapInfo
{
    float3 lightPosVS;
    uint mode;
    float lightIntensity;
    float depthScale;
    float depthLayers;
    float pad0;
};

[[vk::binding(1, 0)]] cbuffer MapInfoCB : register(b1, space0)
{
    MapInfo mapInfo;
}

[[vk::binding(0, 1)]] SamplerState textureSampler : register(s0, space1);
[[vk::binding(1, 1)]] Texture2D<float4> albedoTexture : register(t1, space1);
[[vk::binding(2, 1)]] Texture2D<float4> normalTexture : register(t2, space1);
[[vk::binding(3, 1)]] Texture2D<float4> depthTexture : register(t3, space1);

static const uint modeAlbedoTexture = 0;
static const uint modeNormalTexture = 1;
static const uint modeDepthTexture = 2;
static const uint modeNormalMap = 3;
static const uint modeParallaxScale = 4;
static const uint modeSteepParallax = 5;

struct PSInput
{
    float4 posCS : SV_Position;
    [[vk::location(0)]] float3 posVS : TEXCOORD0;
    [[vk::location(1)]] float3 tangentVS : TEXCOORD1;
    [[vk::location(2)]] float3 bitangentVS : TEXCOORD2;
    [[vk::location(3)]] float3 normalVS : TEXCOORD3;
    [[vk::location(5)]] float2 uv : TEXCOORD5;
};

float2 parallaxScale(float2 uv, float3 viewDirTS)
{
    float depthSample = depthTexture.Sample(textureSampler, uv).r;
    return uv + viewDirTS.xy * (depthSample * mapInfo.depthScale) / -viewDirTS.z;
}

float2 parallaxSteep(float2 startUV, float3 viewDirTS)
{
    float2 ddxUV = ddx(startUV);
    float2 ddyUV = ddy(startUV);

    float2 uvDelta = viewDirTS.xy * mapInfo.depthScale / (-viewDirTS.z * mapInfo.depthLayers);
    float depthDelta = 1.0f / mapInfo.depthLayers;
    float3 posDelta = float3(uvDelta, depthDelta);

    float3 pos = float3(startUV, 0.0f);
    for (int i = 0; i < 32; ++i)
    {
        float sampled = depthTexture.SampleGrad(textureSampler, pos.xy, ddxUV, ddyUV).r;
        if (pos.z >= sampled)
        {
            break;
        }
        pos += posDelta;
    }

    return pos.xy;
}

float4 PSMain(PSInput input) : SV_Target0
{
    // Build tangentToView so that mul(M, v) yields tangent*v.x + bitangent*v.y + normal*v.z,
    // matching WGSL's column-major mat3x3f(tangent, bitangent, normal) * v.
    float3x3 tangentToView = float3x3(
        input.tangentVS.x, input.bitangentVS.x, input.normalVS.x,
        input.tangentVS.y, input.bitangentVS.y, input.normalVS.y,
        input.tangentVS.z, input.bitangentVS.z, input.normalVS.z);

    float3x3 viewToTangent = transpose(tangentToView);
    float3 viewDirTS = normalize(mul(viewToTangent, input.posVS));

    float2 uv;
    if (mapInfo.mode == modeParallaxScale)
    {
        uv = parallaxScale(input.uv, viewDirTS);
    }
    else if (mapInfo.mode == modeSteepParallax)
    {
        uv = parallaxSteep(input.uv, viewDirTS);
    }
    else
    {
        uv = input.uv;
    }

    float4 albedoSample = albedoTexture.Sample(textureSampler, uv);
    float4 normalSample = normalTexture.Sample(textureSampler, uv);

    if (mapInfo.mode == modeAlbedoTexture)
    {
        return albedoSample;
    }

    if (mapInfo.mode == modeNormalTexture)
    {
        return normalSample;
    }

    if (mapInfo.mode == modeDepthTexture)
    {
        return depthTexture.Sample(textureSampler, input.uv);
    }

    float3 normalTS = normalSample.xyz * 2.0f - 1.0f;
    float3 normalVS = normalize(mul(tangentToView, normalTS));

    float3 fragToLightVS = mapInfo.lightPosVS - input.posVS;
    float lightSqrDist = dot(fragToLightVS, fragToLightVS);
    float3 lightDirVS = fragToLightVS * rsqrt(lightSqrDist);
    float diffuseLight = mapInfo.lightIntensity * max(dot(lightDirVS, normalVS), 0.0f) / lightSqrDist;
    float3 diffuse = albedoSample.rgb * diffuseLight;
    return float4(diffuse, 1.0f);
}
