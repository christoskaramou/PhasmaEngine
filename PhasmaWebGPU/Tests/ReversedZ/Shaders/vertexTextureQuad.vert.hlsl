// Translated from webgpu-samples reversedZ/vertexTextureQuad.wgsl.
// Generates a full-screen triangle pair from SV_VertexID for the depth-quad
// visualization pass.

static const float2 kPositions[6] = {
    float2(-1.0f, -1.0f),
    float2( 1.0f, -1.0f),
    float2(-1.0f,  1.0f),
    float2(-1.0f,  1.0f),
    float2( 1.0f, -1.0f),
    float2( 1.0f,  1.0f),
};

float4 VSMain(uint vertexIndex : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexIndex], 0.0f, 1.0f);
}
