struct MeshInfoGPU
{
    uint indexOffset;
    uint vertexOffset;
    uint positionsOffset;
    uint renderType;   // 1: Opaque, 2: AlphaCut, 3: AlphaBlend, 4: Transmission
    int textures[5];   // BaseColor, Normal, Metallic, Occlusion, Emissive
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
