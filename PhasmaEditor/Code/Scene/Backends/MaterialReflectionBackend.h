#pragma once

#include "Scene/MaterialReflection.h"

namespace pe
{
    struct PassVariant;

    struct ReflectedMaterialResources
    {
        std::vector<StructMemberInfo> rawMembers;
        std::vector<MaterialTextureSlot> textureSlots;
        uint32_t totalByteSize = 0;
    };

    ReflectedMaterialResources ReflectMaterialResourcesForBackend(const PassVariant &variant);
} // namespace pe
