#include "../Common/Structures.hlsl"
#include "../Common/MeshInfo.hlsl"

struct HitPayload
{
    float4 color;
};

// Bindings
// 0: TLAS
// 1: Output
// 2: Uniforms (SceneData)
// 3: MeshInfos
StructuredBuffer<MeshInfoGPU> g_meshInfos : register(t3);
// 4: Indices
StructuredBuffer<uint> g_indices : register(t4);
// 5: Vertices
StructuredBuffer<Vertex> g_vertices : register(t5);
// 6: Textures
Texture2D g_textures[] : register(t6);
SamplerState g_sampler : register(s6);
// 7: PositionUVs
StructuredBuffer<PositionUvVertex> g_positionUVs : register(t7);

[shader("closesthit")]
void main(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint instanceID = InstanceID();
    uint primitiveID = PrimitiveIndex();
    
    MeshInfoGPU meshInfo = g_meshInfos[instanceID];
    
    // Indices
    // 3 indices per triangle
    uint indexOffset = meshInfo.indexOffset + primitiveID * 3;
    uint3 indices = uint3(
        g_indices[indexOffset + 0],
        g_indices[indexOffset + 1],
        g_indices[indexOffset + 2]
    );

    // Barycentrics
    float3 barycentricCoords = float3(1.0f - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    // PositionUVs
    // Vertex offset is global relative to buffer start, but g_positionUVs is bound from global offset.
    // So we access with (current_mesh_offset + index).
    // Wait, meshInfo.vertexOffset is GLOBAL vertex index.
    // But g_vertices is bound starting at m_verticesOffset.
    // Scene.cpp: meshInfo.vertexOffset += static_cast<uint32_t>(verticesCount);
    // So meshInfo.vertexOffset is 0 for first mesh, N for second.
    // If g_vertices bound at m_verticesOffset, then g_vertices[0] is first vertex of first mesh.
    // So g_vertices[meshInfo.vertexOffset + index] IS CORRECT?
    // Wait, meshInfo.vertexOffset is relative to m_verticesOffset?
    // Scene.cpp accumulates verticesCount.
    // Yes. m_verticesOffset is the start of ALL vertices.
    // meshInfo.vertexOffset is offset within that ALL vertices block.
    // So yes, g_vertices[meshInfo.vertexOffset + index] is correct.
    // Same for g_positionUVs.
    // Wait, meshInfo.positionsOffset corresponds to PositionUvVertex buffer.
    // In Scene.cpp, positionsOffset accumulates positionsCount.
    // So logic is consistent.

    // Fetch UVs
    PositionUvVertex v0 = g_positionUVs[meshInfo.positionsOffset + indices.x];
    PositionUvVertex v1 = g_positionUVs[meshInfo.positionsOffset + indices.y];
    PositionUvVertex v2 = g_positionUVs[meshInfo.positionsOffset + indices.z];

    float2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;

    // Sample Texture
    // Texture 0 is Base Color
    int textureIndex = meshInfo.textures[0];
    float4 color = float4(1, 0, 1, 1); // Fallback debug pink
    
    if (textureIndex >= 0)
    {
        color = g_textures[textureIndex].SampleLevel(g_sampler, uv, 0);
    }
    else
    {
        // Debug: use barycentrics if no texture
        color = float4(barycentricCoords, 1.0);
    }
    
    // DEBUG FORCE COLOR
    color = float4(barycentricCoords, 1.0);

    payload.color = color;
}
