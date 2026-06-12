#include "API/Vulkan/VulkanReflection.h"
#include "API/Vulkan/VulkanShaderImpl.h"
#include "API/RHI.h"
#include "API/Shader.h"

namespace pe
{
    namespace
    {
        std::string GetResourceName(const spirv_cross::Compiler &compiler, spirv_cross::ID id)
        {
            std::string name = compiler.get_name(id);
            if (name.empty())
                name = compiler.get_fallback_name(id);
            return name;
        }

        uint32_t GetResourceArrayCount(const spirv_cross::SPIRType &typeInfo)
        {
            if (typeInfo.array.empty())
                return 1;

            const uint32_t bindlessCount = std::max(1u, std::min(PE_MAX_DESCRIPTORS_PER_BINDING, RHII.GetCaps().maxBindlessTextures));
            if (typeInfo.array_size_literal[0])
            {
                if (typeInfo.array[0] == 0)
                    return bindlessCount; // Open array
                return typeInfo.array[0];
            }
            return bindlessCount; // Unbounded — partially bound at runtime
        }

        const std::unordered_map<spirv_cross::SPIRType::BaseType, size_t> &TypeSizeMap()
        {
            static const std::unordered_map<spirv_cross::SPIRType::BaseType, size_t> map = {
                {spirv_cross::SPIRType::Boolean, sizeof(bool)},
                {spirv_cross::SPIRType::SByte, sizeof(int8_t)},
                {spirv_cross::SPIRType::UByte, sizeof(uint8_t)},
                {spirv_cross::SPIRType::Short, sizeof(int16_t)},
                {spirv_cross::SPIRType::UShort, sizeof(uint16_t)},
                {spirv_cross::SPIRType::Int, sizeof(int32_t)},
                {spirv_cross::SPIRType::UInt, sizeof(uint32_t)},
                {spirv_cross::SPIRType::Int64, sizeof(int64_t)},
                {spirv_cross::SPIRType::UInt64, sizeof(uint64_t)},
                {spirv_cross::SPIRType::Half, sizeof(float) / 2},
                {spirv_cross::SPIRType::Float, sizeof(float)},
                {spirv_cross::SPIRType::Double, sizeof(double)},
            };
            return map;
        }

        uint32_t GetTypeSize(spirv_cross::SPIRType::BaseType base)
        {
            const auto &map = TypeSizeMap();
            auto it = map.find(base);
            return it != map.end() ? static_cast<uint32_t>(it->second) : 0u;
        }

        ReflectionVariableType GetReflectionVariableType(spirv_cross::SPIRType::BaseType base)
        {
            switch (base)
            {
            case spirv_cross::SPIRType::Boolean:
            case spirv_cross::SPIRType::UByte:
            case spirv_cross::SPIRType::UShort:
            case spirv_cross::SPIRType::UInt:
            case spirv_cross::SPIRType::UInt64:
                return ReflectionVariableType::UInt;
            case spirv_cross::SPIRType::SByte:
            case spirv_cross::SPIRType::Short:
            case spirv_cross::SPIRType::Int:
            case spirv_cross::SPIRType::Int64:
                return ReflectionVariableType::SInt;
            case spirv_cross::SPIRType::Half:
            case spirv_cross::SPIRType::Float:
            case spirv_cross::SPIRType::Double:
                return ReflectionVariableType::SFloat;
            default:
                return ReflectionVariableType::None;
            }
        }

        ::PeFormat GetAttributeFormat(uint32_t totalSize, ReflectionVariableType type)
        {
            switch (type)
            {
            case ReflectionVariableType::SInt:
                switch (totalSize)
                {
                case 1:
                    return ::PE_FORMAT_R8_SINT;
                case 2:
                    return ::PE_FORMAT_R16_SINT;
                case 4:
                    return ::PE_FORMAT_R32_SINT;
                case 6:
                    return ::PE_FORMAT_R16G16B16_SINT;
                case 8:
                    return ::PE_FORMAT_R32G32_SINT;
                case 12:
                    return ::PE_FORMAT_R32G32B32_SINT;
                case 16:
                    return ::PE_FORMAT_R32G32B32A32_SINT;
                }
                break;
            case ReflectionVariableType::UInt:
                switch (totalSize)
                {
                case 1:
                    return ::PE_FORMAT_R8_UINT;
                case 2:
                    return ::PE_FORMAT_R16_UINT;
                case 4:
                    return ::PE_FORMAT_R32_UINT;
                case 6:
                    return ::PE_FORMAT_R16G16B16_UINT;
                case 8:
                    return ::PE_FORMAT_R32G32_UINT;
                case 12:
                    return ::PE_FORMAT_R32G32B32_UINT;
                case 16:
                    return ::PE_FORMAT_R32G32B32A32_UINT;
                }
                break;
            case ReflectionVariableType::SFloat:
                switch (totalSize)
                {
                case 2:
                    return ::PE_FORMAT_R16_SFLOAT;
                case 4:
                    return ::PE_FORMAT_R32_SFLOAT;
                case 6:
                    return ::PE_FORMAT_R16G16B16_SFLOAT;
                case 8:
                    return ::PE_FORMAT_R32G32_SFLOAT;
                case 12:
                    return ::PE_FORMAT_R32G32B32_SFLOAT;
                case 16:
                    return ::PE_FORMAT_R32G32B32A32_SFLOAT;
                }
                break;
            default:
                break;
            }

            PE_ERROR("[Shader] Unsupported attribute type");
            return ::PE_FORMAT_UNDEFINED;
        }

