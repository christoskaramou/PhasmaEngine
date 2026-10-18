#include "API/Descriptor_Internal.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12DescriptorImpl.h"
#endif

namespace pe
{
    DescriptorPool::Impl *CreateDescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanDescriptorPoolImpl(owner, desc);
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            return new Dx12DescriptorPoolImpl(owner, desc);
#endif
        default:
            PE_ERROR("CreateDescriptorPoolImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }

    DescriptorLayout::Impl *CreateDescriptorLayoutImpl(DescriptorLayout *owner)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanDescriptorLayoutImpl(owner);
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            return new Dx12DescriptorLayoutImpl(owner);
#endif
        default:
            PE_ERROR("CreateDescriptorLayoutImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }

    Descriptor::Impl *CreateDescriptorImpl(Descriptor *owner)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanDescriptorImpl(owner);
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            return new Dx12DescriptorImpl(owner);
#endif
        default:
            PE_ERROR("CreateDescriptorImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }

    namespace
    {
        int32_t GetBindingIndex(uint32_t binding, const std::vector<DescriptorBindingInfo> &bindingInfos)
        {
            for (int32_t i = 0; i < bindingInfos.size(); i++)
            {
                if (bindingInfos[i].binding == binding)
                    return i;
            }
            PE_WARN("[Descriptor] Binding %u not found", binding);
            return -1;
        }

        bool IsUpdateAfterBindCompatible(PeBindingType type)
        {
            return type != PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT &&
                   type != PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
                   type != PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        }

        bool HasDescriptorUpdate(const DescriptorUpdateInfo &info)
        {
            return !info.views.empty() ||
                   !info.buffers.empty() ||
                   !info.samplers.empty() ||
                   !info.accelerationStructures.empty();
        }

        template <class T>
        void MergeDescriptorValues(std::vector<T> &bound, const std::vector<T> &update, size_t count)
        {
            if (bound.size() < count)
                bound.resize(count);

            // Offsets and ranges may be shorter than the buffers they describe (Lua passes them as given).
            const size_t copied = std::min(update.size(), count);
            std::copy_n(update.begin(), copied, bound.begin());
            std::fill(bound.begin() + copied, bound.begin() + count, T{});
        }

        template <class T>
        std::vector<T> LimitDescriptorValues(const std::vector<T> &values,
                                             uint32_t maxCount,
                                             const char *kind,
                                             const std::string &descriptorName,
                                             uint32_t binding)
        {
            if (values.size() <= maxCount)
                return values;

            PE_WARN("[Descriptor] Truncating %s for '%s' binding %u from %zu to %u entries",
                    kind,
                    descriptorName.c_str(),
                    binding,
                    values.size(),
                    maxCount);
            return std::vector<T>(values.begin(), values.begin() + maxCount);
        }
    } // namespace

    DescriptorPool *DescriptorPool::Create(const DescriptorPoolDesc &desc, const std::string &name)
    {
        DescriptorPool *pool = new DescriptorPool(desc, name);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(DescriptorPool), reinterpret_cast<void *>(pool));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object DescriptorPool created (Handle: %p)", reinterpret_cast<void *>(pool));
#endif
#endif
        return pool;
    }

    DescriptorPool *DescriptorPool::Create(const std::vector<DescriptorPoolSize> &sizes,
                                           const std::string &name,
                                           uint32_t maxSets,
                                           bool updateAfterBind)
    {
        DescriptorPoolDesc desc{};
        desc.sizes = sizes;
        desc.maxSets = maxSets;
        desc.updateAfterBind = updateAfterBind;
        return Create(desc, name);
    }

    void DescriptorPool::Destroy(DescriptorPool *&pool)
    {
        if (pool)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object DescriptorPool destroyed (Handle: %p)", reinterpret_cast<void *>(pool));
#endif
            delete pool;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(DescriptorPool), reinterpret_cast<void *>(pool));
#endif
            pool = nullptr;
        }
    }

    std::vector<DescriptorPool *> DescriptorPool::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(DescriptorPool));
        std::vector<DescriptorPool *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<DescriptorPool *>(p));
        return out;
#else
        return {};
