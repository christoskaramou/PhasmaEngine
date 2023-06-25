#pragma once

namespace pe
{
    inline size_t NextID()
    {
        static size_t ID = 0;
        return ID++;
    }

    template <typename T>
    struct TypeIDHelper
    {
        static constexpr char value{};
    };

    template <typename T>
    constexpr size_t GetTypeID()
    {
        static_assert(sizeof(size_t) >= sizeof(void *), "size_t is too small to hold a pointer");
        return reinterpret_cast<size_t>(&TypeIDHelper<T>::value);
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

    // Maintains the order items added
    template <class Key, class T>
    class OrderedMap
    {
    public:
        using ListType = std::list<T>;
        using iterator = typename ListType::iterator;
        using const_iterator = typename ListType::const_iterator;
        using reverse_iterator = typename ListType::reverse_iterator;
        using const_reverse_iterator = typename ListType::const_reverse_iterator;

        uint32_t size() { return static_cast<uint32_t>(m_list.size()); }

        bool exists(const Key &key) const { return m_map.find(key) != m_map.end(); }

        T &get(const Key &key)
        {
            auto it = m_map.find(key);
            if (it == m_map.end())
            {
                throw std::out_of_range("Key not found in map");
            }
            return *(it->second);
        }

        const T &get(const Key &key) const { return *(m_map.at(key)); }

        T &operator[](const Key &key)
        {
            auto it = m_map.find(key);
            if (it == m_map.end())
            {
                m_list.push_back(T());
                it = m_map.insert({key, std::prev(m_list.end())}).first;
            }
            return *(it->second);
        }

        const T &operator[](const Key &key) const { return *(m_map.at(key)); }

        iterator begin() { return m_list.begin(); }
        iterator end() { return m_list.end(); }
        reverse_iterator rbegin() { return m_list.rbegin(); }
        reverse_iterator rend() { return m_list.rend(); }
        const_iterator cbegin() const { return m_list.cbegin(); }
        const_iterator cend() const { return m_list.cend(); }
        const_reverse_iterator crbegin() const { return m_list.crbegin(); }
        const_reverse_iterator crend() const { return m_list.crend(); }

        bool insert(const Key &key, const T &value)
        {
            if (m_map.find(key) == m_map.end())
            {
                m_list.push_back(value);
                m_map[key] = std::prev(m_list.end());
                return true;
            }
            return false;
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
    template <class VK_TYPE, class DX_TYPE>
    class ApiHandle final : public ApiHandleBase
    {
        // Only work with pointers
        static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle of VK_TYPE type is not a pointer");
        static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle of DX_TYPE type is not a pointer");

    public:
        using VKType = VK_TYPE;
        using DXType = DX_TYPE;

        ApiHandle() : ApiHandleBase(nullptr) {}
        ApiHandle(const VK_TYPE &handle) : ApiHandleBase(handle) {}
        ApiHandle(const DX_TYPE &handle) : ApiHandleBase(handle) {}

        // Operators for auto casting
        operator VK_TYPE() { return static_cast<VK_TYPE>(m_handle); }
        operator VK_TYPE() const { return static_cast<VK_TYPE>(m_handle); }
        operator DX_TYPE() { return static_cast<DX_TYPE>(m_handle); }
        operator DX_TYPE() const { return static_cast<DX_TYPE>(m_handle); }
        operator uintptr_t() { return reinterpret_cast<uintptr_t>(m_handle); }
        operator bool() { return m_handle != nullptr; }
        bool operator!() { return m_handle == nullptr; }

        bool IsNull() const { return m_handle == nullptr; }
    };

    class IHandleBase
    {
    public:
        IHandleBase() : m_id{NextID()} {}

        virtual ~IHandleBase() {}

        size_t GetID() const { return m_id; }

        inline static void DestroyAllIHandles()
        {
            for (auto it = s_allHandles.rbegin(); it != s_allHandles.rend(); ++it)
            {
                (*it)->Suicide();
            }

            s_allHandles.clear();
        }

    protected:
        virtual void Suicide() { PE_ERROR("Unused Base"); }

        inline static OrderedMap<IHandleBase *, IHandleBase *> s_allHandles{};
        size_t m_id;
    };

    // Manages a class that contains handles
    // Disallows moving the actual objects and prodiving a Create and Destroy funtion for every IHandle,
    // which manages the memory for heap allocations through Create
    //
    // Important: Every IHandle should provide a constructor and a destructor managing the
    // create and destroy of the HANDLE
    template <class T, class HANDLE>
    class IHandle : public IHandleBase, public NoCopy, public NoMove
    {
    public:
        template <class... Params>
        inline static T *Create(Params &&...params)
        {
            ValidateBaseClass<ApiHandleBase, HANDLE>();
            ValidateBaseClass<IHandle<T, HANDLE>, T>();

            T *ptr = new T(std::forward<Params>(params)...);

            // Useful for deleting
            s_allHandles.insert(ptr, ptr);

            return ptr;
        }

        inline static void Destroy(T *ptr)
        {
            ValidateBaseClass<ApiHandleBase, HANDLE>();
            ValidateBaseClass<IHandle<T, HANDLE>, T>();

            if (ptr && s_allHandles.erase(ptr))
                delete ptr;
        }

        virtual ~IHandle() {}
        HANDLE &Handle() { return m_handle; }
        const HANDLE &Handle() const { return m_handle; }

    protected:
        IHandle() : m_handle{} {}
        HANDLE m_handle;

    private:
        void Suicide() override { Destroy(static_cast<T *>(this)); }
    };

    using CommandBufferHandle = ApiHandle<VkCommandBuffer, Placeholder<0> *>;
    using DescriptorSetLayoutHandle = ApiHandle<VkDescriptorSetLayout, Placeholder<0> *>;
    using DescriptorSetHandle = ApiHandle<VkDescriptorSet, Placeholder<0> *>;
    using FrameBufferHandle = ApiHandle<VkFramebuffer, Placeholder<0> *>;
    using ImageHandle = ApiHandle<VkImage, Placeholder<0> *>;
    using ImageViewHandle = ApiHandle<VkImageView, Placeholder<0> *>;
    using SamplerHandle = ApiHandle<VkSampler, Placeholder<0> *>;
    using RenderPassHandle = ApiHandle<VkRenderPass, Placeholder<0> *>;
    using CommandPoolHandle = ApiHandle<VkCommandPool, Placeholder<0> *>;
    using BufferHandle = ApiHandle<VkBuffer, Placeholder<0> *>;
    using PipelineCacheHandle = ApiHandle<VkPipelineCache, Placeholder<0> *>;
    using PipelineLayoutHandle = ApiHandle<VkPipelineLayout, Placeholder<0> *>;
    using PipelineHandle = ApiHandle<VkPipeline, Placeholder<0> *>;
    using SemaphoreHandle = ApiHandle<VkSemaphore, Placeholder<0> *>;
    using QueryPoolHandle = ApiHandle<VkQueryPool, Placeholder<0> *>;
    using SwapchainHandle = ApiHandle<VkSwapchainKHR, Placeholder<0> *>;
    using DeviceHandle = ApiHandle<VkDevice, Placeholder<0> *>;
    using SurfaceHandle = ApiHandle<VkSurfaceKHR, Placeholder<0> *>;
    using InstanceHandle = ApiHandle<VkInstance, Placeholder<0> *>;
    using GpuHandle = ApiHandle<VkPhysicalDevice, Placeholder<0> *>;
    using DebugMessengerHandle = ApiHandle<VkDebugUtilsMessengerEXT, Placeholder<0> *>;
    using QueueHandle = ApiHandle<VkQueue, Placeholder<0> *>;
    using DescriptorPoolHandle = ApiHandle<VkDescriptorPool, Placeholder<0> *>;
    using AllocationHandle = ApiHandle<VmaAllocation, Placeholder<0> *>;
    using AllocatorHandle = ApiHandle<VmaAllocator, Placeholder<0> *>;
    using WindowHandle = ApiHandle<SDL_Window *, Placeholder<0> *>;
    using ShaderHandle = ApiHandle<Placeholder<0> *, Placeholder<1> *>;
}
