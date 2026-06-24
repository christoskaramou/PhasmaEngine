#pragma once

namespace pe
{
    struct Mesh_Constants
    {
        float alphaCut;
        float baseColorAlpha;
        uint32_t meshDataOffset;
        uint32_t textureMask;
        uint32_t materialId;
        uint32_t meshImageIndex[5];
        uint32_t materialByteOffset;
        uint32_t editorFlags;
        uint32_t renderType;
        float aabbMinX;
        float aabbMinY;
        float aabbMinZ;
        float aabbMaxX;
        float aabbMaxY;
        float aabbMaxZ;
        // Discrete LOD index ranges (see Mesh::lod*). CullingCS picks a level by camera distance and
        // overrides the draw's firstIndex/indexCount. lodCount==1 means no simplified levels. Must stay
        // byte-identical to the HLSL Mesh_Constants in Structures.hlsl. (4 == Mesh::kMaxLods)
        uint32_t lodCount;
        uint32_t lodIndexOffset[4];
        uint32_t lodIndexCount[4];
        uint32_t lodShift;       // per-mesh additive LOD level offset (see Mesh::lodShift)
        uint32_t lodMeshEnabled; // 0 = this mesh ignores LOD (always full detail)
        float lodMeshBias;       // per-mesh camera-distance multiplier (see Mesh::lodBias)
    };
} // namespace pe
