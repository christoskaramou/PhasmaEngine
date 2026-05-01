#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/AccelerationStructure.h"
#include "API/Debug.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanSamplerImpl.h"

namespace pe
{
    vk::DescriptorType ToVkDescriptorType(PeBindingType type)
    {
        switch (type)
        {
        case PE_DESCRIPTOR_TYPE_SAMPLER:
            return vk::DescriptorType::eSampler;
        case PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return vk::DescriptorType::eCombinedImageSampler;
        case PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return vk::DescriptorType::eSampledImage;
        case PE_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return vk::DescriptorType::eStorageImage;
        case PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return vk::DescriptorType::eStorageTexelBuffer;
        case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return vk::DescriptorType::eUniformBuffer;
        case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return vk::DescriptorType::eStorageBuffer;
        case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return vk::DescriptorType::eUniformBufferDynamic;
        case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return vk::DescriptorType::eStorageBufferDynamic;
        case PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return vk::DescriptorType::eInputAttachment;
        case PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
            return vk::DescriptorType::eAccelerationStructureKHR;
        default:
            return vk::DescriptorType::eCombinedImageSampler;
        }
    }

    PeBindingType FromVkDescriptorType(vk::DescriptorType type)
    {
        switch (type)
        {
        case vk::DescriptorType::eSampler:
            return PE_DESCRIPTOR_TYPE_SAMPLER;
        case vk::DescriptorType::eCombinedImageSampler:
            return PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case vk::DescriptorType::eSampledImage:
            return PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case vk::DescriptorType::eStorageImage:
            return PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case vk::DescriptorType::eStorageTexelBuffer:
            return PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case vk::DescriptorType::eUniformBuffer:
            return PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case vk::DescriptorType::eStorageBuffer:
            return PE_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case vk::DescriptorType::eUniformBufferDynamic:
            return PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case vk::DescriptorType::eStorageBufferDynamic:
            return PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case vk::DescriptorType::eInputAttachment:
            return PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case vk::DescriptorType::eAccelerationStructureKHR:
            return PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE;
        default:
            return PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }
    }

    vk::ShaderStageFlags ToVkShaderStageFlags(PeShaderStageFlags stages)
    {
        if (stages == PE_SHADER_STAGE_ALL)
            return vk::ShaderStageFlagBits::eAll;

        vk::ShaderStageFlags flags{};
        if (stages & PE_SHADER_STAGE_VERTEX)
            flags |= vk::ShaderStageFlagBits::eVertex;
        if (stages & PE_SHADER_STAGE_TESS_CONTROL)
            flags |= vk::ShaderStageFlagBits::eTessellationControl;
        if (stages & PE_SHADER_STAGE_TESS_EVALUATION)
            flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;
        if (stages & PE_SHADER_STAGE_GEOMETRY)
            flags |= vk::ShaderStageFlagBits::eGeometry;
        if (stages & PE_SHADER_STAGE_FRAGMENT)
            flags |= vk::ShaderStageFlagBits::eFragment;
        if (stages & PE_SHADER_STAGE_COMPUTE)
            flags |= vk::ShaderStageFlagBits::eCompute;
        if (stages & PE_SHADER_STAGE_TASK_EXT)
            flags |= vk::ShaderStageFlagBits::eTaskEXT;
        if (stages & PE_SHADER_STAGE_MESH_EXT)
            flags |= vk::ShaderStageFlagBits::eMeshEXT;
        if (stages & PE_SHADER_STAGE_RAYGEN_KHR)
            flags |= vk::ShaderStageFlagBits::eRaygenKHR;
        if (stages & PE_SHADER_STAGE_ANY_HIT_KHR)
            flags |= vk::ShaderStageFlagBits::eAnyHitKHR;
        if (stages & PE_SHADER_STAGE_CLOSEST_HIT_KHR)
            flags |= vk::ShaderStageFlagBits::eClosestHitKHR;
        if (stages & PE_SHADER_STAGE_MISS_KHR)
            flags |= vk::ShaderStageFlagBits::eMissKHR;
        if (stages & PE_SHADER_STAGE_INTERSECTION_KHR)
            flags |= vk::ShaderStageFlagBits::eIntersectionKHR;
        if (stages & PE_SHADER_STAGE_CALLABLE_KHR)
            flags |= vk::ShaderStageFlagBits::eCallableKHR;
        return flags;
    }

