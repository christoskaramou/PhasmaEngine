#include "API/Reflection.h"
#include "API/Shader.h"
#include "API/Descriptor.h"

namespace pe
{
    std::string GetResourceName(const spirv_cross::Compiler &compiler, spirv_cross::ID id)
    {
        std::string name = compiler.get_name(id);
        if (name.empty())
            name = compiler.get_fallback_name(id);

        return name;
    }

    constexpr uint32_t MAX_COUNT_PER_BINDING = 500; // if open or unbounded array, we will use this value with partially bound arrays
    uint32_t GetResourceArrayCount(const spirv_cross::SPIRType &typeInfo)
    {
        // TODO: support arrays of arrays
        if (typeInfo.array.empty())
        {
            return 1; // Not an array
        }

        if (typeInfo.array_size_literal[0])
        {
            if (typeInfo.array[0] == 0)
                return MAX_COUNT_PER_BINDING; // Open array
            else
                return typeInfo.array[0]; // Fixed-size array
        }
        else
        {
            // Unbounded array. Size is determined at runtime.
            return MAX_COUNT_PER_BINDING; // Use a default max size or handle as needed
        }
    }

    void Reflection::Init(Shader *shader)
    {
        m_shader = shader;

        spirv_cross::Compiler compiler{shader->GetSpriv(), shader->Size()};
        auto active = compiler.get_active_interface_variables();
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        compiler.set_enabled_interface_variables(std::move(active));
        auto specConstants = compiler.get_specialization_constants();

        // Specialization constants
        for (const spirv_cross::SpecializationConstant &specConstant : specConstants)
        {
            SpecializationConstantDesc desc{};
            const spirv_cross::SPIRConstant &constant = compiler.get_constant(specConstant.id);
            desc.name = GetResourceName(compiler, specConstant.id);
            desc.typeInfo = compiler.get_type(constant.constant_type);
            desc.constantId = specConstant.constant_id;

            m_specializationConstants.push_back(desc);
        }

        // Shader inputs
        for (const spirv_cross::Resource &resource : resources.stage_inputs)
        {
            ShaderInOutDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);

            m_inputs.push_back(desc);
        }
        std::sort(m_inputs.begin(), m_inputs.end(), [](auto &a, auto &b)
                  { return a.location < b.location; });

        // Shader outputs
        for (const spirv_cross::Resource &resource : resources.stage_outputs)
        {
            ShaderInOutDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);

