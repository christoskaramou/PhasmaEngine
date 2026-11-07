#pragma once

#include "API/Reflection.h"
#include "spirv_cross.hpp"

namespace pe
{
    class Shader;

    // The SPIR-V a shader was created from: Vulkan's module, or the copy WebGPU ships for reflection only (WebGPU RHI
    // spec decision 7). Empty on DX12.
    std::span<const uint32_t> GetShaderSpirv(const Shader *shader);

    void PopulateReflectionFromSpirv(Reflection &refl, Shader *shader);

    std::vector<StructMemberInfo> ReflectStructMembersFromSpirv(const spirv_cross::Compiler &compiler,
                                                                const spirv_cross::SPIRType &structType);

    StructMemberBaseType ToStructMemberBaseType(spirv_cross::SPIRType::BaseType base);
} // namespace pe
