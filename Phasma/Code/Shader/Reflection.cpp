#include "Reflection.h"
#include "Shader.h"
#include "spirv_cross/spirv_cross.hpp"
#include "Renderer/Vertex.h"

namespace pe
{
    ShaderInOutDesc::ShaderInOutDesc()
    {
        location = -1;
    }

    CombinedImageSamplerDesc::CombinedImageSamplerDesc()
    {
        set = -1;
        binding = -1;
    }

    BufferDesc::BufferDesc()
    {
        set = -1;
        binding = -1;
        bufferSize = 0;
    }

    PushConstantDesc::PushConstantDesc()
    {
        size = 0;
        offset = 0;
    }

    void Reflection::Init(Shader *shader)
    {
        m_shader = shader;

        spirv_cross::Compiler compiler{shader->GetSpriv(), shader->Size()};
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        auto active = compiler.get_active_interface_variables();
        compiler.set_enabled_interface_variables(std::move(active));

        // Shader inputs
        for (const spirv_cross::Resource &resource : resources.stage_inputs)
        {
            ShaderInOutDesc desc;
            desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));

            inputs.push_back(desc);
        }
        std::sort(inputs.begin(), inputs.end(), [](auto &a, auto &b)
                  { return a.location < b.location; });

        // Shader outputs
        for (const spirv_cross::Resource &resource : resources.stage_outputs)
        {
            ShaderInOutDesc desc;
            desc.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));

            outputs.push_back(desc);
        }
        std::sort(outputs.begin(), outputs.end(), [](auto &a, auto &b)
                  { return a.location < b.location; });

        // Combined image samplers
        for (const spirv_cross::Resource &resource : resources.sampled_images)
        {
            CombinedImageSamplerDesc desc;
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

            samplers.push_back(desc);
        }

        // Uniform buffers
        for (const spirv_cross::Resource &resource : resources.uniform_buffers)
        {
            BufferDesc desc;
            desc.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            desc.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));
            desc.bufferSize = compiler.get_declared_struct_size(*desc.type);

            uniformBuffers.push_back(desc);
        }

        // Push constants
        for (const spirv_cross::Resource &resource : resources.push_constant_buffers)
        {
            PushConstantDesc desc;
            desc.offset = compiler.get_decoration(resource.id, spv::DecorationOffset);
            desc.type = std::make_shared<spirv_cross::SPIRType>(compiler.get_type(resource.base_type_id));
            desc.size = compiler.get_declared_struct_size(*desc.type);
            pushConstantBuffers.push_back(desc);
        }
    }

    uint32_t GetTypeSize(const ShaderInOutDesc &inout)
    {
        switch (inout.type->basetype)
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
        switch (inout.type->basetype)
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
        uint32_t size = GetTypeSize(input) * input.type->vecsize * input.type->columns;
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
        uint32_t stride = 0;
        for (const ShaderInOutDesc &input : inputs)
            stride += GetTypeSize(input) * input.type->vecsize * input.type->columns;

        VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = stride;
        binding.inputRate = VertexInputRate::Vertex;

        return {binding};
    }

    std::vector<VertexInputAttributeDescription> Reflection::GetVertexAttributes()
    {
        std::vector<VertexInputAttributeDescription> attributes;

        uint32_t offset = 0;
        for (const ShaderInOutDesc &input : inputs)
        {
            uint32_t size = GetTypeSize(input) * input.type->vecsize * input.type->columns;
            VertexInputAttributeDescription attribute;
            attribute.location = input.location;
            attribute.binding = 0;
            attribute.format = GetAttibuteType(input);
            attribute.offset = offset;
            offset += size;

            attributes.push_back(attribute);
        }

        return attributes;
    }
}
