#pragma once
#include "Scene/MaterialAnnotation.h"
#include "API/Reflection.h"

namespace pe
{
    class PassInfoAsset;

    struct MaterialTextureSlot
    {
        std::string bindingName;
        int set = -1;
        int binding = -1;
        MaterialWidgetHint hint = MaterialWidgetHint::Auto;
        uint32_t byteOffset = 0xFFFFFFFF; // offset of the uint index field in the material struct
    };

    struct MaterialFieldDesc
    {
        std::string name;
        spirv_cross::SPIRType::BaseType baseType = spirv_cross::SPIRType::Float;
        uint32_t vecSize = 1;
        uint32_t offset = 0;
        uint32_t size = 0;
        MaterialWidgetHint hint = MaterialWidgetHint::Auto;
        float rangeMin = 0.f;
        float rangeMax = 1.f;
    };

    struct MaterialLayout
    {
        std::vector<MaterialFieldDesc> fields;
        std::vector<MaterialTextureSlot> textureSlots;
        std::vector<StructMemberInfo> structMembers;
        uint32_t totalByteSize = 0;
        bool valid = false;
    };

    MaterialLayout ReflectMaterialLayout(const PassInfoAsset &passInfo);
} // namespace pe