    PeShaderStageFlags FromVkShaderStageFlags(vk::ShaderStageFlags stages)
    {
        VkShaderStageFlags raw = static_cast<VkShaderStageFlags>(stages);
        if (raw == VK_SHADER_STAGE_ALL)
            return PE_SHADER_STAGE_ALL;

        PeShaderStageFlags flags{};
        if (stages & vk::ShaderStageFlagBits::eVertex)
            flags |= PE_SHADER_STAGE_VERTEX;
        if (stages & vk::ShaderStageFlagBits::eTessellationControl)
            flags |= PE_SHADER_STAGE_TESS_CONTROL;
        if (stages & vk::ShaderStageFlagBits::eTessellationEvaluation)
            flags |= PE_SHADER_STAGE_TESS_EVALUATION;
        if (stages & vk::ShaderStageFlagBits::eGeometry)
            flags |= PE_SHADER_STAGE_GEOMETRY;
        if (stages & vk::ShaderStageFlagBits::eFragment)
            flags |= PE_SHADER_STAGE_FRAGMENT;
        if (stages & vk::ShaderStageFlagBits::eCompute)
            flags |= PE_SHADER_STAGE_COMPUTE;
        if (stages & vk::ShaderStageFlagBits::eTaskEXT)
            flags |= PE_SHADER_STAGE_TASK_EXT;
        if (stages & vk::ShaderStageFlagBits::eMeshEXT)
            flags |= PE_SHADER_STAGE_MESH_EXT;
        if (stages & vk::ShaderStageFlagBits::eRaygenKHR)
            flags |= PE_SHADER_STAGE_RAYGEN_KHR;
        if (stages & vk::ShaderStageFlagBits::eAnyHitKHR)
            flags |= PE_SHADER_STAGE_ANY_HIT_KHR;
        if (stages & vk::ShaderStageFlagBits::eClosestHitKHR)
            flags |= PE_SHADER_STAGE_CLOSEST_HIT_KHR;
        if (stages & vk::ShaderStageFlagBits::eMissKHR)
            flags |= PE_SHADER_STAGE_MISS_KHR;
        if (stages & vk::ShaderStageFlagBits::eIntersectionKHR)
            flags |= PE_SHADER_STAGE_INTERSECTION_KHR;
        if (stages & vk::ShaderStageFlagBits::eCallableKHR)
            flags |= PE_SHADER_STAGE_CALLABLE_KHR;
        return flags;
    }

