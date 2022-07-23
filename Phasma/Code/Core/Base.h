#pragma once

namespace pe
{
    inline size_t NextID()
    {
        static size_t ID = 0;
        return ID++;
    }

    template <class T>
    inline size_t GetTypeID()
    {
        static size_t typeID = NextID();
        return typeID;
    }

    struct PlaceHolderBase
    {
    };

    template <uint32_t N>
    struct Placeholder : PlaceHolderBase
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
            for (auto rit = s_allHandles.rbegin(); rit != s_allHandles.rend();)
            {
                IHandleBase *handle = rit->second;
                if (handle)
                    handle->Suicide();
                else
                    ++rit;
            }
        }

    protected:
        virtual void Suicide() { PE_ERROR("Unused Base"); }

        inline static std::map<size_t, IHandleBase *> s_allHandles{};
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
            ptr->m_p = ptr;

            // Useful for deleting
            s_allHandles[ptr->m_id] = ptr;

            return ptr;
        }

        inline static void Destroy(T *ptr)
        {
            ValidateBaseClass<ApiHandleBase, HANDLE>();
            ValidateBaseClass<IHandle<T, HANDLE>, T>();

            if (ptr && ptr->m_p)
            {
                if (s_allHandles.erase(ptr->m_id))
                {
                    delete ptr->m_p;
                    ptr->m_p = nullptr;
                }
            }
        }

        virtual ~IHandle() {}
        HANDLE &Handle() { return m_handle; }
        const HANDLE &Handle() const { return m_handle; }

    protected:
        IHandle() : m_handle{} {}
        HANDLE m_handle;

    private:
        void Suicide() override { Destroy(static_cast<T *>(this)); }

        T *m_p;
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
