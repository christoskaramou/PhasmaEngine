#include "API/Reflection.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanReflection.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12ShaderImpl.h"
#endif

namespace pe
{
    namespace
    {
        void PopulateReflectionForBackend(Reflection &reflection, Shader *shader)
        {
            const PeGraphicsApi api = RHII.GetApi();
            switch (api)
            {
            case PE_GRAPHICS_API_VULKAN:
                PopulateReflectionFromSpirv(reflection, shader);
                return;
            case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
                PopulateReflectionFromDxil(reflection, shader);
#else
                PE_ERROR("PopulateReflectionForBackend: DX12 reflection is Windows-only");
#endif
                return;
            default:
                PE_ERROR("PopulateReflectionForBackend: unsupported graphics api %u", static_cast<uint32_t>(api));
                return;
            }
        }

        template <typename T>
        void FillDxRegisterInfo(DescriptorBindingInfo &info, const T &desc)
        {
            info.dxRegister = desc.dxRegister == INT32_MIN ? static_cast<uint32_t>(desc.binding) : static_cast<uint32_t>(desc.dxRegister);
            info.dxSpace = desc.dxSpace == INT32_MIN ? static_cast<uint32_t>(desc.set) : static_cast<uint32_t>(desc.dxSpace);
        }
    } // namespace

    void Reflection::Init(Shader *shader)
    {
        PopulateReflectionForBackend(*this, shader);
    }

    std::vector<VertexInputBinding> Reflection::GetVertexBindings() const
    {
        PE_ERROR_IF(m_shader->GetShaderStage() != PE_SHADER_STAGE_VERTEX,
                    "Vertex bindings are only available for vertex shaders");

        std::unordered_map<uint32_t, std::vector<const ShaderInOutDesc *>> bindingsMap{};
        for (const ShaderInOutDesc &input : m_inputs)
        {
            if (input.binding == INT32_MIN)
                continue;
            bindingsMap[input.binding].push_back(&input);
        }

        for (auto &pair : bindingsMap)
        {
            std::sort(pair.second.begin(), pair.second.end(),
                      [](const ShaderInOutDesc *a, const ShaderInOutDesc *b)
                      { return a->location < b->location; });
        }

        std::vector<VertexInputBinding> bindings;
        bindings.reserve(bindingsMap.size());
        for (const auto &pair : bindingsMap)
        {
            VertexInputBinding b{};
            b.binding = pair.first;
            for (const ShaderInOutDesc *input : pair.second)
                b.stride += input->size;
            bindings.push_back(b);
        }
        return bindings;
    }

    std::vector<VertexInputAttribute> Reflection::GetVertexAttributes() const
    {
        PE_ERROR_IF(m_shader->GetShaderStage() != PE_SHADER_STAGE_VERTEX,
                    "Vertex attributes are only available for vertex shaders");

        std::unordered_map<uint32_t, uint32_t> bindingOffsets{};

        std::vector<VertexInputAttribute> attributes;
        attributes.reserve(m_inputs.size());

        for (const ShaderInOutDesc &input : m_inputs)
        {
            if (input.binding == INT32_MIN)
                continue;

            uint32_t &offset = bindingOffsets.try_emplace(input.binding, 0).first->second;

            VertexInputAttribute attr{};
            attr.location = static_cast<uint32_t>(input.location);
            attr.binding = static_cast<uint32_t>(input.binding);
            attr.format = input.format;
            attr.offset = offset;
            offset += input.size;

            attributes.push_back(attr);
        }
        return attributes;
    }

