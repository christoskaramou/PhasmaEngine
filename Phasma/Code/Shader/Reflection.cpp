/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

    enum class VariableType
    {
        None,
        SInt,
        UInt,
        SFloat
    };

    VariableType GetVariableType(const ShaderInOutDesc &inout)
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
            return VariableType::None;
        case spirv_cross::SPIRType::Boolean:
        case spirv_cross::SPIRType::UByte:
        case spirv_cross::SPIRType::UShort:
        case spirv_cross::SPIRType::UInt:
        case spirv_cross::SPIRType::UInt64:
            return VariableType::UInt;
        case spirv_cross::SPIRType::SByte:
        case spirv_cross::SPIRType::Short:
        case spirv_cross::SPIRType::Int:
        case spirv_cross::SPIRType::Int64:
            return VariableType::SInt;
        case spirv_cross::SPIRType::Half:
        case spirv_cross::SPIRType::Float:
        case spirv_cross::SPIRType::Double:
            return VariableType::SFloat;
        default:
            return VariableType::None;
        }
    }

    uint32_t GetAttibuteType(const ShaderInOutDesc &input)
    {
        uint32_t size = GetTypeSize(input) * input.type->vecsize * input.type->columns;
        VariableType type = GetVariableType(input);
        switch (size)
        {
        case 1:
            if (type == VariableType::SInt)
                return VK_FORMAT_R8_SINT;
            else if (type == VariableType::UInt)
                return VK_FORMAT_R8_UINT;

        case 2:
            if (type == VariableType::SInt)
                return VK_FORMAT_R16_SINT;
            else if (type == VariableType::UInt)
                return VK_FORMAT_R16_UINT;
            else if (type == VariableType::SFloat)
                return VK_FORMAT_R16_SFLOAT;

        case 4:
            if (type == VariableType::SInt)
                return VK_FORMAT_R32_SINT;
            else if (type == VariableType::UInt)
                return VK_FORMAT_R32_UINT;
            else if (type == VariableType::SFloat)
                return VK_FORMAT_R32_SFLOAT;

        case 6:
            if (type == VariableType::SInt)
                VK_FORMAT_R16G16B16_SINT;
            else if (type == VariableType::UInt)
                VK_FORMAT_R16G16B16_UINT;
            else if (type == VariableType::SFloat)
                return VK_FORMAT_R16G16B16_SFLOAT;

        case 8:
            if (type == VariableType::SInt)
                return VK_FORMAT_R32G32_SINT;
            else if (type == VariableType::UInt)
                return VK_FORMAT_R32G32_UINT;
            else if (type == VariableType::SFloat)
                return VK_FORMAT_R32G32_SFLOAT;

        case 12:
            if (type == VariableType::SInt)
                return VK_FORMAT_R32G32B32_SINT;
            else if (type == VariableType::UInt)
                return VK_FORMAT_R32G32B32_UINT;
            else if (type == VariableType::SFloat)
                return VK_FORMAT_R32G32B32_SFLOAT;

        case 16:
            if (type == VariableType::SInt)
                return VK_FORMAT_R32G32B32A32_SINT;
            else if (type == VariableType::UInt)
                return VK_FORMAT_R32G32B32A32_UINT;
            else if (type == VariableType::SFloat)
                return VK_FORMAT_R32G32B32A32_SFLOAT;

        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    std::vector<VertexInputBindingDescription> Reflection::GetVertexBindings()
    {
        uint32_t stride = 0;
        for (const ShaderInOutDesc &input : inputs)
            stride += GetTypeSize(input) * input.type->vecsize * input.type->columns;

        VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = stride;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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
