#if PE_VULKAN
#include "Renderer/Descriptor.h"
#include "Renderer/RHI.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"

namespace pe
{
    DescriptorPool::DescriptorPool(uint32_t count, DescriptorPoolSize *sizes, const std::string &name)
    {
        uint32_t maxDescriptors = 0;
        std::vector<VkDescriptorPoolSize> descPoolsize(count);
        for (uint32_t i = 0; i < count; i++)
        {
            descPoolsize[i].type = Translate<VkDescriptorType>(sizes[i].type);
            descPoolsize[i].descriptorCount = sizes[i].descriptorCount;
            maxDescriptors += sizes[i].descriptorCount;
        }

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        createInfo.poolSizeCount = count;
        createInfo.pPoolSizes = descPoolsize.data();
        createInfo.maxSets = maxDescriptors;

        VkDescriptorPool descriptorPoolVK;
        PE_CHECK(vkCreateDescriptorPool(RHII.GetDevice(), &createInfo, nullptr, &descriptorPoolVK));
        m_handle = descriptorPoolVK;

        Debug::SetObjectName(m_handle, ObjectType::DescriptorPool, name);
    }

    DescriptorPool::~DescriptorPool()
    {
        if (m_handle)
        {
            vkDestroyDescriptorPool(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }

    DescriptorLayout::DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                       ShaderStage stage,
                                       const std::string &name)
    {
        // Bindings
        std::vector<VkDescriptorSetLayoutBinding> bindingsVK{};
        for (auto const &bindingInfo : bindingInfos)
        {
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = bindingInfo.binding;
            layoutBinding.descriptorType = Translate<VkDescriptorType>(bindingInfo.type);
            layoutBinding.descriptorCount = bindingInfo.count;
            layoutBinding.stageFlags = Translate<VkShaderStageFlags>(stage);
            layoutBinding.pImmutableSamplers = nullptr;

            bindingsVK.push_back(layoutBinding);
        }

        m_variableCount = 1;

        bool allowUpdateAfterBind = true;
        for (int i = 0; i < bindingsVK.size(); i++)
        {
            if (bindingsVK[i].descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
                bindingsVK[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                bindingsVK[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
            {
                allowUpdateAfterBind = false;
                break;
            }
        }

        // Bindings flags
        std::vector<VkDescriptorBindingFlags> bindingFlags(bindingsVK.size());
        for (int i = 0; i < bindingsVK.size(); i++)
        {
            bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

            if (allowUpdateAfterBind)
                bindingFlags[i] |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

            if (bindingInfos[i].bindless)
            {
                // Note: As for now, there can be only one unbound array in a descriptor set and it must be the last binding
                // I think this is a Vulkan limitation
                if (i != bindingInfos.size() - 1)
                    PE_ERROR("DescriptorLayout: An unbound array must be the last binding");

                bindingFlags[i] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                m_variableCount = bindingsVK[i].descriptorCount;
            }
        }

        // Set flags to bindings
        VkDescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlags{};
        layoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        layoutBindingFlags.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        layoutBindingFlags.pBindingFlags = bindingFlags.data();

        // Layout create info
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindingsVK.size());
        dslci.pBindings = bindingsVK.data();
        dslci.pNext = &layoutBindingFlags;
        if (allowUpdateAfterBind)
            dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        VkDescriptorSetLayout layout;
        PE_CHECK(vkCreateDescriptorSetLayout(RHII.GetDevice(), &dslci, nullptr, &layout));
        m_handle = layout;

        Debug::SetObjectName(m_handle, ObjectType::DescriptorSetLayout, name);
    }

    DescriptorLayout::~DescriptorLayout()
    {
        if (m_handle)
        {
            vkDestroyDescriptorSetLayout(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }

    Descriptor::Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                           ShaderStage stage,
                           const std::string &name)
    {
        m_bindingInfoMap = {};
        m_updateInfoMap = {};

        for (const auto &bindingInfo : bindingInfos)
        {
            auto it = m_bindingInfoMap.find(bindingInfo.binding);
            if (it != m_bindingInfoMap.end())
                PE_ERROR("Binding has already beed added");

            m_bindingInfoMap[bindingInfo.binding] = bindingInfo;
        }

        uint32_t size = static_cast<uint32_t>(bindingInfos.size());

        // Get the count per descriptor type
        std::unordered_map<DescriptorType, uint32_t> typeCounts{};
        for (uint32_t i = 0; i < size; i++)
        {
            if (typeCounts.find(bindingInfos[i].type) == typeCounts.end())
                typeCounts[bindingInfos[i].type] = 0;

            typeCounts[bindingInfos[i].type] += bindingInfos[i].count;
        }

        // Create pool sizes typeCounts above
        std::vector<DescriptorPoolSize> poolSizes(typeCounts.size());
        uint32_t i = 0;
        for (auto const &[type, count] : typeCounts)
        {
            poolSizes[i].type = type;
            poolSizes[i].descriptorCount = count;
            i++;
        }

        m_pool = DescriptorPool::Create(i, poolSizes.data(), name + "_pool");
        m_layout = DescriptorLayout::Create(bindingInfos, stage, name + "_layout");

        // DescriptorLayout calculates the variable count on creation
        uint32_t variableDescCounts[] = {m_layout->GetVariableCount()};

        VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo = {};
        variableDescriptorCountAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        variableDescriptorCountAllocInfo.descriptorSetCount = 1;
        variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

        VkDescriptorSetLayout dsetLayout = m_layout->Handle();
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_pool->Handle();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &dsetLayout;
        allocateInfo.pNext = &variableDescriptorCountAllocInfo; // If not flag has been set in the layout, this will be ignored

        VkDescriptorSet dset;
        PE_CHECK(vkAllocateDescriptorSets(RHII.GetDevice(), &allocateInfo, &dset));
        m_handle = dset;

        Debug::SetObjectName(m_handle, ObjectType::DescriptorSet, name);
    }

    Descriptor::~Descriptor()
    {
        DescriptorPool::Destroy(m_pool);
        DescriptorLayout::Destroy(m_layout);
    }

    void Descriptor::SetImages(uint32_t binding,
                               const std::vector<ImageViewHandle> &views,
                               const std::vector<SamplerHandle> &samplers)
    {
        auto it = m_updateInfoMap.find(binding);
        if (it != m_updateInfoMap.end())
        {
            it->second.views = views;
            it->second.samplers = samplers;
        }
        else
        {
            DescriptorUpdateInfo info{};
            info.binding = binding;
            info.views = views;
            info.samplers = samplers;
            m_updateInfoMap[binding] = info;
        }
    }

    void Descriptor::SetImage(uint32_t binding, ImageViewHandle view, SamplerHandle sampler)
    {
        SetImages(binding, {view}, {sampler});
    }

    void Descriptor::SetBuffers(uint32_t binding, const std::vector<Buffer *> &buffers)
    {
        auto it = m_updateInfoMap.find(binding);
        if (it != m_updateInfoMap.end())
        {
            it->second.buffers = buffers;
        }
        else
        {
            DescriptorUpdateInfo info{};
            info.binding = binding;
            info.buffers = buffers;
            m_updateInfoMap[binding] = info;
        }
    }

    void Descriptor::SetBuffer(uint32_t binding, Buffer *buffer)
    {
        SetBuffers(binding, {buffer});
    }

    void Descriptor::SetSampler(uint32_t binding, SamplerHandle sampler)
    {
        auto it = m_updateInfoMap.find(binding);
        if (it != m_updateInfoMap.end())
        {
            it->second.sampler = sampler;
        }
        else
        {
            DescriptorUpdateInfo info{};
            info.binding = binding;
            info.sampler = sampler;
            m_updateInfoMap[binding] = info;
        }
    }

    void Descriptor::UpdateDescriptor()
    {
        for (auto it = m_updateInfoMap.begin(); it != m_updateInfoMap.end(); it++)
        {
            DescriptorUpdateInfo &updateInfo = it->second;
            DescriptorBindingInfo &bindingInfo = m_bindingInfoMap[it->first];

            uint32_t typeCount = 0;
            typeCount += !updateInfo.sampler.IsNull() ? 1 : 0;
            typeCount += updateInfo.views.size() ? 1 : 0;
            typeCount += updateInfo.buffers.size() ? 1 : 0;
            if (typeCount != 1)
                PE_ERROR("DescriptorUpdateInfo type count error");

            std::vector<VkDescriptorImageInfo> imageInfoVK{};
            std::vector<VkDescriptorBufferInfo> bufferInfoVK{};

            if (updateInfo.sampler)
            {
                imageInfoVK.push_back(
                    VkDescriptorImageInfo{
                        updateInfo.sampler,
                        nullptr,
                        VK_IMAGE_LAYOUT_UNDEFINED});
            }

            for (uint32_t i = 0; i < updateInfo.views.size(); i++)
            {
                SamplerHandle sampler = updateInfo.samplers.size() > 0 ? updateInfo.samplers[i] : SamplerHandle{};
                imageInfoVK.push_back(
                    VkDescriptorImageInfo{
                        sampler,
                        updateInfo.views[i],
                        Translate<VkImageLayout>(bindingInfo.imageLayout)});
            }

            for (auto *buffer : updateInfo.buffers)
            {
                size_t range = buffer->Size();
                if (bindingInfo.type == DescriptorType::UniformBufferDynamic ||
                    bindingInfo.type == DescriptorType::StorageBufferDynamic)
                    range /= SWAPCHAIN_IMAGES;

                bufferInfoVK.push_back(
                    VkDescriptorBufferInfo{
                        buffer->Handle(),
                        0,
                        range});
            }

            VkWriteDescriptorSet writeSet{};
            writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeSet.dstSet = m_handle;
            writeSet.dstBinding = bindingInfo.binding;
            writeSet.dstArrayElement = 0;
            writeSet.descriptorCount = static_cast<uint32_t>(max(imageInfoVK.size(), bufferInfoVK.size()));
            writeSet.descriptorType = Translate<VkDescriptorType>(bindingInfo.type);
            writeSet.pImageInfo = imageInfoVK.size() > 0 ? imageInfoVK.data() : nullptr;
            writeSet.pBufferInfo = bufferInfoVK.size() > 0 ? bufferInfoVK.data() : nullptr;

            vkUpdateDescriptorSets(RHII.GetDevice(), 1, &writeSet, 0, nullptr);
        }

        // Calculate frame dynamic offsets
        m_frameDynamicOffsets = {};
        for (uint32_t j = 0; j < SWAPCHAIN_IMAGES; j++)
        {
            for (auto it = m_updateInfoMap.begin(); it != m_updateInfoMap.end(); it++)
            {
                const DescriptorUpdateInfo &updateInfo = it->second;
                const DescriptorBindingInfo &bindingInfo = m_bindingInfoMap[it->first];

                if (bindingInfo.type == DescriptorType::UniformBufferDynamic ||
                    bindingInfo.type == DescriptorType::StorageBufferDynamic)
                {
                    if (updateInfo.buffers.size() > 1)
                        PE_ERROR("Cannot have more than one dynamic buffer in a binding");

                    size_t bufferSize = updateInfo.buffers[0]->Size();
                    m_frameDynamicOffsets.push_back(RHII.GetFrameDynamicOffset(bufferSize, j));
                }
            }
        }

        m_updateInfoMap.clear();
    }

    void Descriptor::GetFrameDynamicOffsets(uint32_t *count, uint32_t **offsets)
    {
        // The count of frame dynamic offsets should be at least SWAPCHAIN_IMAGES count
        if (m_frameDynamicOffsets.size() >= SWAPCHAIN_IMAGES)
        {
            *count = static_cast<uint32_t>(m_frameDynamicOffsets.size() / SWAPCHAIN_IMAGES);
            *offsets = &m_frameDynamicOffsets[(*count) * RHII.GetFrameIndex()];
        }
        else
        {
            *count = 0;
            *offsets = nullptr;
        }
    }

    std::vector<uint32_t> Descriptor::GetAllFrameDynamicOffsets(uint32_t count, Descriptor **descriptors)
    {
        std::vector<uint32_t> dynamicOffsets{};

        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t count;
            uint32_t *offsets;
            descriptors[i]->GetFrameDynamicOffsets(&count, &offsets);
            if (count > 0)
                dynamicOffsets.insert(dynamicOffsets.end(), offsets, offsets + count);
        }

        return dynamicOffsets;
    }
}
#endif
