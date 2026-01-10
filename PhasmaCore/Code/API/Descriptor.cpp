#include "API/Descriptor.h"
#include "API/Buffer.h"
#include "API/Image.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        int32_t GetBindingIndex(uint32_t binding, const std::vector<DescriptorBindingInfo> &bindingInfos)
        {
            for (int32_t i = 0; i < bindingInfos.size(); i++)
            {
                if (bindingInfos[i].binding == binding)
                    return i;
            }
            PE_WARN("Descriptor: Binding not found");
            return -1;
        }
    } // namespace

    DescriptorPool::DescriptorPool(const std::vector<vk::DescriptorPoolSize> &sizes,
                                   const std::string &name,
                                   uint32_t maxSets)
    {
        vk::DescriptorPoolCreateInfo createInfo{};
        createInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        createInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        createInfo.pPoolSizes = sizes.data();
        createInfo.maxSets = std::max(1u, maxSets);

        m_apiHandle = RHII.GetDevice().createDescriptorPool(createInfo);

        Debug::SetObjectName(m_apiHandle, name);
    }

    DescriptorPool::~DescriptorPool()
    {
        if (m_apiHandle)
            RHII.GetDevice().destroyDescriptorPool(m_apiHandle);
    }

    DescriptorLayout::DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                       vk::ShaderStageFlags stage,
                                       const std::string &name,
                                       bool pushDescriptor)
    {
        m_pushDescriptor = pushDescriptor;
        m_variableCount = 1;

        // Bindings
        std::vector<vk::DescriptorSetLayoutBinding> bindings{};
        for (auto const &bindingInfo : bindingInfos)
        {
            vk::DescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = bindingInfo.binding;
            layoutBinding.descriptorType = bindingInfo.type;
            layoutBinding.descriptorCount = bindingInfo.count;
            layoutBinding.stageFlags = stage;
            layoutBinding.pImmutableSamplers = nullptr;

            bindings.push_back(layoutBinding);
        }

        m_allowUpdateAfterBind = true;
        for (int i = 0; i < bindings.size(); i++)
        {
            if (bindings[i].descriptorType == vk::DescriptorType::eInputAttachment ||
                bindings[i].descriptorType == vk::DescriptorType::eUniformBufferDynamic ||
                bindings[i].descriptorType == vk::DescriptorType::eStorageBufferDynamic)
            {
                m_allowUpdateAfterBind = false;
                break;
            }
        }

        // Bindings flags
        std::vector<vk::DescriptorBindingFlags> bindingFlags(bindings.size());
        for (int i = 0; i < bindings.size(); i++)
        {

            if (m_allowUpdateAfterBind && !pushDescriptor)
                bindingFlags[i] |= vk::DescriptorBindingFlagBits::eUpdateAfterBind;

            if (bindingInfos[i].bindless)
            {
                // Note: As for now, it can be only one unbound array in a descriptor set and it must be the last binding
                // Vulkan limitation?
                PE_ERROR_IF(i != bindingInfos.size() - 1, "DescriptorLayout: An unbound array must be the last binding");

                bindingFlags[i] |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
                m_variableCount = bindings[i].descriptorCount;
            }
        }

        // Set flags to bindings
        vk::DescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlags{};
        layoutBindingFlags.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        layoutBindingFlags.pBindingFlags = bindingFlags.data();

        // Layout create info
        vk::DescriptorSetLayoutCreateInfo dslci{};
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings = bindings.data();
        dslci.pNext = &layoutBindingFlags;
        if (pushDescriptor)
            dslci.flags |= vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor;
        else if (m_allowUpdateAfterBind)
            dslci.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

        m_apiHandle = RHII.GetDevice().createDescriptorSetLayout(dslci);

        Debug::SetObjectName(m_apiHandle, name);
    }

    DescriptorLayout::~DescriptorLayout()
    {
        if (m_apiHandle)
        {
            RHII.GetDevice().destroyDescriptorSetLayout(m_apiHandle);
            m_apiHandle = vk::DescriptorSetLayout{};
        }
    }

    Descriptor::Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                           vk::ShaderStageFlags stage,
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
        std::unordered_map<vk::DescriptorType, uint32_t> typeCounts{};
        for (auto i = 0; i < m_bindingInfos.size(); i++)
        {
            auto it = typeCounts.find(m_bindingInfos[i].type);
            if (it == typeCounts.end())
                typeCounts[m_bindingInfos[i].type] = m_bindingInfos[i].count;
            else
                it->second += m_bindingInfos[i].count;
        }

        // Create pool sizes from typeCounts above
        std::vector<vk::DescriptorPoolSize> poolSizes(typeCounts.size());
        uint32_t i = 0;
        for (auto const &[type, count] : typeCounts)
        {
            poolSizes[i].type = type;
            poolSizes[i].descriptorCount = count;
            i++;
        }

        // Create DescriptorPool and DescriptorLayout for this descriptor set
        m_pool = DescriptorPool::Create(poolSizes, name + "_pool");
        m_layout = DescriptorLayout::GetOrCreate(m_bindingInfos, m_stage, m_pushDescriptor);

        // DescriptorLayout calculates the variable count on creation
        uint32_t variableDescCounts[] = {m_layout->GetVariableCount()};

        vk::DescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo = {};
        variableDescriptorCountAllocInfo.descriptorSetCount = 1;
        variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

        vk::DescriptorSetLayout dsetLayout = m_layout->ApiHandle();
        vk::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = m_pool->ApiHandle();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &dsetLayout;
        allocateInfo.pNext = &variableDescriptorCountAllocInfo; // If the flag was not set in the layout, this will be ignored

        m_apiHandle = RHII.GetDevice().allocateDescriptorSets(allocateInfo)[0];

        Debug::SetObjectName(m_apiHandle, name);
    }

    Descriptor::~Descriptor()
    {
        DescriptorPool::Destroy(m_pool);
    }

    void Descriptor::SetImageViews(uint32_t binding,
                                   const std::vector<vk::ImageView> &views,
                                   const std::vector<vk::Sampler> &samplers)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.views = views;
        info.samplers = samplers;
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::SetImageView(uint32_t binding, vk::ImageView view, vk::Sampler sampler)
    {
        SetImageViews(binding, {view}, {sampler});
    }

    void Descriptor::SetBuffers(uint32_t binding,
                                const std::vector<Buffer *> &buffers,
                                const std::vector<uint64_t> &offsets,
                                const std::vector<uint64_t> &ranges)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.buffers = buffers;
        info.offsets = offsets;
        info.ranges = ranges;
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::SetBuffer(uint32_t binding, Buffer *buffer, uint64_t offset, uint64_t range)
    {
        SetBuffers(binding, {buffer}, {offset}, {range});
    }

    void Descriptor::SetSamplers(uint32_t binding, const std::vector<vk::Sampler> &samplers)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.samplers = samplers;
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::SetSampler(uint32_t binding, vk::Sampler sampler)
    {
        SetSamplers(binding, {sampler});
    }

    void Descriptor::SetAccelerationStructure(uint32_t binding, vk::AccelerationStructureKHR tlas)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.accelerationStructures = {tlas};
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::Update()
    {
        std::vector<vk::WriteDescriptorSet> writes{};
        std::vector<std::vector<vk::DescriptorImageInfo>> imageInfos{};
        std::vector<std::vector<vk::DescriptorBufferInfo>> bufferInfos{};
        std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> accelerationWrites{};
        imageInfos.reserve(m_updateInfos.size());
        bufferInfos.reserve(m_updateInfos.size());
        accelerationWrites.reserve(m_updateInfos.size());
        writes.reserve(m_updateInfos.size());

        for (uint32_t i = 0; i < m_updateInfos.size(); i++)
        {
            auto &updateInfo = m_updateInfos[i];
            auto &bindingInfo = m_bindingInfos[i];

            vk::WriteDescriptorSet write{};
            write.dstSet = m_apiHandle;
            write.dstBinding = updateInfo.binding;
            write.dstArrayElement = 0;
            write.descriptorType = bindingInfo.type;
            if (updateInfo.views.size())
            {
                auto &infos = imageInfos.emplace_back(std::vector<vk::DescriptorImageInfo>{});
                infos.resize(updateInfo.views.size());
                for (uint32_t j = 0; j < updateInfo.views.size(); j++)
                {
                    infos[j] = vk::DescriptorImageInfo{};
                    infos[j].imageView = updateInfo.views[j];
                    infos[j].imageLayout = bindingInfo.imageLayout;
                    infos[j].sampler = bindingInfo.type == vk::DescriptorType::eCombinedImageSampler ? updateInfo.samplers[j] : vk::Sampler{};
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
                    infos[j].buffer = updateInfo.buffers[j]->ApiHandle();
                    infos[j].offset = updateInfo.offsets.size() > 0 ? updateInfo.offsets[j] : 0;
                    infos[j].range = updateInfo.ranges.size() > 0 ? (updateInfo.ranges[j] > 0 ? updateInfo.ranges[j] : vk::WholeSize) : vk::WholeSize;
                }

                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pBufferInfo = infos.data();
            }
            else if (updateInfo.samplers.size() && bindingInfo.type != vk::DescriptorType::eCombinedImageSampler)
            {
                auto &infos = imageInfos.emplace_back(std::vector<vk::DescriptorImageInfo>{});
                infos.resize(updateInfo.samplers.size());
                for (uint32_t j = 0; j < updateInfo.samplers.size(); j++)
                {
                    infos[j] = vk::DescriptorImageInfo{};
                    infos[j].imageView = nullptr;
                    infos[j].imageLayout = vk::ImageLayout::eUndefined;
                    infos[j].sampler = updateInfo.samplers[j];
                }

                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pImageInfo = infos.data();
            }
            else if (updateInfo.accelerationStructures.size())
            {
                auto &accelerationWrite = accelerationWrites.emplace_back();
                accelerationWrite.accelerationStructureCount = static_cast<uint32_t>(updateInfo.accelerationStructures.size());
                accelerationWrite.pAccelerationStructures = updateInfo.accelerationStructures.data();

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

        m_updateInfos.clear();
        m_updateInfos.resize(m_bindingInfos.size());
    }
} // namespace pe