        spv::ExecutionModel ToExecutionModel(PeShaderStageFlags stage)
        {
            if (stage == PE_SHADER_STAGE_VERTEX)
                return spv::ExecutionModelVertex;
            if (stage == PE_SHADER_STAGE_FRAGMENT)
                return spv::ExecutionModelFragment;
            if (stage == PE_SHADER_STAGE_COMPUTE)
                return spv::ExecutionModelGLCompute;
            if (stage == PE_SHADER_STAGE_RAYGEN_KHR)
                return spv::ExecutionModelRayGenerationKHR;
            if (stage == PE_SHADER_STAGE_ANY_HIT_KHR)
                return spv::ExecutionModelAnyHitKHR;
            if (stage == PE_SHADER_STAGE_CLOSEST_HIT_KHR)
                return spv::ExecutionModelClosestHitKHR;
            if (stage == PE_SHADER_STAGE_MISS_KHR)
                return spv::ExecutionModelMissKHR;
            if (stage == PE_SHADER_STAGE_INTERSECTION_KHR)
                return spv::ExecutionModelIntersectionKHR;
            if (stage == PE_SHADER_STAGE_CALLABLE_KHR)
                return spv::ExecutionModelCallableKHR;
            if (stage == PE_SHADER_STAGE_TASK_EXT)
                return spv::ExecutionModelTaskEXT;
            if (stage == PE_SHADER_STAGE_MESH_EXT)
                return spv::ExecutionModelMeshEXT;
            PE_ERROR("[Shader] Unsupported shader stage for reflection entry point selection");
            return spv::ExecutionModelMax;
        }
    } // namespace

    StructMemberBaseType ToStructMemberBaseType(spirv_cross::SPIRType::BaseType base)
    {
        switch (base)
        {
        case spirv_cross::SPIRType::Boolean:
            return StructMemberBaseType::Boolean;
        case spirv_cross::SPIRType::SByte:
            return StructMemberBaseType::SByte;
        case spirv_cross::SPIRType::UByte:
            return StructMemberBaseType::UByte;
        case spirv_cross::SPIRType::Short:
            return StructMemberBaseType::Short;
        case spirv_cross::SPIRType::UShort:
            return StructMemberBaseType::UShort;
        case spirv_cross::SPIRType::Int:
            return StructMemberBaseType::Int;
        case spirv_cross::SPIRType::UInt:
            return StructMemberBaseType::UInt;
        case spirv_cross::SPIRType::Int64:
            return StructMemberBaseType::Int64;
        case spirv_cross::SPIRType::UInt64:
            return StructMemberBaseType::UInt64;
        case spirv_cross::SPIRType::Half:
            return StructMemberBaseType::Half;
        case spirv_cross::SPIRType::Float:
            return StructMemberBaseType::Float;
        case spirv_cross::SPIRType::Double:
            return StructMemberBaseType::Double;
        case spirv_cross::SPIRType::Struct:
            return StructMemberBaseType::Struct;
        default:
            return StructMemberBaseType::Unknown;
        }
    }

    std::vector<StructMemberInfo> ReflectStructMembersFromSpirv(const spirv_cross::Compiler &compiler,
                                                                const spirv_cross::SPIRType &structType)
    {
        std::vector<StructMemberInfo> members;
        const uint32_t memberCount = static_cast<uint32_t>(structType.member_types.size());
        members.reserve(memberCount);

        for (uint32_t i = 0; i < memberCount; ++i)
        {
            const spirv_cross::SPIRType &memberType = compiler.get_type(structType.member_types[i]);

            StructMemberInfo info;
            info.name = compiler.get_member_name(structType.self, i);
            info.offset = compiler.type_struct_member_offset(structType, i);
            info.size = static_cast<uint32_t>(compiler.get_declared_struct_member_size(structType, i));
            info.baseType = ToStructMemberBaseType(memberType.basetype);
            info.vecSize = memberType.vecsize;
            info.columns = memberType.columns;
            members.push_back(info);
        }
        return members;
    }

