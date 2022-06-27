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

    DescriptorLayout::DescriptorLayout(DescriptorInfo *info, const std::string &name)
    {
        // store by binding
        std::map<uint32_t, std::vector<DescriptorBindingInfo>> infosMap{};
        for (uint32_t i = 0; i < info->count; i++)
        {
            if (infosMap.find(info->bindingInfos[i].binding) == infosMap.end())
                infosMap[info->bindingInfos[i].binding] = std::vector<DescriptorBindingInfo>{};

            infosMap[info->bindingInfos[i].binding].push_back(info->bindingInfos[i]);
        }

        // Bindings
        std::vector<VkDescriptorSetLayoutBinding> bindings{};
        for (auto const &[binding, bindingInfos] : infosMap)
        {
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding;
            layoutBinding.descriptorType = Translate<VkDescriptorType>(bindingInfos[0].type);
            layoutBinding.descriptorCount = static_cast<uint32_t>(bindingInfos.size());
            layoutBinding.stageFlags = Translate<VkShaderStageFlags>(info->stage);
            layoutBinding.pImmutableSamplers = nullptr;

            bindings.push_back(layoutBinding);
        }

        m_variableCount = 1;

        // Bindings flags
        std::vector<VkDescriptorBindingFlags> bindingFlags(bindings.size());
        for (int i = 0; i < bindings.size(); i++)
        {
            if (bindings[i].descriptorCount > 1)
            {
                // Note: As for now, there can be only one unbound array in a descriptor set and it must be the last binding
                // I think this is a Vulkan limitation
                if (i != bindings.size() - 1)
                    PE_ERROR("DescriptorLayout: An unbound array must be the last binding");
                bindingFlags[i] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                m_variableCount = bindings[i].descriptorCount;
            }
            else
            {
                bindingFlags[i] = 0;
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
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings = bindings.data();
        dslci.pNext = &layoutBindingFlags;

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

    Descriptor::Descriptor(DescriptorInfo *info, const std::string &name)
    {
        m_bindingInfos = std::vector<DescriptorBindingInfo>(info->count);

        // Get the count per descriptor type
        std::unordered_map<DescriptorType, uint32_t> typeCounts{};
        for (uint32_t i = 0; i < info->count; i++)
        {
            m_bindingInfos[i] = info->bindingInfos[i];

            if (typeCounts.find(info->bindingInfos[i].type) == typeCounts.end())
                typeCounts[info->bindingInfos[i].type] = 0;

            typeCounts[info->bindingInfos[i].type]++;
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
        m_layout = DescriptorLayout::Create(info, name + "_layout");

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
        allocateInfo.pNext = &variableDescriptorCountAllocInfo;

        VkDescriptorSet dset;
        PE_CHECK(vkAllocateDescriptorSets(RHII.GetDevice(), &allocateInfo, &dset));
        m_handle = dset;

        UpdateDescriptor(info);

        // Pre-calculate frame dynamic offsets
        m_frameDynamicOffsets = {};
        for (uint32_t j = 0; j < SWAPCHAIN_IMAGES; j++)
        {
            for (size_t i = 0; i < m_bindingInfos.size(); i++)
            {
                if (m_bindingInfos[i].type == DescriptorType::UniformBufferDynamic ||
                    m_bindingInfos[i].type == DescriptorType::StorageBufferDynamic)
                {
                    size_t bufferSize = m_bindingInfos[i].pBuffer->Size();
                    m_frameDynamicOffsets.push_back(RHII.GetFrameDynamicOffset(bufferSize, j));
                }
            }
        }

        Debug::SetObjectName(m_handle, ObjectType::DescriptorSet, name);
    }

    Descriptor::~Descriptor()
    {
        DescriptorPool::Destroy(m_pool);
        DescriptorLayout::Destroy(m_layout);
    }

    void Descriptor::UpdateDescriptor(DescriptorInfo *info)
    {
        std::vector<VkDescriptorImageInfo> imageInfos{};
        imageInfos.reserve(info->count);
        std::vector<VkDescriptorBufferInfo> bufferInfos{};
        bufferInfos.reserve(info->count);

        // store by binding
        std::map<uint32_t, std::vector<DescriptorBindingInfo>> infosMap{};
        for (uint32_t i = 0; i < info->count; i++)
            infosMap[info->bindingInfos[i].binding].push_back(info->bindingInfos[i]);

        std::vector<VkWriteDescriptorSet> writeSets{};
        writeSets.reserve(infosMap.size());

        for (auto const &[binding, bindingInfos] : infosMap)
        {
            for (auto const &bindingInfo : bindingInfos)
            {
                if (bindingInfo.pImage != nullptr)
                {
                    imageInfos.push_back(
                        VkDescriptorImageInfo{
                            bindingInfo.pImage->sampler,
                            bindingInfo.pImage->view,
                            Translate<VkImageLayout>(bindingInfo.imageLayout)});
                }
                else if (bindingInfo.pBuffer != nullptr)
                {
                    size_t range = bindingInfo.pBuffer->Size();
                    if (bindingInfos[0].type == DescriptorType::UniformBufferDynamic ||
                        bindingInfos[0].type == DescriptorType::StorageBufferDynamic)
                        range /= SWAPCHAIN_IMAGES;

                    bufferInfos.push_back(
                        VkDescriptorBufferInfo{
                            bindingInfo.pBuffer->Handle(),
                            0,
                            range});
                }
                else
                {
                    PE_ERROR("Descriptor::UpdateDescriptor: pImage and pBuffer are both nullptr");
                }
            }

            VkWriteDescriptorSet writeSet{};
            writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeSet.dstSet = m_handle;
            writeSet.dstBinding = binding;
            writeSet.dstArrayElement = 0;
            writeSet.descriptorCount = static_cast<uint32_t>(bindingInfos.size());
            writeSet.descriptorType = Translate<VkDescriptorType>(bindingInfos[0].type);

            if (bindingInfos[0].type == DescriptorType::CombinedImageSampler)
            {
                size_t index = imageInfos.size() - bindingInfos.size();
                writeSet.pImageInfo = &imageInfos[index];
            }
            else if (bindingInfos[0].type == DescriptorType::UniformBuffer ||
                     bindingInfos[0].type == DescriptorType::StorageBuffer ||
                     bindingInfos[0].type == DescriptorType::UniformBufferDynamic ||
                     bindingInfos[0].type == DescriptorType::StorageBufferDynamic)
            {
                size_t index = bufferInfos.size() - bindingInfos.size();
                writeSet.pBufferInfo = &bufferInfos[index];
            }
            else
                PE_ERROR("Descriptor::UpdateDescriptor: DescriptorType::Invalid");

            writeSets.push_back(writeSet);
        }

        vkUpdateDescriptorSets(RHII.GetDevice(), static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
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
