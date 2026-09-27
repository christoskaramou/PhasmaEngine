// Minimal fullscreen pass for the native script ABI's AddFullscreenPass (v12): reads the scene
// depth and normal targets and blends a params-tinted overlay onto the viewport. Exercised by
// tools/tests/native-scripts (ApiProbe bits 27-28); editor_smoke.py copies it into the project
// Assets, where the engine's Shaders/Common includes do not resolve, so it declares its own.
[[vk::combinedImageSampler]][[vk::binding(0, 0)]] Texture2D Depth;
[[vk::combinedImageSampler]][[vk::binding(0, 0)]] SamplerState sampler_Depth;
[[vk::combinedImageSampler]][[vk::binding(1, 0)]] Texture2D Normal;
[[vk::combinedImageSampler]][[vk::binding(1, 0)]] SamplerState sampler_Normal;

struct PushConstants_FullscreenTest
{
    float4 sizeThicknessNear; // x = 1/width, y = 1/height, z = thickness, w = camera near
    float4 params;
};
[[vk::push_constant]] PushConstants_FullscreenTest pc;

struct Varyings
{
    float2 uv : TEXCOORD0;
    float4 position : SV_POSITION;
};

Varyings mainVS(uint vertexID : SV_VertexID)
{
    Varyings output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

float4 mainPS(Varyings input) : SV_Target0
{
    float depth = Depth.Sample(sampler_Depth, input.uv).x;
    float3 normal = Normal.Sample(sampler_Normal, input.uv).xyz;
    return float4(normal * pc.params.xyz, pc.params.w * saturate(pc.sizeThicknessNear.z) * saturate(depth));
}