    void PopulateReflectionFromSpirv(Reflection &refl, Shader *shader)
    {
        refl.m_shader = shader;

        const uint32_t *spirvData = GetVulkanShaderSpirv(shader);
        const size_t spirvWords = GetVulkanShaderSpirvSizeWords(shader);
        if (!spirvData || spirvWords == 0)
            return;

        spirv_cross::Compiler compiler{spirvData, spirvWords};

        if (!shader->GetEntryName().empty())
        {
            const spv::ExecutionModel model = ToExecutionModel(shader->GetShaderStage());
            if (model != spv::ExecutionModelMax)
                compiler.set_entry_point(shader->GetEntryName(), model);
        }

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        const auto specConstants = compiler.get_specialization_constants();

        for (const spirv_cross::SpecializationConstant &specConstant : specConstants)
        {
            const spirv_cross::SPIRConstant &constant = compiler.get_constant(specConstant.id);
            const spirv_cross::SPIRType &type = compiler.get_type(constant.constant_type);

            SpecializationConstantDesc desc{};
            desc.name = GetResourceName(compiler, specConstant.id);
            desc.constantId = specConstant.constant_id;
            desc.baseType = ToStructMemberBaseType(type.basetype);
            desc.vecSize = type.vecsize;
            desc.columns = type.columns;
            refl.m_specializationConstants.push_back(desc);
        }

        auto fillInOut = [&](const spirv_cross::Resource &resource, bool isInput)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            const uint32_t totalSize = GetTypeSize(type.basetype) * type.vecsize * type.columns;
            const ReflectionVariableType reflectionType = GetReflectionVariableType(type.basetype);

            ShaderInOutDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            desc.size = totalSize;
            if (totalSize > 0 && reflectionType != ReflectionVariableType::None)
                desc.format = GetAttributeFormat(totalSize, reflectionType);
            if (isInput)
                desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            return desc;
        };

        for (const spirv_cross::Resource &resource : resources.stage_inputs)
            refl.m_inputs.push_back(fillInOut(resource, true));
        std::sort(refl.m_inputs.begin(), refl.m_inputs.end(),
                  [](auto &a, auto &b)
                  { return a.location < b.location; });

        for (const spirv_cross::Resource &resource : resources.stage_outputs)
            refl.m_outputs.push_back(fillInOut(resource, false));
        std::sort(refl.m_outputs.begin(), refl.m_outputs.end(),
                  [](auto &a, auto &b)
                  { return a.location < b.location; });

        for (const spirv_cross::Resource &resource : resources.sampled_images)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            CombinedImageSamplerReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            refl.m_combinedImageSamplers.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.separate_samplers)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            SamplerReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            refl.m_samplers.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.separate_images)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            ImageReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            refl.m_images.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.storage_images)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            ImageReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            refl.m_storageImages.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.uniform_buffers)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            BufferReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            desc.bufferSize = compiler.get_declared_struct_size(type);
            refl.m_uniformBuffers.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.storage_buffers)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            BufferReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            desc.bufferSize = compiler.get_declared_struct_size(type);
            // SPIR-V doesn't preserve the StructuredBuffer-vs-ByteAddressBuffer source distinction;
            // both surface as a NonWritable buffer block. Vulkan collapses them to eStorageBuffer
            // anyway, so any RO kind is correct here. The DX12 reflection path resolves the
            // precise variant from D3D_SIT_*.
            const auto blockFlags = compiler.get_buffer_block_flags(resource.id);
            desc.kind = blockFlags.get(spv::DecorationNonWritable) ? PeBufferKind::Structured
                                                                   : PeBufferKind::StorageRW;
            refl.m_storageBuffers.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.acceleration_structures)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
            AccelerationStructureReflection desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.count = GetResourceArrayCount(type);
            refl.m_accelerationStructures.push_back(desc);
        }

        for (const spirv_cross::Resource &resource : resources.push_constant_buffers)
        {
            const spirv_cross::SPIRType &type = compiler.get_type(resource.base_type_id);
            refl.m_pushConstants.name = GetResourceName(compiler, resource.id);
            refl.m_pushConstants.structName = compiler.get_name(type.self);
            refl.m_pushConstants.size = compiler.get_declared_struct_size(type);
        }
    }
} // namespace pe
