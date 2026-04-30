// Port of webgpu-samples/alpha-to-coverage-emulator scenes/CrossingGradients.wgsl
// (vertex side) to HLSL (SM 6.0). Not used by the default (Foliage) preset
// but ported for content completeness.

struct Varying
{
    float4 pos   : SV_Position;
    float4 color : COLOR0;
};

static const float2 kSquare[6] = {
    float2(-1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f, -1.0f),
    float2( 1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
};

Varying VSMain(uint vertex_index : SV_VertexID, float4 color : ATTRIBUTE0)
{
    float2 p = kSquare[vertex_index % 6];
    // Direct clip-space vertex; no projection matrix. Flip Y for Vulkan NDC.
    Varying o;
    o.pos   = float4(p.x, -p.y, 0.0f, 1.0f);
    o.color = color;
    return o;
}
