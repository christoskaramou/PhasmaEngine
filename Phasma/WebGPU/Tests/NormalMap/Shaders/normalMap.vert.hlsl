struct SpaceTransforms
{
    column_major float4x4 worldViewProjMatrix;
    column_major float4x4 worldViewMatrix;
};

struct MapInfo
{
    float3 lightPosVS;
    uint mode;
    float lightIntensity;
    float depthScale;
    float depthLayers;
    float pad0;
};

[[vk::binding(0, 0)]] cbuffer SpaceTransformsCB : register(b0, space0)
{
    SpaceTransforms spaceTransform;
}

[[vk::binding(1, 0)]] cbuffer MapInfoCB : register(b1, space0)
{
    MapInfo mapInfo;
}

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 vert_tan : TEXCOORD1;
    [[vk::location(4)]] float3 vert_bitan : TEXCOORD2;
};

struct VSOutput
{
    float4 posCS : SV_Position;
    [[vk::location(0)]] float3 posVS : TEXCOORD0;
    [[vk::location(1)]] float3 tangentVS : TEXCOORD1;
    [[vk::location(2)]] float3 bitangentVS : TEXCOORD2;
    [[vk::location(3)]] float3 normalVS : TEXCOORD3;
    [[vk::location(5)]] float2 uv : TEXCOORD5;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 pos4 = float4(input.position, 1.0f);
    o.posCS = mul(spaceTransform.worldViewProjMatrix, pos4);
    o.posVS = mul(spaceTransform.worldViewMatrix, pos4).xyz;
    o.tangentVS = mul(spaceTransform.worldViewMatrix, float4(input.vert_tan, 0.0f)).xyz;
    o.bitangentVS = mul(spaceTransform.worldViewMatrix, float4(input.vert_bitan, 0.0f)).xyz;
    o.normalVS = mul(spaceTransform.worldViewMatrix, float4(input.normal, 0.0f)).xyz;
    o.uv = input.uv;
    return o;
}
