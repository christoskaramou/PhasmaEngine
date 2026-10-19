#ifndef PE_DEFORMED_GEOMETRY
#define PE_DEFORMED_GEOMETRY

static const uint PE_EDITOR_FLAG_DEFORMED = 64u;
static const uint PE_DEFORMED_VERTEX_SIZE = 80u;

bool PeUsesGeneratedMask(Mesh_Constants mesh)
{
    return (mesh.editorFlags & 32u) != 0u && mesh.renderType == 2u && // RenderType::AlphaCut
           mesh.materialByteOffset != 0xffffffffu;
}

struct PeDeformedVertex
{
    float4 clip;
    float4 previousClip;
    float3 position;
    float3 normal;
    float4 tangent;
};

bool PeLoadDeformedVertex(ByteAddressBuffer buffer, uint draw, uint sourceVertex, out PeDeformedVertex vertex)
{
    vertex = (PeDeformedVertex)0;
    uint bytes;
    buffer.GetDimensions(bytes);
    if (bytes < 16u)
        return false;
    if (draw >= buffer.Load(0u) || draw >= (bytes - 16u) / 16u)
        return false;

    const uint4 header = buffer.Load4((draw + 1u) * 16u);
    const bool sourceRange = sourceVertex >= header.x && sourceVertex - header.x < header.y;
    const bool outputRange = header.z <= bytes && bytes - header.z >= PE_DEFORMED_VERTEX_SIZE;
    if (!sourceRange || !outputRange)
    {
        return false;
    }

    const uint local = sourceVertex - header.x;
    if (local > (bytes - header.z - PE_DEFORMED_VERTEX_SIZE) / PE_DEFORMED_VERTEX_SIZE)
        return false;

    const uint offset = header.z + local * PE_DEFORMED_VERTEX_SIZE;
    vertex.clip = asfloat(buffer.Load4(offset));
    vertex.previousClip = asfloat(buffer.Load4(offset + 16u));
    vertex.position = asfloat(buffer.Load3(offset + 32u));
    vertex.normal = asfloat(buffer.Load3(offset + 48u));
    vertex.tangent = asfloat(buffer.Load4(offset + 64u));
    return true;
}

#endif
