#include "Reflection.h"
#include "Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"

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
    uint32_t GetResourceArrayCount(spirv_cross::SPIRType typeInfo)
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

    Reflection::~Reflection()
    {
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
            SpecializationConstantDesc desc;
            const spirv_cross::SPIRConstant &constant = compiler.get_constant(specConstant.id);
            desc.name = GetResourceName(compiler, specConstant.id);
            desc.typeInfo = compiler.get_type(constant.constant_type);
            desc.constantId = specConstant.constant_id;

            m_specializationConstants.push_back(desc);
        }

        // Shader inputs
        for (const spirv_cross::Resource &resource : resources.stage_inputs)
        {
            ShaderInOutDesc desc;
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);

            m_inputs.push_back(desc);
        }
        std::sort(m_inputs.begin(), m_inputs.end(), [](auto &a, auto &b)
                  { return a.location < b.location; });

        // Shader outputs
        for (const spirv_cross::Resource &resource : resources.stage_outputs)
        {
            ShaderInOutDesc desc;
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
            CombinedImageSamplerDesc desc;
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_combinedImageSamplers.push_back(desc);
        }

        // Samplers
        for (const spirv_cross::Resource &resource : resources.separate_samplers)
        {
            SamplerDesc desc;
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_samplers.push_back(desc);
        }

        // Images
        for (const spirv_cross::Resource &resource : resources.separate_images)
        {
            ImageDesc desc;
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_images.push_back(desc);
        }

        // Storage Images
        for (const spirv_cross::Resource &resource : resources.storage_images)
        {
            ImageDesc desc;
            desc.name = GetResourceName(compiler, resource.id);
            desc.typeInfo = compiler.get_type(resource.type_id);
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            m_storageImages.push_back(desc);
        }

        // Uniform Buffers
        for (const spirv_cross::Resource &resource : resources.uniform_buffers)
        {
            BufferDesc desc;
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
            BufferDesc desc;
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

    uint32_t GetTypeSize(const ShaderInOutDesc &inout)
    {
        switch (inout.typeInfo.basetype)
        {
        case spirv_cross::SPIRType::Unknown:
        case spirv_cross::SPIRType::Void:
        case spirv_cross::SPIRType::AtomicCounter:
        case spirv_cross::SPIRType::Struct:
        case spirv_cross::SPIRType::Image:
        case spirv_cross::SPIRType::SampledImage:
        case spirv_cross::SPIRType::Sampler:
        case spirv_cross::SPIRType::AccelerationStructure:
        case spirv_cross::SPIRType::RayQuery:
        case spirv_cross::SPIRType::ControlPointArray:
        case spirv_cross::SPIRType::Interpolant:
        case spirv_cross::SPIRType::Char:
            return 0;
        case spirv_cross::SPIRType::Boolean:
            return sizeof(bool);
        case spirv_cross::SPIRType::SByte:
            return sizeof(int8_t);
        case spirv_cross::SPIRType::UByte:
            return sizeof(uint8_t);
        case spirv_cross::SPIRType::Short:
            return sizeof(int16_t);
        case spirv_cross::SPIRType::UShort:
            return sizeof(uint16_t);
        case spirv_cross::SPIRType::Int:
            return sizeof(int32_t);
        case spirv_cross::SPIRType::UInt:
            return sizeof(uint32_t);
        case spirv_cross::SPIRType::Int64:
            return sizeof(int64_t);
        case spirv_cross::SPIRType::UInt64:
            return sizeof(uint64_t);
        case spirv_cross::SPIRType::Half:
            return sizeof(float_t) / 2;
        case spirv_cross::SPIRType::Float:
            return sizeof(float_t);
        case spirv_cross::SPIRType::Double:
            return sizeof(double_t);
        default:
            return 0;
        }
    }

    ReflectionVariableType GetReflectionVariableType(const ShaderInOutDesc &inout)
    {
        switch (inout.typeInfo.basetype)
        {
        case spirv_cross::SPIRType::Unknown:
        case spirv_cross::SPIRType::Void:
        case spirv_cross::SPIRType::AtomicCounter:
        case spirv_cross::SPIRType::Struct:
        case spirv_cross::SPIRType::Image:
        case spirv_cross::SPIRType::SampledImage:
        case spirv_cross::SPIRType::Sampler:
        case spirv_cross::SPIRType::AccelerationStructure:
        case spirv_cross::SPIRType::RayQuery:
        case spirv_cross::SPIRType::ControlPointArray:
        case spirv_cross::SPIRType::Interpolant:
        case spirv_cross::SPIRType::Char:
            return ReflectionVariableType::None;
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

    Format GetAttibuteType(const ShaderInOutDesc &input)
    {
        uint32_t size = GetTypeSize(input) * input.typeInfo.vecsize * input.typeInfo.columns;
        ReflectionVariableType type = GetReflectionVariableType(input);
        switch (size)
        {
        case 1:
            if (type == ReflectionVariableType::SInt)
                return Format::R8SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::R8UInt;

        case 2:
            if (type == ReflectionVariableType::SInt)
                return Format::R16SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::R16UInt;
            else if (type == ReflectionVariableType::SFloat)
                return Format::R16SFloat;

        case 4:
            if (type == ReflectionVariableType::SInt)
                return Format::R32SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::R32UInt;
            else if (type == ReflectionVariableType::SFloat)
                return Format::R32SFloat;

        case 6:
            if (type == ReflectionVariableType::SInt)
                return Format::RGB16SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::RGB16UInt;
            else if (type == ReflectionVariableType::SFloat)
                return Format::RGB16SFloat;

        case 8:
            if (type == ReflectionVariableType::SInt)
                return Format::RG32SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::RG32UInt;
            else if (type == ReflectionVariableType::SFloat)
                return Format::RG32SFloat;

        case 12:
            if (type == ReflectionVariableType::SInt)
                return Format::RGB32SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::RGB32UInt;
            else if (type == ReflectionVariableType::SFloat)
                return Format::RGB32SFloat;

        case 16:
            if (type == ReflectionVariableType::SInt)
                return Format::RGBA32SInt;
            else if (type == ReflectionVariableType::UInt)
                return Format::RGBA32UInt;
            else if (type == ReflectionVariableType::SFloat)
                return Format::RGBA32SFloat;
        }

        PE_ERROR("Unsupported attribute type");
        return Format::Undefined;
    }

    std::vector<VertexInputBindingDescription> Reflection::GetVertexBindings()
    {
        PE_ERROR_IF(m_shader->GetShaderStage() != ShaderStage::VertexBit, "Vertex bindings are only available for vertex shaders");

        uint32_t stride = 0;
        for (const ShaderInOutDesc &input : m_inputs)
            stride += GetTypeSize(input) * input.typeInfo.vecsize * input.typeInfo.columns;

        if (stride == 0)
            return {};

        std::vector<VertexInputBindingDescription> vertexBindings{};

        VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = stride;
        binding.inputRate = VertexInputRate::Vertex;
        vertexBindings.push_back(binding);

        return vertexBindings;
    }

    std::vector<VertexInputAttributeDescription> Reflection::GetVertexAttributes()
    {
        PE_ERROR_IF(m_shader->GetShaderStage() != ShaderStage::VertexBit, "Vertex attributes are only available for vertex shaders");

        std::vector<VertexInputAttributeDescription> vertexAttributes{};

        uint32_t offset = 0;
        for (const ShaderInOutDesc &input : m_inputs)
        {
            uint32_t size = GetTypeSize(input) * input.typeInfo.vecsize * input.typeInfo.columns;
            VertexInputAttributeDescription attribute;
            attribute.location = input.location;
            attribute.binding = 0;
            attribute.format = GetAttibuteType(input);
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
            DescriptorBindingInfo info;
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.imageLayout = ImageLayout::ShaderReadOnly;
            info.type = DescriptorType::CombinedImageSampler;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const SamplerDesc &desc : m_samplers)
        {
            DescriptorBindingInfo info;
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.type = DescriptorType::Sampler;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const ImageDesc &desc : m_images)
        {
            DescriptorBindingInfo info;
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.imageLayout = ImageLayout::ShaderReadOnly;
            info.type = DescriptorType::SampledImage;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const ImageDesc &desc : m_storageImages)
        {
            DescriptorBindingInfo info;
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.imageLayout = ImageLayout::General;
            info.type = DescriptorType::StorageImage;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const BufferDesc &desc : m_uniformBuffers)
        {
            DescriptorBindingInfo info;
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.type = DescriptorType::UniformBuffer;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (const BufferDesc &desc : m_storageBuffers)
        {
            DescriptorBindingInfo info;
            info.binding = desc.binding;
            info.count = GetResourceArrayCount(desc.typeInfo);
            info.type = DescriptorType::StorageBuffer;
            info.name = desc.name;

            setInfos[desc.set].push_back(info);
        }

        for (auto &setInfo : setInfos)
        {
            if (setInfo.empty())
                continue;

            std::sort(setInfo.begin(), setInfo.end(), [](auto &a, auto &b)
                      { return a.binding < b.binding; });
        }

        for (auto &setInfo : setInfos)
        {
            if (setInfo.empty())
            {
                descriptors.push_back(nullptr);
                continue;
            }

            // // count all bindings and decide if we can use push descriptor
            // uint32_t count = 0;
            // for (const DescriptorBindingInfo &bindingInfo : setInfo)
            // count += bindingInfo.count;
            // bool pushDescriptor = count <= RHII.GetMaxPushDescriptorsPerSet();

            bool usePushDescriptor = false;

            Descriptor *descriptor = Descriptor::Create(setInfo, m_shader->GetShaderStage(), usePushDescriptor, "auto_descriptor_" + m_shader->GetID());
            descriptors.push_back(descriptor);
        }

        return descriptors;
    }
}
