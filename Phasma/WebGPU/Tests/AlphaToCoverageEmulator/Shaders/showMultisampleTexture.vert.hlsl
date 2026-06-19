// Port of webgpu-samples/alpha-to-coverage-emulator ShowMultisampleTexture.wgsl
// (vertex side) to HLSL (SM 6.0).

struct Varying
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

static const float2 kSquare[6] = {
    float2(0.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 0.0f),
    float2(1.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f),
};

Varying VSMain(uint vertex_index : SV_VertexID)
{
    float2 uv = kSquare[vertex_index];
    // Flip Y for Vulkan clip-space: upstream writes uv*(2,-2)+(-1,1). In our
    // engine we flip the Y in the shader instead of using a Vulkan Y-down
    // projection matrix, because this pipeline draws directly in clip space.
    float4 pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    // Vulkan's NDC has +Y pointing down; undo the upstream flip so the image
    // appears right-side-up in our swapchain.
    pos.y = -pos.y;

    Varying o;
    o.pos = pos;
    o.uv  = uv;
    return o;
}
