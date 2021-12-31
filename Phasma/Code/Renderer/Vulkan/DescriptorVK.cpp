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
    DescriptorBinding::DescriptorBinding()
    {
        binding = 0;
        descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorCount = 1;
        stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pImmutableSamplers = nullptr;
    }

    DescriptorBinding::DescriptorBinding(
        uint32_t binding,
        DescriptorType descriptorType,
        ShaderStageFlags stageFlags,
        uint32_t descriptorCount,
        SamplerHandle *pImmutableSamplers)
        : binding(binding),
          descriptorType(descriptorType),
          descriptorCount(descriptorCount),
          stageFlags(stageFlags),
          pImmutableSamplers(pImmutableSamplers)
    {
    }

    DescriptorPool::DescriptorPool(uint32_t maxDescriptorSets)
    {
        std::vector<VkDescriptorPoolSize> descPoolsize(4);
        descPoolsize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descPoolsize[0].descriptorCount = maxDescriptorSets;
        descPoolsize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descPoolsize[1].descriptorCount = maxDescriptorSets;
        descPoolsize[2].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        descPoolsize[2].descriptorCount = maxDescriptorSets;
        descPoolsize[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descPoolsize[3].descriptorCount = maxDescriptorSets;

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        createInfo.poolSizeCount = static_cast<uint32_t>(descPoolsize.size());
        createInfo.pPoolSizes = descPoolsize.data();
        createInfo.maxSets = maxDescriptorSets;

        VkDescriptorPool descriptorPoolVK;
        vkCreateDescriptorPool(RHII.device, &createInfo, nullptr, &descriptorPoolVK);
        m_handle = descriptorPoolVK;
    }

    DescriptorPool::~DescriptorPool()
    {
        if (m_handle)
        {
            vkDestroyDescriptorPool(RHII.device, m_handle, nullptr);
            m_handle = {};
        }
    }

    DescriptorLayout::DescriptorLayout(const std::vector<DescriptorBinding> &bindings)
    {
        if (bindings.empty())
            PE_ERROR("DescriptorLayout: No bindings provided");

        // Bindings
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
        for (const auto &layoutBinding : bindings)
        {
            setLayoutBindings.push_back(
                VkDescriptorSetLayoutBinding{
                    layoutBinding.binding,
                    (VkDescriptorType)layoutBinding.descriptorType,
                    layoutBinding.descriptorCount,
                    (VkShaderStageFlags)layoutBinding.stageFlags,
                    nullptr});
        }

        // Bindings flags
        std::vector<VkDescriptorBindingFlags> descriptorBindingFlags(setLayoutBindings.size());
        for (int i = 0; i < setLayoutBindings.size(); i++)
        {
            if (setLayoutBindings[i].descriptorCount > 1)
            {
                // Note: As for now, there can be only one unbound array in a descriptor set and it must be the last binding
                // I think this is a Vulkan limitation
                if (i != setLayoutBindings.size() - 1)
                    PE_ERROR("DescriptorLayout: An unbound array must be the last binding");
                descriptorBindingFlags[i] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            }
            else
            {
                descriptorBindingFlags[i] = 0;
            }
        }

        // Set flags to bindings
        VkDescriptorSetLayoutBindingFlagsCreateInfo setLayoutBindingFlags{};
        setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        setLayoutBindingFlags.bindingCount = static_cast<uint32_t>(descriptorBindingFlags.size());
        setLayoutBindingFlags.pBindingFlags = descriptorBindingFlags.data();

        // Layout create info
        VkDescriptorSetLayoutCreateInfo descriptorLayout{};
        descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        descriptorLayout.pBindings = setLayoutBindings.data();
        descriptorLayout.pNext = &setLayoutBindingFlags;

        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(RHII.device, &descriptorLayout, nullptr, &layout);
        m_handle = layout;
    }

    DescriptorLayout::~DescriptorLayout()
    {
        if (m_handle)
        {
            vkDestroyDescriptorSetLayout(RHII.device, m_handle, nullptr);
            m_handle = {};
        }
    }

    Descriptor::Descriptor(DescriptorLayout *layout, uint32_t variableCount)
    {
        if (!layout)
            PE_ERROR("DescriptorLayout is nullptr");

        uint32_t variableDescCounts[] = {variableCount};

        VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo = {};
        variableDescriptorCountAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        variableDescriptorCountAllocInfo.descriptorSetCount = 1;
        variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

        VkDescriptorSetLayout dsetLayout = layout->Handle();
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = RHII.descriptorPool->Handle();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &dsetLayout;
        allocateInfo.pNext = &variableDescriptorCountAllocInfo;

        VkDescriptorSet dset;
        vkAllocateDescriptorSets(RHII.device, &allocateInfo, &dset);
        m_handle = dset;
    }

    Descriptor::~Descriptor()
    {
    }

    DescriptorUpdateInfo::DescriptorUpdateInfo()
        : binding(0),
          pBuffer(nullptr),
          bufferUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
          pImage(nullptr),
          imageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
    }

    void Descriptor::UpdateDescriptor(uint32_t infoCount, DescriptorUpdateInfo *pInfo)
    {
        // store by binding
        std::map<uint32_t, std::vector<DescriptorUpdateInfo>> infosMap{};
        for (uint32_t i = 0; i < infoCount; i++)
            infosMap[pInfo[i].binding].push_back(pInfo[i]);

        std::vector<VkDescriptorImageInfo> imageInfos{};
        imageInfos.reserve(infoCount);
        std::vector<VkDescriptorBufferInfo> bufferInfos{};
        bufferInfos.reserve(infoCount);
        std::vector<VkWriteDescriptorSet> writeSets{};
        writeSets.reserve(infosMap.size());

        for (auto const &[binding, infos] : infosMap)
        {
            for (auto const &info : infos)
            {
                if (info.pImage != nullptr)
                    imageInfos.push_back(VkDescriptorImageInfo{info.pImage->sampler, info.pImage->view, (VkImageLayout)info.imageLayout});
                else if (info.pBuffer != nullptr)
                    bufferInfos.push_back(VkDescriptorBufferInfo{info.pBuffer->Handle(), 0, info.pBuffer->Size()});
                else
                    PE_ERROR("Descriptor::UpdateDescriptor: pImage and pBuffer are both nullptr");
            }

            VkWriteDescriptorSet writeSet{};
            writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeSet.dstSet = m_handle;
            writeSet.dstBinding = binding;
            writeSet.dstArrayElement = 0;
            writeSet.descriptorCount = static_cast<uint32_t>(infos.size());

            if (infos[0].pImage != nullptr)
            {
                size_t index = imageInfos.size() - infos.size();
                writeSet.pImageInfo = &imageInfos[index];
                writeSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            else //(infos[0].pBuffer != nullptr)
            {
                size_t index = bufferInfos.size() - infos.size();
                writeSet.pBufferInfo = &bufferInfos[index];
                if (infos[0].bufferUsage == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
                    writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                else if (infos[0].bufferUsage == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
                    writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                else
                    PE_ERROR("Descriptor::UpdateDescriptor: bufferUsage is not supported");
            }

            writeSets.push_back(writeSet);
        }

        vkUpdateDescriptorSets(RHII.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
    }
}
#endif
