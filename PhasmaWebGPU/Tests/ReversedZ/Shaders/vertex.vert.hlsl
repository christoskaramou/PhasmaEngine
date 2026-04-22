// Translated from webgpu-samples reversedZ/vertex.wgsl.
// Renders the instanced geometry with per-instance model matrix and a shared
// view-projection matrix, passing the vertex color through to the fragment
// stage.

#define NUM_INSTANCES 5

struct Uniforms
{
    column_major float4x4 modelMatrix[NUM_INSTANCES];
};

struct Camera
{
    column_major float4x4 viewProjectionMatrix;
};

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0)
{
    Uniforms uniforms;
}

[[vk::binding(1, 0)]] cbuffer CameraCB : register(b1, space0)
{
    Camera camera;
}

struct VSInput
{
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float4 color    : COLOR0;
};

struct VSOutput
{
    float4 svPos                          : SV_Position;
    [[vk::location(0)]] float4 fragColor  : TEXCOORD0;
};

VSOutput VSMain(VSInput input, uint instanceIdx : SV_InstanceID)
{
    VSOutput o;
    float4 worldPos = mul(uniforms.modelMatrix[instanceIdx], input.position);
    o.svPos         = mul(camera.viewProjectionMatrix, worldPos);
    o.fragColor     = input.color;
    return o;
}
