struct MeshInfoGPU
{
    uint indexOffset;
    uint vertexOffset;
    uint positionsOffset;
    int textures[5]; // BaseColor, Normal, Metallic, Occlusion, Emissive
};

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
};

struct PositionUvVertex
{
    float3 position;
    float2 uv;
};
