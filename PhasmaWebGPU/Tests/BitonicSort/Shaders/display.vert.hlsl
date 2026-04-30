// Fullscreen triangle-list (6 verts) driven by SV_VertexID. Y is flipped so the
// grid origin matches the WGSL reference (bottom-left = element 0).

struct VSOutput
{
    float4 svPos                    : SV_Position;
    [[vk::location(0)]] float2 uv   : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Triangle list covering clip space:
    //   tri 0: (1, 1) (1,-1) (-1,-1)
    //   tri 1: (1, 1) (-1,-1) (-1, 1)
    float2 positions[6] = {
        float2( 1.0,  1.0),
        float2( 1.0, -1.0),
        float2(-1.0, -1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
        float2(-1.0,  1.0),
    };

    float2 uvs[6] = {
        float2(1.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(0.0, 0.0),
    };

    VSOutput o;
    float2 p = positions[vertexId];
    // Vulkan clip-space has Y pointing down; flip so the WGSL-reference UV
    // mapping yields the same orientation on screen.
    o.svPos  = float4(p.x, -p.y, 0.0, 1.0);
    o.uv     = uvs[vertexId];
    return o;
}
