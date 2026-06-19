struct SceneUniforms
{
    column_major float4x4 viewProjectionMatrix;
};

struct ModelUniforms
{
    column_major float4x4 modelMatrix;
};

[[vk::binding(0, 0)]] cbuffer SceneCB : register(b0, space0)
{
    SceneUniforms scene;
}

[[vk::binding(0, 1)]] cbuffer ModelCB : register(b0, space1)
{
    ModelUniforms model;
}

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 svPos                       : SV_Position;
    [[vk::location(0)]] float3 normal  : TEXCOORD0;
    [[vk::location(1)]] float2 uv      : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 worldPos = mul(model.modelMatrix, float4(input.position, 1.0f));
    o.svPos = mul(scene.viewProjectionMatrix, worldPos);
    float3 worldNormal = mul(model.modelMatrix, float4(input.normal, 0.0f)).xyz;
    o.normal = normalize(worldNormal);
    o.uv = input.uv;
    return o;
}
