struct Scene
{
    column_major float4x4 lightViewProj;
    column_major float4x4 cameraViewProj;
    float3 lightPos;
    float _pad0;
};

struct Model
{
    column_major float4x4 modelMatrix;
};

[[vk::binding(0, 0)]] cbuffer SceneCB : register(b0, space0)
{
    Scene scene;
}
[[vk::binding(0, 1)]] cbuffer ModelCB : register(b0, space1)
{
    Model model;
}

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
};

struct VSOutput
{
    float4 svPos                         : SV_Position;
    [[vk::location(0)]] float3 shadowPos : TEXCOORD0;
    [[vk::location(1)]] float3 fragPos   : TEXCOORD1;
    [[vk::location(2)]] float3 fragNorm  : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 wp = mul(model.modelMatrix, float4(input.position, 1.f));

    float4 posFromLight = mul(scene.lightViewProj, wp);
    o.shadowPos = float3(
        posFromLight.x * 0.5f + 0.5f,
        posFromLight.y * 0.5f + 0.5f,
        posFromLight.z);

    o.svPos = mul(scene.cameraViewProj, wp);
    o.fragPos = o.svPos.xyz;
    o.fragNorm = input.normal;
    return o;
}
