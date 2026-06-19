struct VSOutput
{
    float4 svPos                           : SV_Position;
    [[vk::location(0)]] float4 direction   : TEXCOORD0;
};

// Full-screen triangle. `direction` is the clip-space position passed to
// the fragment shader so it can reconstruct a view-space direction via
// the inverse view-projection matrix.
VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f),
    };
    float2 p = pos[vertexIndex];

    VSOutput o;
    // Flip Y to match Vulkan's Y-down NDC (WebGPU is Y-up).
    o.svPos     = float4(p.x, -p.y, 0.0f, 1.0f);
    o.direction = float4(p.x,  p.y, -1.0f, 1.0f);
    return o;
}
