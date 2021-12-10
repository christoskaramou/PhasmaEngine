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
    DescriptorBinding::DescriptorBinding(uint32_t binding, DescriptorType descriptorType, ShaderStageFlags stageFlags) :
        binding(binding), descriptorType(descriptorType), descriptorCount(1), stageFlags(stageFlags), pImmutableSamplers()
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

    DescriptorLayout::DescriptorLayout(const std::vector<DescriptorBinding>& bindings)
    {
        this->bindings = bindings;

        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
        for (const auto& layoutBinding : bindings)
        {
            setLayoutBindings.push_back(
                VkDescriptorSetLayoutBinding
                {
                    layoutBinding.binding,
                    (VkDescriptorType)layoutBinding.descriptorType,
                    1,
                    (VkShaderStageFlags)layoutBinding.stageFlags,
                    nullptr
                }
            );
        }

        VkDescriptorSetLayoutCreateInfo descriptorLayout{};
        descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        descriptorLayout.pBindings = setLayoutBindings.data();

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

    Descriptor::Descriptor(DescriptorLayout* layout)
    {
        VkDescriptorSetLayout dsetLayout = layout->Handle();
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = RHII.descriptorPool->Handle();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &dsetLayout;

        VkDescriptorSet dset;
        vkAllocateDescriptorSets(RHII.device, &allocateInfo, &dset);
        m_handle = dset;
    }

    Descriptor::~Descriptor()
    {
    }

    DescriptorUpdateInfo::DescriptorUpdateInfo() :
        binding(0), pBuffer(nullptr), bufferUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT), pImage(nullptr), imageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
    }

    void Descriptor::UpdateDescriptor(uint32_t infoCount, DescriptorUpdateInfo* pInfo)
    {
        std::deque<VkDescriptorImageInfo> dsii{};
        auto const wSetImage = [this, &dsii](uint32_t dstBinding, Image* image, ImageLayout layout)
        {
            VkDescriptorImageInfo info{ image->sampler, image->view, (VkImageLayout)layout };
            dsii.push_back(info);

            VkWriteDescriptorSet textureWriteSet{};
            textureWriteSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            textureWriteSet.dstSet = m_handle;
            textureWriteSet.dstBinding = dstBinding;
            textureWriteSet.dstArrayElement = 0;
            textureWriteSet.descriptorCount = 1;
            textureWriteSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureWriteSet.pImageInfo = &dsii.back();

            return textureWriteSet;
        };

        std::deque<VkDescriptorBufferInfo> dsbi{};
        auto const wSetBuffer = [this, &dsbi](uint32_t dstBinding, Buffer* buffer, BufferUsageFlags bufferUsage)
        {
            VkDescriptorBufferInfo info{ buffer->Handle(), 0, buffer->Size() };
            dsbi.push_back(info);

            VkWriteDescriptorSet textureWriteSet{};
            textureWriteSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            textureWriteSet.dstSet = m_handle;
            textureWriteSet.dstBinding = dstBinding;
            textureWriteSet.dstArrayElement = 0;
            textureWriteSet.descriptorCount = 1;
            textureWriteSet.descriptorType = bufferUsage == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ?
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            textureWriteSet.pBufferInfo = &dsbi.back();

            return textureWriteSet;
        };

        std::vector<VkWriteDescriptorSet> textureWriteSets(infoCount);

        for(uint32_t i = 0; i < infoCount; i++)
        {
            if (pInfo[i].pImage != nullptr)
                textureWriteSets[i] = wSetImage(pInfo[i].binding, pInfo[i].pImage, pInfo[i].imageLayout);
            else if (pInfo[i].pBuffer != nullptr)
                textureWriteSets[i] = wSetBuffer(pInfo[i].binding, pInfo[i].pBuffer, pInfo[i].bufferUsage);
            else
                PE_ERROR("Descriptor::UpdateDescriptor: pImage and pBuffer are nullptr");
        }

        vkUpdateDescriptorSets(RHII.device, (uint32_t)textureWriteSets.size(), textureWriteSets.data(), 0, nullptr);
    }
}
#endif