#endif
    }

    DescriptorPool::DescriptorPool(const DescriptorPoolDesc &desc, const std::string &name)
        : m_desc{desc}, m_name{name}
    {
        m_impl = CreateDescriptorPoolImpl(this, desc);
    }

    DescriptorPool::~DescriptorPool()
    {
        delete m_impl;
    }

    DescriptorLayout *DescriptorLayout::Create(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                               PeShaderStageFlags stage,
                                               const std::string &name,
                                               bool pushDescriptor)
    {
        DescriptorLayout *layout = new DescriptorLayout(bindingInfos, stage, name, pushDescriptor);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(DescriptorLayout), reinterpret_cast<void *>(layout));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object DescriptorLayout created (Handle: %p)", reinterpret_cast<void *>(layout));
#endif
#endif
        return layout;
    }

    void DescriptorLayout::Destroy(DescriptorLayout *&layout)
    {
        if (layout)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object DescriptorLayout destroyed (Handle: %p)", reinterpret_cast<void *>(layout));
#endif
            delete layout;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(DescriptorLayout), reinterpret_cast<void *>(layout));
#endif
            layout = nullptr;
        }
    }

    std::vector<DescriptorLayout *> DescriptorLayout::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(DescriptorLayout));
        std::vector<DescriptorLayout *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<DescriptorLayout *>(p));
        return out;
#else
        return {};
#endif
    }

    DescriptorLayout::DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                       PeShaderStageFlags stage,
                                       const std::string &name,
                                       bool pushDescriptor)
        : m_bindingInfos{bindingInfos},
          m_stage{stage},
          m_name{name},
          m_variableCount{1},
          m_pushDescriptor{pushDescriptor},
          m_allowUpdateAfterBind{true}
    {
        for (int i = 0; i < m_bindingInfos.size(); i++)
        {
            if (!IsUpdateAfterBindCompatible(m_bindingInfos[i].type))
            {
                m_allowUpdateAfterBind = false;
                break;
            }
        }

        for (int i = 0; i < m_bindingInfos.size(); i++)
        {
            if (m_bindingInfos[i].bindless)
            {
                // Note: As for now, it can be only one unbound array in a descriptor set and it must be the last binding
                // Vulkan limitation?
                PE_ERROR_IF(i != m_bindingInfos.size() - 1, "DescriptorLayout: An unbound array must be the last binding");
                m_variableCount = m_bindingInfos[i].count;
            }
        }

        m_impl = CreateDescriptorLayoutImpl(this);
    }

    DescriptorLayout::~DescriptorLayout()
    {
        delete m_impl;
    }

    DescriptorLayoutBuilder &DescriptorLayoutBuilder::AddBinding(uint32_t binding,
                                                                 PeBindingType type,
                                                                 uint32_t count,
                                                                 PeImageLayout imageLayout,
                                                                 bool bindless,
                                                                 const std::string &name)
    {
        DescriptorBindingInfo info{};
        info.binding = binding;
        info.dxRegister = binding;
        info.dxSpace = 0;
        info.count = count;
        info.type = type;
        info.imageLayout = imageLayout;
        info.bindless = bindless;
        info.name = name;
        m_bindingInfos.push_back(info);
        return *this;
    }

    DescriptorLayoutBuilder &DescriptorLayoutBuilder::SetStage(PeShaderStageFlags stage)
    {
        m_stage = stage;
        return *this;
    }

    DescriptorLayoutBuilder &DescriptorLayoutBuilder::SetPushDescriptor(bool pushDescriptor)
    {
        m_pushDescriptor = pushDescriptor;
        return *this;
    }

    DescriptorLayoutBuilder &DescriptorLayoutBuilder::SetName(const std::string &name)
    {
        m_name = name;
        return *this;
    }

    DescriptorLayout *DescriptorLayoutBuilder::Build() const
    {
        return DescriptorLayout::Create(m_bindingInfos, m_stage, m_name, m_pushDescriptor);
    }

    Descriptor *Descriptor::Create(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                   PeShaderStageFlags stage,
                                   bool pushDescriptor,
                                   const std::string &name)
    {
        Descriptor *descriptor = new Descriptor(bindingInfos, stage, pushDescriptor, name);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Descriptor), reinterpret_cast<void *>(descriptor));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Descriptor created (Handle: %p)", reinterpret_cast<void *>(descriptor));
