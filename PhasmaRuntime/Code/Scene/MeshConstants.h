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
    };
} // namespace pe