    VulkanDescriptorPoolImpl::VulkanDescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc)
        : m_owner{owner}
    {
        std::vector<vk::DescriptorPoolSize> sizes(desc.sizes.size());
        for (size_t i = 0; i < desc.sizes.size(); i++)
        {
            sizes[i].type = ToVkDescriptorType(desc.sizes[i].type);
            sizes[i].descriptorCount = desc.sizes[i].descriptorCount;
        }

        vk::DescriptorPoolCreateInfo createInfo{};
        createInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        if (desc.updateAfterBind)
            createInfo.flags |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        createInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        createInfo.pPoolSizes = sizes.data();
        createInfo.maxSets = std::max(1u, desc.maxSets);

        m_pool = RHII.GetDevice().createDescriptorPool(createInfo);

        Debug::SetObjectName(m_pool, owner->m_name);
    }

    VulkanDescriptorPoolImpl::~VulkanDescriptorPoolImpl()
    {
        if (m_pool)
            RHII.GetDevice().destroyDescriptorPool(m_pool);
    }

    VulkanDescriptorLayoutImpl::VulkanDescriptorLayoutImpl(DescriptorLayout *owner)
        : m_owner{owner}
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings{};
        for (auto const &bindingInfo : owner->m_bindingInfos)
        {
            vk::DescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = bindingInfo.binding;
            layoutBinding.descriptorType = ToVkDescriptorType(bindingInfo.type);
            layoutBinding.descriptorCount = bindingInfo.count;
            layoutBinding.stageFlags = ToVkShaderStageFlags(owner->m_stage);
            layoutBinding.pImmutableSamplers = nullptr;

            bindings.push_back(layoutBinding);
        }

        std::vector<vk::DescriptorBindingFlags> bindingFlags(bindings.size());
        for (int i = 0; i < bindings.size(); i++)
        {
            if (owner->m_allowUpdateAfterBind && !owner->m_pushDescriptor)
                bindingFlags[i] |= vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound;

            if (owner->m_bindingInfos[i].bindless)
                bindingFlags[i] |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
        }

        vk::DescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlags{};
        layoutBindingFlags.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        layoutBindingFlags.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo dslci{};
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings = bindings.data();
        dslci.pNext = &layoutBindingFlags;
        if (owner->m_pushDescriptor)
            dslci.flags |= vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor;
        else if (owner->m_allowUpdateAfterBind)
            dslci.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

        m_layout = RHII.GetDevice().createDescriptorSetLayout(dslci);

        Debug::SetObjectName(m_layout, owner->m_name);
    }

    VulkanDescriptorLayoutImpl::~VulkanDescriptorLayoutImpl()
    {
        if (m_layout)
            RHII.GetDevice().destroyDescriptorSetLayout(m_layout);
    }

    VulkanDescriptorImpl::VulkanDescriptorImpl(Descriptor *owner)
        : m_owner{owner}
    {
        uint32_t variableDescCounts[] = {owner->m_layout->GetVariableCount()};

        vk::DescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo = {};
        variableDescriptorCountAllocInfo.descriptorSetCount = 1;
        variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

        vk::DescriptorSetLayout dsetLayout = GetVulkanDescriptorLayout(owner->m_layout);
        vk::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = GetVulkanDescriptorPool(owner->m_pool);
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &dsetLayout;
        allocateInfo.pNext = &variableDescriptorCountAllocInfo; // If the flag was not set in the layout, this will be ignored

        m_set = RHII.GetDevice().allocateDescriptorSets(allocateInfo)[0];

        Debug::SetObjectName(m_set, owner->m_name);
    }

    void VulkanDescriptorImpl::Update(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                      const std::vector<DescriptorUpdateInfo> &updateInfos)
    {
        std::vector<vk::WriteDescriptorSet> writes{};
        std::vector<std::vector<vk::DescriptorImageInfo>> imageInfos{};
        std::vector<std::vector<vk::DescriptorBufferInfo>> bufferInfos{};
        std::vector<std::vector<vk::AccelerationStructureKHR>> accelerationInfos{};
        std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> accelerationWrites{};
        imageInfos.reserve(updateInfos.size());
        bufferInfos.reserve(updateInfos.size());
        accelerationInfos.reserve(updateInfos.size());
        accelerationWrites.reserve(updateInfos.size());
        writes.reserve(updateInfos.size());

        for (uint32_t i = 0; i < updateInfos.size(); i++)
        {
            auto &updateInfo = updateInfos[i];
            auto &bindingInfo = bindingInfos[i];

            vk::WriteDescriptorSet write{};
            write.dstSet = m_set;
            write.dstBinding = updateInfo.binding;
            write.dstArrayElement = 0;
            write.descriptorType = ToVkDescriptorType(bindingInfo.type);
            if (updateInfo.views.size())
            {
                auto &infos = imageInfos.emplace_back(std::vector<vk::DescriptorImageInfo>{});
                infos.resize(updateInfo.views.size());
                for (uint32_t j = 0; j < updateInfo.views.size(); j++)
                {
                    infos[j] = vk::DescriptorImageInfo{};
                    infos[j].imageView = pe::GetVulkanImageView(updateInfo.views[j]);
                    infos[j].imageLayout = ToVkImageLayout(bindingInfo.imageLayout);
                    infos[j].sampler = bindingInfo.type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && j < updateInfo.samplers.size() ? pe::GetVulkanSampler(updateInfo.samplers[j]) : vk::Sampler{};
                }

                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pImageInfo = infos.data();
            }
            else if (updateInfo.buffers.size())
            {
                auto &infos = bufferInfos.emplace_back(std::vector<vk::DescriptorBufferInfo>{});
                infos.resize(updateInfo.buffers.size());
                for (uint32_t j = 0; j < updateInfo.buffers.size(); j++)
                {
                    infos[j] = vk::DescriptorBufferInfo{};
                    infos[j].buffer = pe::GetVulkanBuffer(updateInfo.buffers[j]);
                    infos[j].offset = updateInfo.offsets.size() > 0 ? updateInfo.offsets[j] : 0;
                    infos[j].range = updateInfo.ranges.size() > 0 ? (updateInfo.ranges[j] > 0 ? updateInfo.ranges[j] : vk::WholeSize) : vk::WholeSize;
                }

                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pBufferInfo = infos.data();
            }
            else if (updateInfo.samplers.size() && bindingInfo.type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                auto &infos = imageInfos.emplace_back(std::vector<vk::DescriptorImageInfo>{});
                infos.resize(updateInfo.samplers.size());
                for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                {
                    infos[j] = vk::DescriptorImageInfo{};
                    infos[j].imageView = nullptr;
                    infos[j].imageLayout = vk::ImageLayout::eUndefined;
                    infos[j].sampler = pe::GetVulkanSampler(updateInfo.samplers[j]);
                }

                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pImageInfo = infos.data();
            }
            else if (updateInfo.accelerationStructures.size())
            {
                auto &accelerationStructures = accelerationInfos.emplace_back(std::vector<vk::AccelerationStructureKHR>{});
                accelerationStructures.reserve(updateInfo.accelerationStructures.size());
                for (AccelerationStructure *as : updateInfo.accelerationStructures)
                    accelerationStructures.push_back(as ? as->ApiHandle() : vk::AccelerationStructureKHR{});

                auto &accelerationWrite = accelerationWrites.emplace_back();
                accelerationWrite.accelerationStructureCount = static_cast<uint32_t>(accelerationStructures.size());
                accelerationWrite.pAccelerationStructures = accelerationStructures.data();

                write.pNext = &accelerationWrite;
                write.descriptorCount = accelerationWrite.accelerationStructureCount;
            }
            else
            {
                continue; // We are allowing this since all bindings can be partially bound
            }

            writes.push_back(write);
        }

        RHII.GetDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    DescriptorPool::Impl *CreateDescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc)
    {
        return new VulkanDescriptorPoolImpl(owner, desc);
    }

    DescriptorLayout::Impl *CreateDescriptorLayoutImpl(DescriptorLayout *owner)
    {
        return new VulkanDescriptorLayoutImpl(owner);
    }

    Descriptor::Impl *CreateDescriptorImpl(Descriptor *owner)
    {
        return new VulkanDescriptorImpl(owner);
    }
} // namespace pe
