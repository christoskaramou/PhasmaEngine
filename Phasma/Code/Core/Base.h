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

namespace pe
{
    inline void PE_ERROR_MSG(const std::string &msg,
                             const std::string &func,
                             const std::string &file,
                             int line)
    {
        std::string error = "Error: " + msg + ", \nFunc: " + func + ", \nFile: " + file + ", \nLine: " + std::to_string(line) + "\n\n";
        std::cout << error << std::endl;
        exit(-1);
    }
    
    inline void PE_CHECK_RESULT(uint32_t res,
                                const std::string &func,
                                const std::string &file,
                                int line)
    {
        if (res != 0)
            PE_ERROR_MSG("Result error", func, file, line);
    }

    inline void PE_CHECK_RESULT(uint32_t res)
    {
        if (res != 0)
            exit(-1);
    }

#if _DEBUG
#define PE_CHECK(res) PE_CHECK_RESULT(res, __func__, __FILE__, __LINE__)
#define PE_ERROR(msg) PE_ERROR_MSG(msg, __func__, __FILE__, __LINE__)
#else
#define PE_CHECK(res) PE_CHECK_RESULT(res)
#define PE_ERROR(msg) exit(-1)
#endif

    constexpr uint32_t BitSet(uint8_t bit)
    {
        return 1 << bit;
    }

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
    template <class VK_TYPE, class DX_TYPE>
    class ApiHandle final
    {
        // Only work with pointers
        static_assert(std::is_pointer_v<VK_TYPE>, "ApiHandle type is not a pointer");
        static_assert(std::is_pointer_v<DX_TYPE>, "ApiHandle type is not a pointer");

    public:
        using BaseVK = VK_TYPE;
        using BaseDX = DX_TYPE;

        ApiHandle() : m_handle(nullptr) {}
        ApiHandle(const VK_TYPE &handle) : m_handle(handle) {}
        ApiHandle(const DX_TYPE &handle) : m_handle(handle) {}

        // Operators for auto casting
        operator VK_TYPE() { return static_cast<VK_TYPE>(m_handle); }
        operator VK_TYPE() const { return static_cast<VK_TYPE>(m_handle); }
        operator DX_TYPE() { return static_cast<DX_TYPE>(m_handle); }
        operator DX_TYPE() const { return static_cast<DX_TYPE>(m_handle); }
        operator uintptr_t() { return reinterpret_cast<uintptr_t>(m_handle); }
        operator bool() { return m_handle != nullptr; }
        bool operator!() { return m_handle == nullptr; }

    private:
        void *m_handle;
    };

    // Manages a class that contains handles
    // Disallows moving the actual objects and prodiving a Create and Destroy funtion for every IHandle,
    // which manages the memory for heap allocations through Create
    //
    // Important: Every IHandle should provide a constructor and a destructor managing the
    // create and destroy of the HANDLE
    template <class T, class HANDLE>
    class IHandle : public NoCopy, public NoMove
    {
    public:
        template <class... Params>
        inline static T *Create(Params &&...params)
        {
            static_assert(std::is_base_of_v<IHandle<T, HANDLE>, T>);

            T *ptr = new T(std::forward<Params>(params)...);
            ptr->p = ptr;

            return ptr;
        }

        inline static void Destroy(T *ptr)
        {
            static_assert(std::is_base_of_v<IHandle<T, HANDLE>, T>);

            if (ptr && ptr->p)
            {
                delete ptr->p;
                ptr->p = nullptr;
            }
        }

        virtual ~IHandle() {}
        HANDLE &Handle() { return m_handle; }
        const HANDLE &Handle() const { return m_handle; }

    protected:
        IHandle() : m_handle{} {}
        HANDLE m_handle;

    private:
        T *p;
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
    using AllocationCreateFlags = uint32_t;
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
    using ObjectType = uint32_t;

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
    using QueueHandle = ApiHandle<VkQueue, Placeholder *>;
    using DescriptorPoolHandle = ApiHandle<VkDescriptorPool, Placeholder *>;
    using AllocationHandle = ApiHandle<VmaAllocation, Placeholder *>;
    using AllocatorHandle = ApiHandle<VmaAllocator, Placeholder *>;
}
