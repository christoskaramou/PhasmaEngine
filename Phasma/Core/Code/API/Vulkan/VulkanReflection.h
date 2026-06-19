#pragma once

#include "API/Reflection.h"
#include "spirv_cross.hpp"

namespace pe
{
    class Shader;

    void PopulateReflectionFromSpirv(Reflection &refl, Shader *shader);

    std::vector<StructMemberInfo> ReflectStructMembersFromSpirv(const spirv_cross::Compiler &compiler,
                                                                const spirv_cross::SPIRType &structType);

    StructMemberBaseType ToStructMemberBaseType(spirv_cross::SPIRType::BaseType base);
} // namespace pe
