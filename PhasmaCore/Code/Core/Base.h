#pragma once

namespace pe
{
    namespace ID
    {
        inline size_t NextID()
        {
            static size_t ID = 0;
            return ID++;
        }

        // Compile time type id
        template <class T>
        struct TypeIDHelper
        {
            static constexpr char value{};
        };

        template <class T>
        struct remove_all_pointers_and_references
        {
            using type = std::remove_cv_t<std::remove_reference_t<T>>;
        };

        template <class T>
        struct remove_all_pointers_and_references<T *> : remove_all_pointers_and_references<T>
        {
        };

        template <class T>
        using remove_all_pointers_and_references_t = typename remove_all_pointers_and_references<T>::type;

        template <class T>
        constexpr size_t GetTypeID()
        {
            static_assert(sizeof(size_t) >= sizeof(void *), "size_t is too small to hold a pointer");
            using CleanType = remove_all_pointers_and_references_t<T>;
            return reinterpret_cast<size_t>(&TypeIDHelper<CleanType>::value);
        }

        template <class T>
        constexpr size_t GetTypeID(T &&)
        {
            return GetTypeID<std::remove_reference_t<T>>();
        }
    }

    template <uint32_t N>
    struct Placeholder
    {
    };

    class NoCopy
    {
    public:
        NoCopy() = default;
        NoCopy(const NoCopy &) = delete;
        NoCopy &operator=(const NoCopy &) = delete;
        NoCopy(NoCopy &&) = default;
        NoCopy &operator=(NoCopy &&) = default;
    };

    class NoMove
    {
    public:
        NoMove() = default;
        NoMove(const NoMove &) = default;
        NoMove &operator=(const NoMove &) = default;
        NoMove(NoMove &&) = delete;
        NoMove &operator=(NoMove &&) = delete;
    };

    struct BarrierInfo
    {
        BarrierType type;
    };

    // Maintains the order of items
    template <class Key, class Value>
    class OrderedMap
    {
    public:
        using ListType = std::list<Value>;
        using iterator = typename ListType::iterator;
        using const_iterator = typename ListType::const_iterator;
        using reverse_iterator = typename ListType::reverse_iterator;
        using const_reverse_iterator = typename ListType::const_reverse_iterator;

        iterator begin() { return m_list.begin(); }
        iterator end() { return m_list.end(); }
        reverse_iterator rbegin() { return m_list.rbegin(); }
        reverse_iterator rend() { return m_list.rend(); }
        const_iterator cbegin() const { return m_list.cbegin(); }
        const_iterator cend() const { return m_list.cend(); }
        const_reverse_iterator crbegin() const { return m_list.crbegin(); }
        const_reverse_iterator crend() const { return m_list.crend(); }

        OrderedMap() = default;

        OrderedMap(size_t capacity)
        {
            m_map.reserve(capacity);
        }

        OrderedMap(std::initializer_list<std::pair<const Key, Value>> init)
        {
            for (const auto &item : init)
            {
                insert(item.first, item.second);
            }
        }

        uint32_t size() { return static_cast<uint32_t>(m_list.size()); }

        iterator find(const Key &key)
        {
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                return it->second;
            }
            return m_list.end();
        }

