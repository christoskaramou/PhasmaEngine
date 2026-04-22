// Translated from webgpu-samples reversedZ/vertexPrecisionErrorPass.wgsl.
// Emits clip-space position twice: once as the SV_Position output and once as
// a varying so the fragment stage can recompute z/w and compare it against the
// depth value sampled from the pre-pass depth texture.

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
};

struct VSOutput
{
    float4 svPos                       : SV_Position;
    [[vk::location(0)]] float4 clipPos : TEXCOORD0;
};

VSOutput VSMain(VSInput input, uint instanceIdx : SV_InstanceID)
{
    VSOutput o;
    float4 worldPos = mul(uniforms.modelMatrix[instanceIdx], input.position);
    float4 clipPos  = mul(camera.viewProjectionMatrix, worldPos);
    o.svPos         = clipPos;
    o.clipPos       = clipPos;
    return o;
}
