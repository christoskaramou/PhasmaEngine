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

// RHI Instance
#define RHII (*RHI::Get())
#define WIDTH RHII.GetSurface()->actualExtent.width
#define HEIGHT RHII.GetSurface()->actualExtent.height
#define WIDTH_f static_cast<float>(WIDTH)
#define HEIGHT_f static_cast<float>(HEIGHT)

namespace pe
{
    class CommandPool;
    class CommandBuffer;
    class DescriptorPool;
    class DescriptorLayout;
    class Descriptor;
    class Fence;
    class Semaphore;
    class Swapchain;
    class Image;
    class Surface;
    class Buffer;
    class Queue;

    class RHI : public NoCopy, public NoMove
    {
    public:
        static RHI *Get()
        {
            static auto rhi = new RHI();
            return rhi;
        }

        static void Remove()
        {
            if (Get())
                delete Get();
        }

        ~RHI();

        void Init(SDL_Window *window);
        void CreateInstance(SDL_Window *window);
        void CreateSurface();
        void GetSurfaceProperties();
        void FindGpu();
        void GetGraphicsFamilyId();
        void GetTransferFamilyId();
        void GetComputeFamilyId();
        bool IsInstanceExtensionValid(const char *name);
        bool IsInstanceLayerValid(const char *name);
        bool IsDeviceExtensionValid(const char *name);
        void CreateDevice();
        void CreateAllocator();
        void GetQueues();
        void CreateSwapchain(Surface *surface);
        void CreateCommandPools();
        void CreateDescriptorPool(uint32_t maxDescriptorSets);
        void CreateGlobalDescriptors();
        void CreateCmdBuffers(uint32_t bufferCount = 0);
        void CreateSemaphores(uint32_t semaphoresCount);
        void Destroy();

        Format GetDepthFormat();

        void WaitDeviceIdle();

        class UniformBuffer
        {
        public:
            Buffer *buffer = nullptr;
            size_t size = 0;
            Descriptor *descriptor = nullptr;
            DescriptorLayout *layout = nullptr;
        };

        class UniformImages
        {
        public:
            uint32_t count = 0;
            Descriptor *descriptor = nullptr;
            DescriptorLayout *layout = nullptr;
        };

        uint32_t GetFrameIndex() { return m_frameCounter % SWAPCHAIN_IMAGES; }
        size_t GetFrameCounter() { return m_frameCounter; }
        void NextFrame() { m_frameCounter++; }

        const InstanceHandle &GetInstance() { return m_instance; }
        const GpuHandle &GetGpu() { return m_gpu; }
        const std::string &GetGpuName() { return m_gpuName; }
        const DeviceHandle &GetDevice() { return m_device; }
        DescriptorPool *GetDescriptorPool() { return m_descriptorPool; }
        std::vector<Semaphore *> &GetSemaphores() { return m_semaphores; }
        const AllocatorHandle &GetAllocator() { return m_allocator; }
        Queue *GetGeneralQueue() { return m_generalQueue; }
        CommandBuffer *GetGeneralCmd() { return m_generalCmd; }
        SDL_Window *GetWindow() { return m_window; }
        Surface *GetSurface() { return m_surface; }
        Swapchain *GetSwapchain() { return m_swapchain; }

        std::deque<UniformBuffer> &GetUniformBuffers() { return m_uniformBuffers; }
        std::deque<UniformImages> &GetUniformImages() { return m_uniformImages; }

    private:
        RHI();

        InstanceHandle m_instance;
        GpuHandle m_gpu;
        std::string m_gpuName;
        DeviceHandle m_device;
        DescriptorPool *m_descriptorPool;
        std::vector<Semaphore *> m_semaphores;
        AllocatorHandle m_allocator;

        Queue *m_generalQueue;
        CommandBuffer *m_generalCmd;

        SDL_Window *m_window;
        Surface *m_surface;
        Swapchain *m_swapchain;

        size_t m_frameCounter;

        std::deque<UniformBuffer> m_uniformBuffers;
        std::deque<UniformImages> m_uniformImages;
    };
}