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

    class IHandleBase
    {
    public:
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
        size_t m_id = 0;
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
            static_assert(std::is_base_of_v<IHandle<T, HANDLE>, T>);

            T *ptr = new T(std::forward<Params>(params)...);
            ptr->p = ptr;

            // Useful for deleting
            size_t id = NextID();
            ptr->m_id = id;
            s_allHandles[id] = ptr;

            return ptr;
        }

        inline static void Destroy(T *ptr)
        {
            static_assert(std::is_base_of_v<IHandle<T, HANDLE>, T>);

            if (ptr && ptr->p)
            {
                s_allHandles.erase(ptr->m_id);

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
        void Suicide() override { Destroy(static_cast<T *>(this)); }

        T *p;
    };

    struct Placeholder
    {
    };

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
