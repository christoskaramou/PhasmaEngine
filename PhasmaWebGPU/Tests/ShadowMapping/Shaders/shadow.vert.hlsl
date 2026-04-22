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

float4 VSMain(VSInput input) : SV_Position
{
    float4 wp = mul(model.modelMatrix, float4(input.position, 1.f));
    return mul(scene.lightViewProj, wp);
}
