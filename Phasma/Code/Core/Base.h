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

#pragma once

#if _DEBUG
    #define PE_PRINT_ERROR_MSG(msg)         \
        {                                   \
            std::stringstream ss;           \
            ss << "Error: " << msg;         \
            ss << ", File: " << __FILE__;   \
            ss << ", Line: " << __LINE__;   \
            std::cout << ss.str();          \
        }
#else
    #define PE_PRINT_ERROR_MSG(msg)
#endif

#define PE_ERROR(msg)               \
    {                               \
        PE_PRINT_ERROR_MSG(msg);    \
        exit(-1);                   \
    }

namespace pe
{
    inline size_t NextID()
    {
        static size_t ID = 0;
        return ID++;
    }

    template<class T>
    inline size_t GetTypeID()
    {
        static size_t typeID = NextID();
        return typeID;
    }

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

    // Used to abstract rendering api handles
    template<class VK_TYPE, class DX_TYPE>
    class ApiHandle final
    {
        // Only work with pointers
        static_assert(std::is_pointer_v<VK_TYPE>,
        "ApiHandle type is not a pointer");
        static_assert(std::is_pointer_v<DX_TYPE>,
        "ApiHandle type is not a pointer");
    public:
        using BaseVK = VK_TYPE;
        using BaseDX = DX_TYPE;

        ApiHandle() : m_handle(nullptr)
        {};

        ApiHandle(const VK_TYPE &handle) : m_handle(handle)
        {}

        ApiHandle(const DX_TYPE &handle) : m_handle(handle)
        {}

        // Operators for auto casting
        operator VK_TYPE()
        { return static_cast<VK_TYPE>(m_handle); }

        operator VK_TYPE() const
        { return static_cast<VK_TYPE>(m_handle); }

        operator DX_TYPE()
        { return static_cast<DX_TYPE>(m_handle); }

        operator DX_TYPE() const
        { return static_cast<DX_TYPE>(m_handle); }

        // Not used at the moment
        void *raw()
        { return m_handle; }

        operator bool()
        {
            return m_handle != nullptr;
        }

        bool operator!()
        {
            return m_handle == nullptr;
        }

    private:
        void *m_handle;
    };

    // Manages a class that contains handles
    // Disallows moving the actual objects and prodiving a Create and Destroy funtion for every IHandle,
    // which manages the memory for heap allocations through Create
    //
    // Important: Every IHandle should provide a constructor and a destructor managing the
    // create and destroy of the HANDLE
    template<class T, class HANDLE>
    class IHandle : public NoCopy, public NoMove
    {
    public:
        template<class ... Params>
        inline static T *Create(Params &&... params)
        {
            // This means the T class is IHandle and already defined
            static_assert(std::is_base_of_v < IHandle<T, HANDLE>, T > );

            T *ptr = new T(std::forward<Params>(params)...);

            // m_heapId is used to check on Destroy if the object was created via Create
            // Thus it is a heap allocated object and must be deleted
            ptr->m_heapId = NextID();

            sm_iHandles[ptr->m_heapId] = ptr;

            return ptr;
        }

        virtual ~IHandle()
        {}

        void Destroy()
        {
            // If the object is not created via Create function, return and let the actual class to manage
            if (m_heapId == std::numeric_limits<size_t>::max())
                return;

            auto it = sm_iHandles.find(m_heapId);
            if (it != sm_iHandles.end())
            {
                sm_iHandles.erase(it);

                // This will call the derived object's destructor too, because the destructor of IHandle is virtual
                delete this;
            }
        };

        HANDLE &Handle()
        { return m_handle; }

        const HANDLE &Handle() const
        { return m_handle; }

    private:
        inline static std::unordered_map<size_t, IHandle *> sm_iHandles{};
        size_t m_heapId;

    protected:
        IHandle() : m_handle{}, m_heapId{std::numeric_limits<size_t>::max()}
        {}

        HANDLE m_handle;
    };

    struct Placeholder
    {
    };

    using SampleCountFlagBits = uint32_t;
    using SharingMode = uint32_t;
    using Format = uint32_t;
    using ColorSpace = uint32_t;
    using ImageLayout = uint32_t;
    using ImageTiling = uint32_t;
    using Filter = uint32_t;
    using ImageViewType = uint32_t;
    using SamplerAddressMode = uint32_t;
    using BorderColor = uint32_t;
    using CompareOp = uint32_t;
    using SamplerMipmapMode = uint32_t;
    using PipelineStageFlags = uint32_t;
    using AccessFlags = uint32_t;
    using MemoryPropertyFlags = uint32_t;
    using BufferUsageFlags = uint32_t;
    using ImageCreateFlags = uint32_t;
    using ImageAspectFlags = uint32_t;
    using ImageUsageFlags = uint32_t;
    using ImageType = uint32_t;
    using VertexInputRate = uint32_t;
    using DynamicState = uint32_t;
    using ShaderStageFlags = uint32_t;
    using DescriptorType = uint32_t;
    using AttachmentDescriptionFlags = uint32_t;
    using AttachmentLoadOp = uint32_t;
    using AttachmentStoreOp = uint32_t;
    using IndexType = uint32_t;
    using BlendFactor = uint32_t;
    using BlendOp = uint32_t;
    using ColorComponentFlags = uint32_t;
    using PresentMode = uint32_t;

    using CommandBufferHandle = ApiHandle<VkCommandBuffer, Placeholder *>;
    using DescriptorSetLayoutHandle = ApiHandle<VkDescriptorSetLayout, Placeholder *>;
    using DescriptorSetHandle = ApiHandle<VkDescriptorSet, Placeholder *>;
    using FrameBufferHandle = ApiHandle<VkFramebuffer, Placeholder *>;
    using ImageHandle = ApiHandle<VkImage, Placeholder *>;
    using ImageViewHandle = ApiHandle<VkImageView, Placeholder *>;
    using SamplerHandle = ApiHandle<VkSampler, Placeholder *>;
    using RenderPassHandle = ApiHandle<VkRenderPass, Placeholder *>;
    using CommandPoolHandle = ApiHandle<VkCommandPool, Placeholder *>;
    using BufferHandle = ApiHandle<VkBuffer, Placeholder *>;
    using PipelineCacheHandle = ApiHandle<VkPipelineCache, Placeholder *>;
    using PipelineLayoutHandle = ApiHandle<VkPipelineLayout, Placeholder *>;
    using PipelineHandle = ApiHandle<VkPipeline, Placeholder *>;
    using FenceHandle = ApiHandle<VkFence, Placeholder *>;
    using SemaphoreHandle = ApiHandle<VkSemaphore, Placeholder *>;
    using QueryPoolHandle = ApiHandle<VkQueryPool, Placeholder *>;
    using SwapchainHandle = ApiHandle<VkSwapchainKHR, Placeholder *>;
    using DeviceHandle = ApiHandle<VkDevice, Placeholder *>;
    using SurfaceHandle = ApiHandle<VkSurfaceKHR, Placeholder *>;
    using InstanceHandle = ApiHandle<VkInstance, Placeholder *>;
    using GpuHandle = ApiHandle<VkPhysicalDevice, Placeholder *>;
    using DebugMessengerHandle = ApiHandle<VkDebugUtilsMessengerEXT, Placeholder *>;
    using DeviceQueueHandle = ApiHandle<VkQueue, Placeholder *>;
    using DescriptorPoolHandle = ApiHandle<VkDescriptorPool, Placeholder *>;
}