#endif
#endif
        return descriptor;
    }

    void Descriptor::Destroy(Descriptor *&descriptor)
    {
        if (descriptor && descriptor->m_shared)
        {
            descriptor = nullptr;
            return;
        }

        if (descriptor)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Descriptor destroyed (Handle: %p)", reinterpret_cast<void *>(descriptor));
#endif
            delete descriptor;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Descriptor), reinterpret_cast<void *>(descriptor));
#endif
            descriptor = nullptr;
        }
    }

    std::vector<Descriptor *> Descriptor::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Descriptor));
        std::vector<Descriptor *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Descriptor *>(p));
        return out;
#else
        return {};
#endif
    }

    namespace
    {
        // Bindless arrays start small and grow together when a scene needs more textures.
        struct BindlessRegistry
        {
            std::mutex mutex;
            std::unordered_set<Descriptor *> descriptors;
            uint32_t capacity = 256;
        };

        // Never destroyed, so descriptors released during static destruction still find it.
        BindlessRegistry &Bindless()
        {
            static auto *registry = new BindlessRegistry();
            return *registry;
        }

        // Only the scene texture array (any set that ends with pe_textures) grows; other bindless arrays keep their size.
        bool HasSceneTextureArray(const std::vector<DescriptorBindingInfo> &bindingInfos)
        {
            return !bindingInfos.empty() &&
                   bindingInfos.back().bindless &&
                   bindingInfos.back().name == PE_TEXTURE_TABLE_NAME;
        }
    } // namespace

    uint32_t Descriptor::GetBindlessCapacity()
    {
        BindlessRegistry &registry = Bindless();
        std::lock_guard<std::mutex> lock(registry.mutex);
        return registry.capacity;
    }

    size_t Descriptor::GetBindlessArrayCount()
    {
        BindlessRegistry &registry = Bindless();
        std::lock_guard<std::mutex> lock(registry.mutex);
        return registry.descriptors.size();
    }

    bool Descriptor::ResizeBindless(uint32_t capacity)
    {
        BindlessRegistry &registry = Bindless();
        std::lock_guard<std::mutex> lock(registry.mutex);
        // Grow only: bound resources were truncated to the current capacity, so a smaller one has nothing to drop.
        if (capacity <= registry.capacity)
            return true;

        // No rollback: arrays that grew before a failure keep their size, and every array still holds the old capacity.
        for (Descriptor *descriptor : registry.descriptors)
        {
            const uint32_t target = std::min(capacity, descriptor->m_bindingInfos.back().count);
            if (!descriptor->SetVariableCount(target))
                return false;
        }

        registry.capacity = capacity;
        return true;
    }

    bool Descriptor::IsTextureTable() const
    {
        return m_bindingInfos.size() == 1 &&
               m_bindingInfos[0].bindless &&
               m_bindingInfos[0].type == PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
               m_bindingInfos[0].name == PE_TEXTURE_TABLE_NAME;
    }

    uint32_t Descriptor::GetBindingCapacity(size_t bindingIndex) const
    {
        const DescriptorBindingInfo &info = m_bindingInfos[bindingIndex];
        return info.bindless ? m_variableCount : info.count;
    }

    DescriptorPool *Descriptor::CreatePool() const
    {
        // Get the count per descriptor type
        std::unordered_map<PeBindingType, uint32_t> typeCounts{};
        for (size_t i = 0; i < m_bindingInfos.size(); i++)
            typeCounts[m_bindingInfos[i].type] += GetBindingCapacity(i);

        std::vector<DescriptorPoolSize> poolSizes;
        poolSizes.reserve(typeCounts.size());
        for (auto const &[type, count] : typeCounts)
            poolSizes.push_back({type, count});

        bool allowUpdateAfterBindPool = true;
        for (const auto &binding : m_bindingInfos)
        {
            if (!IsUpdateAfterBindCompatible(binding.type))
            {
                allowUpdateAfterBindPool = false;
                break;
            }
        }

        return DescriptorPool::Create(poolSizes, m_name + "_pool", 1, allowUpdateAfterBindPool);
    }

    bool Descriptor::SetVariableCount(uint32_t count)
    {
        if (count == m_variableCount)
            return true;

        const uint32_t previousCount = m_variableCount;
        DescriptorPool *previousPool = m_pool;
        Impl *previousImpl = m_impl;
        // The new descriptor starts empty: its records may name resources freed since they were written (a scene
        // switch, a disabled pass), and the native set path replays records as it allocates. Every owner rewrites
        // after a resize, which only Scene::UpdateImageViews requests before it bumps the geometry version.
        std::vector<DescriptorUpdateInfo> previousResources = m_boundResources;
        for (size_t i = 0; i < m_boundResources.size(); ++i)
            m_boundResources[i] = DescriptorUpdateInfo{.binding = m_bindingInfos[i].binding};

        m_variableCount = count;
        try
        {
            m_pool = CreatePool();
            m_impl = CreateDescriptorImpl(this);
        }
        catch (const std::exception &error)
        {
            PE_WARN("[Descriptor] '%s' cannot hold %u bindless entries: %s", m_name.c_str(), count, error.what());
            if (m_pool != previousPool)
                DescriptorPool::Destroy(m_pool);

            m_pool = previousPool;
            m_impl = previousImpl;
            m_variableCount = previousCount;
            m_boundResources = std::move(previousResources);
            return false;
        }

        // In-flight frames may still read the previous descriptors.
        auto retire = [previousImpl, previousPool]() mutable
        {
            delete previousImpl;
            DescriptorPool::Destroy(previousPool);
        };
        RHII.AddToDeletionQueue(std::move(retire));
        return true;
    }

    bool Descriptor::HasBinding(uint32_t binding) const
    {
        for (const DescriptorBindingInfo &bindingInfo : m_bindingInfos)
        {
            if (bindingInfo.binding == binding)
                return true;
        }
        return false;
    }

    Descriptor::Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                           PeShaderStageFlags stage,
                           bool pushDescriptor,
                           const std::string &name)
        : m_pushDescriptor{pushDescriptor},
          m_bindingInfos{bindingInfos},
          m_updateInfos(bindingInfos.size()),
          m_boundResources(bindingInfos.size()),
          m_stage{stage},
          m_name{name},
          m_dynamicOffsets{}
    {
        std::sort(m_bindingInfos.begin(), m_bindingInfos.end(), [](const DescriptorBindingInfo &a, const DescriptorBindingInfo &b)
                  { return a.binding < b.binding; });
        for (DescriptorBindingInfo &bindingInfo : m_bindingInfos)
        {
            if (bindingInfo.dxRegister == static_cast<uint32_t>(-1))
                bindingInfo.dxRegister = bindingInfo.binding;
        }

        m_layout = DescriptorLayout::GetOrCreate(m_bindingInfos, m_stage, m_pushDescriptor);
        if (!HasSceneTextureArray(m_bindingInfos))
        {
            if (!m_bindingInfos.empty() && m_bindingInfos.back().bindless)
                m_variableCount = m_bindingInfos.back().count;

            CreateBackend();
            return;
        }

        // Registered under the same lock that reads the capacity, so a concurrent resize cannot miss it.
        BindlessRegistry &registry = Bindless();
        std::lock_guard<std::mutex> lock(registry.mutex);
        m_variableCount = std::min(m_bindingInfos.back().count, registry.capacity);
        CreateBackend();
        registry.descriptors.insert(this);
    }

    void Descriptor::CreateBackend()
    {
        try
        {
            m_pool = CreatePool();
            m_impl = CreateDescriptorImpl(this);
        }
        catch (const std::exception &error)
        {
            // Backends throw without logging so a failed resize can fall back quietly; a new descriptor cannot.
            DescriptorPool::Destroy(m_pool);
            PE_ERROR("[Descriptor] '%s': %s", m_name.c_str(), error.what());
        }
    }

    Descriptor::~Descriptor()
    {
        if (HasSceneTextureArray(m_bindingInfos))
        {
            BindlessRegistry &registry = Bindless();
            std::lock_guard<std::mutex> lock(registry.mutex);
            registry.descriptors.erase(this);
        }

        delete m_impl;
        DescriptorPool::Destroy(m_pool);
    }

    void Descriptor::SetImageViews(uint32_t binding,
                                   const std::vector<ImageView *> &views,
                                   const std::vector<Sampler *> &samplers)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        const auto type = m_bindingInfos[bindingIndex].type;
        if (type != PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            type != PE_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
            type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            type != PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
        {
            PE_WARN("[Descriptor] Type mismatch: Binding %u is not an image type!", binding);
            return;
        }

        DescriptorUpdateInfo info{};
        info.binding = binding;
        const uint32_t capacity = GetBindingCapacity(bindingIndex);
        info.views = LimitDescriptorValues(views, capacity, "image views", m_name, binding);
        info.samplers = LimitDescriptorValues(samplers, capacity, "samplers", m_name, binding);
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::SetImageView(uint32_t binding, ImageView *view, Sampler *sampler)
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

        const auto type = m_bindingInfos[bindingIndex].type;
        if (type != PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
            type != PE_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
            type != PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
            type != PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
            type != PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER &&
            type != PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER)
        {
            PE_WARN("[Descriptor] Type mismatch: Binding %u is not a buffer type!", binding);
            return;
        }

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

    void Descriptor::SetSamplers(uint32_t binding, const std::vector<Sampler *> &samplers)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        const auto type = m_bindingInfos[bindingIndex].type;
        if (type != PE_DESCRIPTOR_TYPE_SAMPLER && type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        {
            PE_WARN("[Descriptor] Type mismatch: Binding %u is not a sampler type!", binding);
            return;
        }

        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.samplers = samplers;
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::SetSampler(uint32_t binding, Sampler *sampler)
    {
        SetSamplers(binding, {sampler});
    }

    void Descriptor::SetAccelerationStructure(uint32_t binding, AccelerationStructure *tlas)
    {
        int32_t bindingIndex = GetBindingIndex(binding, m_bindingInfos);
        if (bindingIndex == -1)
            return;

        const auto type = m_bindingInfos[bindingIndex].type;
        if (type != PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE)
        {
            PE_WARN("[Descriptor] Type mismatch: Binding %u is not an acceleration structure type!", binding);
            return;
        }

        DescriptorUpdateInfo info{};
        info.binding = binding;
        info.accelerationStructures = {tlas};
        m_updateInfos[bindingIndex] = info;
    }

    void Descriptor::Update()
    {
        PE_PROFILE_COUNTER("Descriptor Update Calls", 1);
        PE_PROFILE_COUNTER("Descriptor Update Bindings", m_updateInfos.size());
        PE_PROFILE_SCOPE("Descriptor Update");
        {
            PE_PROFILE_SCOPE("Descriptor Backend Update");
            m_impl->Update(m_bindingInfos, m_updateInfos);
        }

        {
            PE_PROFILE_SCOPE("Descriptor Store Bound Resources");
            for (uint32_t i = 0; i < m_updateInfos.size(); i++)
            {
                const auto &update = m_updateInfos[i];
                auto &bound = m_boundResources[i];
                // A bindless array is always written whole, even empty, so entries past the new list must not linger.
                if (m_bindingInfos[i].bindless && update.binding == m_bindingInfos[i].binding)
                {
                    bound = update;
                    continue;
                }

                if (!HasDescriptorUpdate(update))
                    continue;

                bound.binding = update.binding;

                MergeDescriptorValues(bound.views, update.views, update.views.size());
                MergeDescriptorValues(bound.samplers, update.samplers, update.samplers.size());
                MergeDescriptorValues(bound.buffers, update.buffers, update.buffers.size());
                MergeDescriptorValues(bound.offsets, update.offsets, update.buffers.size());
                MergeDescriptorValues(bound.ranges, update.ranges, update.buffers.size());
                MergeDescriptorValues(bound.accelerationStructures, update.accelerationStructures,
                                      update.accelerationStructures.size());
            }
        }

        m_updateInfos.clear();
        m_updateInfos.resize(m_bindingInfos.size());
    }
} // namespace pe