    std::vector<Descriptor *> Reflection::GetDescriptors() const
    {
        int maxSet = INT32_MIN;
        for (const CombinedImageSamplerReflection &desc : m_combinedImageSamplers)
            maxSet = std::max(maxSet, desc.set);
        for (const SamplerReflection &desc : m_samplers)
            maxSet = std::max(maxSet, desc.set);
        for (const ImageReflection &desc : m_images)
            maxSet = std::max(maxSet, desc.set);
        for (const ImageReflection &desc : m_storageImages)
            maxSet = std::max(maxSet, desc.set);
        for (const BufferReflection &desc : m_uniformBuffers)
            maxSet = std::max(maxSet, desc.set);
        for (const BufferReflection &desc : m_storageBuffers)
            maxSet = std::max(maxSet, desc.set);
        for (const AccelerationStructureReflection &desc : m_accelerationStructures)
            maxSet = std::max(maxSet, desc.set);

        if (maxSet == INT32_MIN)
            return {};

        std::vector<std::vector<DescriptorBindingInfo>> setInfos{};
        setInfos.resize(maxSet + 1);

        std::vector<Descriptor *> descriptors{};
        descriptors.reserve(maxSet + 1);

        for (const CombinedImageSamplerReflection &desc : m_combinedImageSamplers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.imageLayout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            info.type = PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (const SamplerReflection &desc : m_samplers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.type = PE_DESCRIPTOR_TYPE_SAMPLER;
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (const ImageReflection &desc : m_images)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.imageLayout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            info.type = PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (const ImageReflection &desc : m_storageImages)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.imageLayout = PE_IMAGE_LAYOUT_GENERAL;
            info.type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (const BufferReflection &desc : m_uniformBuffers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.type = PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (const BufferReflection &desc : m_storageBuffers)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.structuredStride = desc.structuredStride;
            switch (desc.kind)
            {
            case PeBufferKind::Structured:
                info.type = PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER;
                break;
            case PeBufferKind::ByteAddress:
                info.type = PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER;
                break;
            case PeBufferKind::StorageRW:
            default:
                info.type = PE_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            }
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (const AccelerationStructureReflection &desc : m_accelerationStructures)
        {
            DescriptorBindingInfo info{};
            info.binding = desc.binding;
            info.count = desc.count;
            info.type = PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE;
            info.name = desc.name;
            FillDxRegisterInfo(info, desc);
            setInfos[desc.set].push_back(info);
        }

        for (auto &setInfo : setInfos)
        {
            Descriptor *descriptor = nullptr;

            if (!setInfo.empty())
            {
                std::sort(setInfo.begin(), setInfo.end(),
                          [](auto &a, auto &b)
                          { return a.binding < b.binding; });

                // SPIRV-Cross reflection may report both a CombinedImageSampler and a separate Sampler/Image for the same binding
                // when using HLSL [[vk::combinedImageSampler]] or -fspv-target-env=vulkan1.3.
                // Merge or deduplicate to avoid "duplicate binding" validation errors.
                for (size_t i = 0; i < setInfo.size() - 1;)
                {
                    if (setInfo[i].binding == setInfo[i + 1].binding)
                    {
                        auto &a = setInfo[i];
                        auto &b = setInfo[i + 1];

                        if (a.type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && b.type == PE_DESCRIPTOR_TYPE_SAMPLER)
                        {
                            setInfo.erase(setInfo.begin() + i + 1);
                            continue;
                        }
                        if (a.type == PE_DESCRIPTOR_TYPE_SAMPLER && b.type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                        {
                            setInfo.erase(setInfo.begin() + i);
                            continue;
                        }

                        if ((a.type == PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE && b.type == PE_DESCRIPTOR_TYPE_SAMPLER) ||
                            (a.type == PE_DESCRIPTOR_TYPE_SAMPLER && b.type == PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE))
                        {
                            PE_ERROR_IF(a.dxRegister != b.dxRegister || a.dxSpace != b.dxSpace,
                                        "Combined image/sampler binding has mismatched DX12 registers");
                            a.type = PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                            a.imageLayout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            setInfo.erase(setInfo.begin() + i + 1);
                            continue;
                        }
                    }
                    i++;
                }

                descriptor = Descriptor::Create(setInfo, m_shader->GetShaderStage(), false, "auto_descriptor");
            }

            descriptors.push_back(descriptor);
        }

        return descriptors;
    }
} // namespace pe