            m_outputs.push_back(desc);
        }
        std::sort(m_outputs.begin(), m_outputs.end(), [](auto &a, auto &b)
                  { return a.location < b.location; });

        // Combined Image Samplers
        for (const spirv_cross::Resource &resource : resources.sampled_images)
        {
            CombinedImageSamplerDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_combinedImageSamplers.push_back(desc);
        }

        // Samplers
        for (const spirv_cross::Resource &resource : resources.separate_samplers)
        {
            SamplerDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_samplers.push_back(desc);
        }

        // Images
        for (const spirv_cross::Resource &resource : resources.separate_images)
        {
            ImageDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_images.push_back(desc);
        }

        // Storage Images
        for (const spirv_cross::Resource &resource : resources.storage_images)
        {
            ImageDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_storageImages.push_back(desc);
        }

        // Uniform Buffers
        for (const spirv_cross::Resource &resource : resources.uniform_buffers)
        {
            BufferDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.bufferSize = compiler.get_declared_struct_size(desc.typeInfo);

            m_uniformBuffers.push_back(desc);
        }

        // Storage Buffers
        for (const spirv_cross::Resource &resource : resources.storage_buffers)
        {
            BufferDesc desc{};
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.bufferSize = compiler.get_declared_struct_size(desc.typeInfo);

            m_storageBuffers.push_back(desc);
        }

        // Push constants
        for (const spirv_cross::Resource &resource : resources.push_constant_buffers)
        {
            m_pushConstants.name = GetResourceName(compiler, resource.id);
            m_pushConstants.typeInfo = compiler.get_type(resource.base_type_id); // compiler.get_type(resource.type_id);
            m_pushConstants.structName = compiler.get_name(m_pushConstants.typeInfo.self);
            m_pushConstants.size = compiler.get_declared_struct_size(m_pushConstants.typeInfo);
        }
    }

    static const std::unordered_map<spirv_cross::SPIRType::BaseType, size_t> s_typeSizeMap = {
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

    uint32_t GetTypeSize(const ShaderInOutDesc &inout)
    {
        auto it = s_typeSizeMap.find(inout.typeInfo.basetype);
        if (it != s_typeSizeMap.end())
        {
            return static_cast<uint32_t>(it->second);
        }

        return 0; // Default case
    }

    static const std::unordered_map<spirv_cross::SPIRType::BaseType, ReflectionVariableType> s_typeReflectionMap = {
        {spirv_cross::SPIRType::Boolean, ReflectionVariableType::UInt},
        {spirv_cross::SPIRType::UByte, ReflectionVariableType::UInt},
        {spirv_cross::SPIRType::UShort, ReflectionVariableType::UInt},
        {spirv_cross::SPIRType::UInt, ReflectionVariableType::UInt},
        {spirv_cross::SPIRType::UInt64, ReflectionVariableType::UInt},
        {spirv_cross::SPIRType::SByte, ReflectionVariableType::SInt},
        {spirv_cross::SPIRType::Short, ReflectionVariableType::SInt},
        {spirv_cross::SPIRType::Int, ReflectionVariableType::SInt},
        {spirv_cross::SPIRType::Int64, ReflectionVariableType::SInt},
        {spirv_cross::SPIRType::Half, ReflectionVariableType::SFloat},
        {spirv_cross::SPIRType::Float, ReflectionVariableType::SFloat},
        {spirv_cross::SPIRType::Double, ReflectionVariableType::SFloat},
    };

    ReflectionVariableType GetReflectionVariableType(const ShaderInOutDesc &inout)
    {
        auto it = s_typeReflectionMap.find(inout.typeInfo.basetype);
        if (it != s_typeReflectionMap.end())
        {
            return it->second;
        }

        return ReflectionVariableType::None; // Default case
    }

    static const std::unordered_map<std::pair<uint32_t, ReflectionVariableType>, vk::Format, PairHash_um> s_attributeFormatMap = {
        {{1, ReflectionVariableType::SInt}, {vk::Format::eR8Sint}},
        {{1, ReflectionVariableType::UInt}, {vk::Format::eR8Uint}},
        {{2, ReflectionVariableType::SInt}, {vk::Format::eR16Sint}},
        {{2, ReflectionVariableType::UInt}, {vk::Format::eR16Uint}},
        {{2, ReflectionVariableType::SFloat}, {vk::Format::eR16Sfloat}},
        {{4, ReflectionVariableType::SInt}, {vk::Format::eR32Sint}},
        {{4, ReflectionVariableType::UInt}, {vk::Format::eR32Uint}},
        {{4, ReflectionVariableType::SFloat}, {vk::Format::eR32Sfloat}},
        {{6, ReflectionVariableType::SInt}, {vk::Format::eR16G16B16Sint}},
        {{6, ReflectionVariableType::UInt}, {vk::Format::eR16G16B16Uint}},
        {{6, ReflectionVariableType::SFloat}, {vk::Format::eR16G16B16Sfloat}},
        {{8, ReflectionVariableType::SInt}, {vk::Format::eR32G32Sint}},
        {{8, ReflectionVariableType::UInt}, {vk::Format::eR32G32Uint}},
        {{8, ReflectionVariableType::SFloat}, {vk::Format::eR32G32Sfloat}},
        {{12, ReflectionVariableType::SInt}, {vk::Format::eR32G32B32Sint}},
        {{12, ReflectionVariableType::UInt}, {vk::Format::eR32G32B32Uint}},
        {{12, ReflectionVariableType::SFloat}, {vk::Format::eR32G32B32Sfloat}},
        {{16, ReflectionVariableType::SInt}, {vk::Format::eR32G32B32A32Sint}},
        {{16, ReflectionVariableType::UInt}, {vk::Format::eR32G32B32A32Uint}},
        {{16, ReflectionVariableType::SFloat}, {vk::Format::eR32G32B32A32Sfloat}},
    };

    vk::Format GetAttributeFormat(const ShaderInOutDesc &input)
    {
        uint32_t size = GetTypeSize(input) * input.typeInfo.vecsize * input.typeInfo.columns;
        ReflectionVariableType type = GetReflectionVariableType(input);

        auto it = s_attributeFormatMap.find({size, type});
        if (it != s_attributeFormatMap.end())
        {
            return it->second;
        }

        PE_ERROR("Unsupported attribute type");
        return vk::Format::eUndefined;
    }

    std::vector<vk::VertexInputBindingDescription> Reflection::GetVertexBindings()
    {
        PE_ERROR_IF(m_shader->GetShaderStage() != vk::ShaderStageFlagBits::eVertex, "Vertex bindings are only available for vertex shaders");

        // separate bindings by binding number
        std::unordered_map<uint32_t, std::vector<ShaderInOutDesc>> bindingsMap{};
        for (const ShaderInOutDesc &input : m_inputs)
        {
            if (input.binding == INT32_MIN)
                continue;

            bindingsMap[input.binding].push_back(input);
        }

        // sort by location
        for (auto &pair : bindingsMap)
        {
            std::sort(pair.second.begin(), pair.second.end(), [](auto &a, auto &b)
                      { return a.location < b.location; });
        }

        std::vector<vk::VertexInputBindingDescription> vertexBindings{};
        vertexBindings.reserve(bindingsMap.size());

        for (const auto &pair : bindingsMap)
        {
            vk::VertexInputBindingDescription binding{};
            binding.binding = pair.first;
            binding.inputRate = vk::VertexInputRate::eVertex;
            binding.stride = 0;
            for (const ShaderInOutDesc &input : pair.second)
                binding.stride += GetTypeSize(input) * input.typeInfo.vecsize * input.typeInfo.columns;

            vertexBindings.push_back(binding);
        }

        return vertexBindings;
    }

    std::vector<vk::VertexInputAttributeDescription> Reflection::GetVertexAttributes()
    {
        PE_ERROR_IF(m_shader->GetShaderStage() != vk::ShaderStageFlagBits::eVertex, "Vertex attributes are only available for vertex shaders");

        std::unordered_map<uint32_t, uint32_t> bindingOffsets{};

        std::vector<vk::VertexInputAttributeDescription> vertexAttributes{};
        vertexAttributes.reserve(m_inputs.size());

        for (const ShaderInOutDesc &input : m_inputs)
        {
            if (input.binding == INT32_MIN)
                continue;

            uint32_t &offset = bindingOffsets.try_emplace(input.binding, 0).first->second;

            uint32_t size = GetTypeSize(input) * input.typeInfo.vecsize * input.typeInfo.columns;
            vk::VertexInputAttributeDescription attribute{};
            attribute.location = input.location;
            attribute.binding = input.binding;
            attribute.format = GetAttributeFormat(input);
            attribute.offset = offset;
            offset += size;

            vertexAttributes.push_back(attribute);
        }

        return vertexAttributes;
    }

    std::vector<Descriptor *> Reflection::GetDescriptors()
    {
        // Check if we have any descriptors
        int maxSet = INT32_MIN;
        for (const CombinedImageSamplerDesc &desc : m_combinedImageSamplers)
            maxSet = std::max(maxSet, desc.set);
        for (const SamplerDesc &desc : m_samplers)
            maxSet = std::max(maxSet, desc.set);
        for (const ImageDesc &desc : m_images)
            maxSet = std::max(maxSet, desc.set);
        for (const ImageDesc &desc : m_storageImages)
            maxSet = std::max(maxSet, desc.set);
        for (const BufferDesc &desc : m_uniformBuffers)
            maxSet = std::max(maxSet, desc.set);
        for (const BufferDesc &desc : m_storageBuffers)
            maxSet = std::max(maxSet, desc.set);

        if (maxSet == INT32_MIN)
            return {};

        // Sets with their info
        std::vector<std::vector<DescriptorBindingInfo>> setInfos{};
        setInfos.resize(maxSet + 1);

        std::vector<Descriptor *> descriptors{};
        descriptors.reserve(maxSet + 1);

        for (const CombinedImageSamplerDesc &desc : m_combinedImageSamplers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            info.type = vk::DescriptorType::eCombinedImageSampler;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const SamplerDesc &desc : m_samplers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.type = vk::DescriptorType::eSampler;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const ImageDesc &desc : m_images)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            info.type = vk::DescriptorType::eSampledImage;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const ImageDesc &desc : m_storageImages)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.imageLayout = vk::ImageLayout::eGeneral;
            info.type = vk::DescriptorType::eStorageImage;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const BufferDesc &desc : m_uniformBuffers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.type = vk::DescriptorType::eUniformBuffer;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const BufferDesc &desc : m_storageBuffers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.type = vk::DescriptorType::eStorageBuffer;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (auto &setInfo : setInfos)
        {
            Descriptor *descriptor = nullptr;

            if (!setInfo.empty())
            {
                std::sort(setInfo.begin(), setInfo.end(), [](auto &a, auto &b)
                          { return a.binding < b.binding; });
                descriptor = Descriptor::Create(setInfo, m_shader->GetShaderStage(), false, "auto_descriptor");
            }

            descriptors.push_back(descriptor);
        }

        return descriptors;
    }
}
