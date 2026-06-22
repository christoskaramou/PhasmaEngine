#pragma once
#include "Voxel/VoxelTypes.h"

// POD block definition. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    enum class VoxelRenderClass : uint8_t
    {
        Air,
        Opaque /* Cutout, Transparent later */
    };

    struct BlockType
    {
        BlockId id;
        std::string name;
        bool solid;
        bool opaque;
        VoxelRenderClass renderClass;
        uint16_t faceTiles[6]; // face order: +X,-X,+Y,-Y,+Z,-Z
    };
} // namespace pe::voxel
