#ifdef PE_VULKAN
#include "Renderer/Descriptor.h"
#include "Renderer/RHI.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"

namespace pe
{
    DescriptorPool::DescriptorPool(uint32_t count, DescriptorPoolSize *sizes, const std::string &name)
    {
        // Type and count per element in descriptor pool
        std::vector<VkDescriptorPoolSize> descPoolsize(count);
        for (uint32_t i = 0; i < count; i++)
        {
            descPoolsize[i].type = Translate<VkDescriptorType>(sizes[i].type);
            descPoolsize[i].descriptorCount = sizes[i].descriptorCount;
        }

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT ;
        createInfo.poolSizeCount = static_cast<uint32_t>(descPoolsize.size());
        createInfo.pPoolSizes = descPoolsize.data();
        createInfo.maxSets = 1; // Unique descriptor sets for each descriptor pool

        VkDescriptorPool descriptorPoolVK;
        PE_CHECK(vkCreateDescriptorPool(RHII.GetDevice(), &createInfo, nullptr, &descriptorPoolVK));
        m_apiHandle = descriptorPoolVK;

        Debug::SetObjectName(m_apiHandle, name);
    }

    DescriptorPool::~DescriptorPool()
    {
        if (m_apiHandle)
        {
            vkDestroyDescriptorPool(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }

    DescriptorLayout::DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                       ShaderStage stage,
                                       const std::string &name,
                                       bool pushDescriptor)
    {
        m_pushDescriptor = pushDescriptor;
        m_variableCount = 1;

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

        m_allowUpdateAfterBind = true;
        for (int i = 0; i < bindingsVK.size(); i++)
        {
            if (bindingsVK[i].descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
                bindingsVK[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                bindingsVK[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
            {
                m_allowUpdateAfterBind = false;
                break;
            }
        }

        // Bindings flags
        std::vector<VkDescriptorBindingFlags> bindingFlags(bindingsVK.size());
        for (int i = 0; i < bindingsVK.size(); i++)
        {
            bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

            if (m_allowUpdateAfterBind && !pushDescriptor)
                bindingFlags[i] |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

            if (bindingInfos[i].bindless)
            {
                // Note: As for now, it can be only one unbound array in a descriptor set and it must be the last binding
                // Vulkan limitation?
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
        if (pushDescriptor)
            dslci.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        else if (m_allowUpdateAfterBind)
            dslci.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        VkDescriptorSetLayout layout;
        PE_CHECK(vkCreateDescriptorSetLayout(RHII.GetDevice(), &dslci, nullptr, &layout));
        m_apiHandle = layout;

        Debug::SetObjectName(m_apiHandle, name);
    }

    DescriptorLayout::~DescriptorLayout()
    {
        if (m_apiHandle)
        {
            vkDestroyDescriptorSetLayout(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }

    Descriptor::Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                           ShaderStage stage,
                           bool pushDescriptor,
                           const std::string &name)
        : m_pushDescriptor{pushDescriptor},
          m_bindingInfos{bindingInfos},
          m_updateInfos(bindingInfos.size()),
          m_stage{stage},
          m_dynamicOffsets{}
    {
        std::sort(m_bindingInfos.begin(), m_bindingInfos.end(), [](const DescriptorBindingInfo &a, const DescriptorBindingInfo &b)
                  { return a.binding < b.binding; });

        // Get the count per descriptor type
        std::unordered_map<DescriptorType, uint32_t> typeCounts{};
        for (auto i = 0; i < m_bindingInfos.size(); i++)
        {
            auto it = typeCounts.find(m_bindingInfos[i].type);
            if (it == typeCounts.end())
                typeCounts[m_bindingInfos[i].type] = m_bindingInfos[i].count;
            else
                it->second += m_bindingInfos[i].count;
        }

        // Create pool sizes from typeCounts above
        std::vector<DescriptorPoolSize> poolSizes(typeCounts.size());
        uint32_t i = 0;
        for (auto const &[type, count] : typeCounts)
        {
            poolSizes[i].type = type;
            poolSizes[i].descriptorCount = count;
            i++;
        }

        // Create DescriptorPool and DescriptorLayout for this descriptor set
        m_pool = DescriptorPool::Create(i, poolSizes.data(), name + "_pool");
        m_layout = DescriptorLayout::GetOrCreate(m_bindingInfos, m_stage, m_pushDescriptor);

        // DescriptorLayout calculates the variable count on creation
        uint32_t variableDescCounts[] = {m_layout->GetVariableCount()};

        VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo = {};
        variableDescriptorCountAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        variableDescriptorCountAllocInfo.descriptorSetCount = 1;
        variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

        VkDescriptorSetLayout dsetLayout = m_layout->ApiHandle();
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_pool->ApiHandle();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &dsetLayout;
        allocateInfo.pNext = &variableDescriptorCountAllocInfo; // If the flag was not set in the layout, this will be ignored

        VkDescriptorSet dset;
        PE_CHECK(vkAllocateDescriptorSets(RHII.GetDevice(), &allocateInfo, &dset));
        m_apiHandle = dset;

        Debug::SetObjectName(m_apiHandle, name);
    }
    
    void Descriptor::SetImageViews(uint32_t binding,
                                   const std::vector<ImageViewApiHandle> &views,
                                   const std::vector<SamplerApiHandle> &samplers)
    {
        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.views = views;
        info.samplers = samplers;
        m_updateInfos[binding] = info;
    }

    void Descriptor::SetImageView(uint32_t binding, ImageViewApiHandle view, SamplerApiHandle sampler)
    {
        SetImageViews(binding, {view}, {sampler});
    }

    void Descriptor::SetBuffers(uint32_t binding,
                                const std::vector<Buffer *> &buffers,
                                const std::vector<uint64_t> &offsets,
                                const std::vector<uint64_t> &ranges)
    {
        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.buffers = buffers;
        info.offsets = offsets;
        info.ranges = ranges;
        m_updateInfos[binding] = info;
    }

    void Descriptor::SetBuffer(uint32_t binding, Buffer *buffer, uint64_t offset, uint64_t range)
    {
        SetBuffers(binding, {buffer}, {offset}, {range});
    }

    void Descriptor::SetSamplers(uint32_t binding, const std::vector<SamplerApiHandle> &samplers)
    {
        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.samplers = samplers;
        m_updateInfos[binding] = info;
    }

    void Descriptor::SetSampler(uint32_t binding, SamplerApiHandle sampler)
    {
        SetSamplers(binding, {sampler});
    }

    void Descriptor::Update()
    {
        std::vector<VkWriteDescriptorSet> writes{};
        std::vector<std::vector<VkDescriptorImageInfo>> imageInfosVK{};
        std::vector<std::vector<VkDescriptorBufferInfo>> bufferInfosVK{};
        imageInfosVK.reserve(m_updateInfos.size());
        bufferInfosVK.reserve(m_updateInfos.size());
        writes.reserve(m_updateInfos.size());

        for (uint32_t i = 0; i < m_updateInfos.size(); i++)
        {
            auto &updateInfo = m_updateInfos[i];
            auto &bindingInfo = m_bindingInfos[i];

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_apiHandle;
            write.dstBinding = updateInfo.binding;
            write.dstArrayElement = 0;
            write.descriptorType = Translate<VkDescriptorType>(bindingInfo.type);
            if (updateInfo.views.size() > 0)
            {
                auto &infosVK = imageInfosVK.emplace_back(std::vector<VkDescriptorImageInfo>{});
                infosVK.resize(updateInfo.views.size());
                for (uint32_t j = 0; j < updateInfo.views.size(); j++)
                {
                    infosVK[j] = {};
                    infosVK[j].imageView = updateInfo.views[j];
                    infosVK[j].imageLayout = Translate<VkImageLayout>(bindingInfo.imageLayout);
                    infosVK[j].sampler = bindingInfo.type == DescriptorType::CombinedImageSampler ? updateInfo.samplers[j] : SamplerApiHandle{};
                }

                write.descriptorCount = static_cast<uint32_t>(infosVK.size());
                write.pImageInfo = infosVK.data();
                imageInfosVK.push_back(infosVK);
            }
            else if (updateInfo.buffers.size() > 0)
            {
                auto &infosVK = bufferInfosVK.emplace_back(std::vector<VkDescriptorBufferInfo>{});
                infosVK.resize(updateInfo.buffers.size());
                for (uint32_t j = 0; j < updateInfo.buffers.size(); j++)
                {
                    infosVK[j] = {};
                    infosVK[j].buffer = updateInfo.buffers[j]->ApiHandle();
                    infosVK[j].offset = updateInfo.offsets.size() > 0 ? updateInfo.offsets[j] : 0;
                    infosVK[j].range = updateInfo.ranges.size() > 0 ? (updateInfo.ranges[j] > 0 ? updateInfo.ranges[j] : VK_WHOLE_SIZE) : VK_WHOLE_SIZE;
                }

                write.descriptorCount = static_cast<uint32_t>(infosVK.size());
                write.pBufferInfo = infosVK.data();
            }
            else if (updateInfo.samplers.size() > 0 && bindingInfo.type != DescriptorType::CombinedImageSampler)
            {
                auto &infosVK = imageInfosVK.emplace_back(std::vector<VkDescriptorImageInfo>{});
                infosVK.resize(updateInfo.samplers.size());
                for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                {
                    infosVK[j] = {};
                    infosVK[j].imageView = nullptr;
                    infosVK[j].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    infosVK[j].sampler = updateInfo.samplers[j];
                }

                write.descriptorCount = static_cast<uint32_t>(infosVK.size());
                write.pImageInfo = infosVK.data();
            }
            else
            {
                continue; // We are allowing this since all bindings can be partially bound
            }

            writes.push_back(write);
        }

        vkUpdateDescriptorSets(RHII.GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        m_updateInfos.clear();
        m_updateInfos.resize(m_bindingInfos.size());
    }
}
#endif