        const_iterator find(const Key &key) const
        {
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                return it->second;
            }
            return m_list.end();
        }

        std::optional<iterator> exists(const Key &key)
        {
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        Value &get(const Key &key)
        {
            auto it = m_map.find(key);
            if (it == m_map.end())
            {
                throw std::out_of_range("Key not found in map");
            }
            return *(it->second);
        }

        const Value &get(const Key &key) const
        {
            return *(m_map.at(key));
        }

        Value &operator[](const Key &key)
        {
            auto [it, inserted] = m_map.try_emplace(key, m_list.end());
            if (inserted)
            {
                m_list.push_back(Value());
                it->second = std::prev(m_list.end());
            }
            return *(it->second);
        }

        const Value &operator[](const Key &key) const
        {
            return *(m_map.at(key));
        }

        std::pair<bool, std::optional<iterator>> insert(const Key &key, const Value &value)
        {
            auto it = m_map.find(key);
            if (it == m_map.end())
            {
                m_list.push_back(value);
                auto iter = m_map.emplace_hint(m_map.end(), key, std::prev(m_list.end()));
                return {true, iter->second};
            }
            return {false, std::nullopt};
        }

        bool erase(const Key &key)
        {
            auto map_it = m_map.find(key);
            if (map_it != m_map.end())
            {
                m_list.erase(map_it->second);
                m_map.erase(map_it);
                return true;
            }
            return false;
        }

        void clear()
        {
            m_list.clear();
            m_map.clear();
        }

    private:
        ListType m_list;
        std::unordered_map<Key, iterator> m_map;
    };

    class ApiHandleBase
    {
    public:
        void *Get() const { return m_handle; }

    protected:
        ApiHandleBase(void *handle) : m_handle{handle} {}
        virtual ~ApiHandleBase() {}

        void *m_handle;
    };

    // Used to abstract rendering api handles
    // We can add more types here
    template <ObjectType Ob, class VK_TYPE, class DX_TYPE>
    class ApiHandle final : public ApiHandleBase
    {
        // Only work with pointers
        static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle of VK_TYPE type is not a pointer");
        static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle of DX_TYPE type is not a pointer");

    public:
        static constexpr ObjectType ObType = Ob;

        ApiHandle() : ApiHandleBase(nullptr) {}
        ApiHandle(const VK_TYPE &handle) : ApiHandleBase(handle) {}
        ApiHandle(const DX_TYPE &handle) : ApiHandleBase(handle) {}

        // Operators for auto casting
        operator VK_TYPE() const { return static_cast<VK_TYPE>(m_handle); }
        operator DX_TYPE() const { return static_cast<DX_TYPE>(m_handle); }
        operator uint64_t() const { return reinterpret_cast<uint64_t>(m_handle); }
        operator bool() { return m_handle != nullptr; }
        bool operator!() { return m_handle == nullptr; }

        bool IsNull() const { return m_handle == nullptr; }
    };

    class PeHandleBase
    {
    public:
        PeHandleBase() {}
        virtual ~PeHandleBase() {}

        inline static void DestroyAllIHandles()
        {
            std::vector<PeHandleBase *> toDestroy;
            for (auto apiHandle : s_allApiHandles)
            {
                toDestroy.push_back(apiHandle);
            }

            for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it)
            {
                (*it)->Suicide();
            }
        }

    protected:
        virtual void Suicide() = 0;

        inline static OrderedMap<PeHandleBase *, PeHandleBase *> s_allApiHandles{};
    };

    // Manages a class that contains handles
    // Disallows moving the actual objects and prodiving a Create and Destroy funtion for every PeHandle,
    // which manages the memory for heap allocations through Create
    //
    // Important: Every PeHandle should provide a constructor and a destructor managing the
    // create and destroy of the API_HANDLE
    template <class T, class API_HANDLE>
    class PeHandle : public PeHandleBase, public NoCopy, public NoMove
    {
    public:
        template <class... Params>
        inline static T *Create(Params &&...params)
        {
            ValidateBaseClass<ApiHandleBase, API_HANDLE>();
            ValidateBaseClass<PeHandle<T, API_HANDLE>, T>();

            T *ptr = new T(std::forward<Params>(params)...);

            // Useful for deleting
            s_allApiHandles.insert(ptr, ptr);

            return ptr;
        }

        inline static void Destroy(T *&ptr)
        {
            ValidateBaseClass<ApiHandleBase, API_HANDLE>();
            ValidateBaseClass<PeHandle<T, API_HANDLE>, T>();

            if (ptr && s_allApiHandles.erase(ptr))
                delete ptr; // should call ~T() destructor

            ptr = nullptr;
        }

        virtual ~PeHandle() {}
        API_HANDLE &ApiHandle() { return m_apiHandle; }
        const API_HANDLE &ApiHandle() const { return m_apiHandle; }

    protected:
        PeHandle() : m_apiHandle{} {}
        PeHandle(const API_HANDLE &apiHandle) : m_apiHandle{apiHandle} {}

        void Suicide() override
        {
            T *temp = static_cast<T *>(this);
            Destroy(temp);
        }

        API_HANDLE m_apiHandle;
    };

    using AllocationApiHandle = ApiHandle<ObjectType::Unknown, VmaAllocation, Placeholder<0> *>;
    using AllocatorApiHandle = ApiHandle<ObjectType::Unknown, VmaAllocator, Placeholder<0> *>;
    using BufferApiHandle = ApiHandle<ObjectType::Buffer, VkBuffer, Placeholder<0> *>;
    using CommandBufferApiHandle = ApiHandle<ObjectType::CommandBuffer, VkCommandBuffer, Placeholder<0> *>;
    using CommandPoolApiHandle = ApiHandle<ObjectType::CommandPool, VkCommandPool, Placeholder<0> *>;
    using DebugMessengerApiHandle = ApiHandle<ObjectType::DebugUtilsMessenger, VkDebugUtilsMessengerEXT, Placeholder<0> *>;
    using DescriptorPoolApiHandle = ApiHandle<ObjectType::DescriptorPool, VkDescriptorPool, Placeholder<0> *>;
    using DescriptorSetApiHandle = ApiHandle<ObjectType::DescriptorSet, VkDescriptorSet, Placeholder<0> *>;
    using DescriptorSetLayoutApiHandle = ApiHandle<ObjectType::DescriptorSetLayout, VkDescriptorSetLayout, Placeholder<0> *>;
    using DeviceApiHandle = ApiHandle<ObjectType::Device, VkDevice, Placeholder<0> *>;
    using EventApiHandle = ApiHandle<ObjectType::Event, VkEvent, Placeholder<0> *>;
    using FramebufferApiHandle = ApiHandle<ObjectType::Framebuffer, VkFramebuffer, Placeholder<0> *>;
    using GpuApiHandle = ApiHandle<ObjectType::PhysicalDevice, VkPhysicalDevice, Placeholder<0> *>;
    using ImageApiHandle = ApiHandle<ObjectType::Image, VkImage, Placeholder<0> *>;
    using ImageViewApiHandle = ApiHandle<ObjectType::ImageView, VkImageView, Placeholder<0> *>;
    using InstanceApiHandle = ApiHandle<ObjectType::Instance, VkInstance, Placeholder<0> *>;
    using PipelineCacheApiHandle = ApiHandle<ObjectType::PipelineCache, VkPipelineCache, Placeholder<0> *>;
    using PipelineApiHandle = ApiHandle<ObjectType::Pipeline, VkPipeline, Placeholder<0> *>;
    using PipelineLayoutApiHandle = ApiHandle<ObjectType::PipelineLayout, VkPipelineLayout, Placeholder<0> *>;
    using QueryPoolApiHandle = ApiHandle<ObjectType::QueryPool, VkQueryPool, Placeholder<0> *>;
    using QueueApiHandle = ApiHandle<ObjectType::Queue, VkQueue, Placeholder<0> *>;
    using RenderPassApiHandle = ApiHandle<ObjectType::RenderPass, VkRenderPass, Placeholder <0>*>;
    using SamplerApiHandle = ApiHandle<ObjectType::Sampler, VkSampler, Placeholder<0> *>;
    using SemaphoreApiHandle = ApiHandle<ObjectType::Semaphore, VkSemaphore, Placeholder<0> *>;
    using ShaderApiHandle = ApiHandle<ObjectType::Unknown, Placeholder<0> *, Placeholder<1> *>;
    using SurfaceApiHandle = ApiHandle<ObjectType::Surface, VkSurfaceKHR, Placeholder<0> *>;
    using SwapchainApiHandle = ApiHandle<ObjectType::Swapchain, VkSwapchainKHR, Placeholder<0> *>;
}
