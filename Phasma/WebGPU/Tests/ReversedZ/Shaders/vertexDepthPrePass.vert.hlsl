// Translated from webgpu-samples reversedZ/vertexDepthPrePass.wgsl.
// Position-only pass used to populate the depth texture before the
// precision-error pass runs.

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

float4 VSMain(VSInput input, uint instanceIdx : SV_InstanceID) : SV_Position
{
    float4 worldPos = mul(uniforms.modelMatrix[instanceIdx], input.position);
    return mul(camera.viewProjectionMatrix, worldPos);
}
