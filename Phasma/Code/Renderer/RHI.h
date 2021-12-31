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

#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

// RHI Instance
#define RHII (*RHI::Get())
#define WIDTH RHII.surface->actualExtent.width
#define HEIGHT RHII.surface->actualExtent.height
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

    class RHI : public NoCopy, public NoMove
    {
    private:
        RHI();

    public:
        ~RHI();

        void CreateInstance(SDL_Window *window);

        void CreateSurface();

        void GetSurfaceProperties();

        void GetGpu();

        void GetGraphicsFamilyId();

        void GetTransferFamilyId();

        void GetComputeFamilyId();

        void CreateDevice();

        void CreateAllocator();

        void GetGraphicsQueue();

        void GetTransferQueue();

        void GetComputeQueue();

        void GetQueues();

        void CreateSwapchain(Surface *surface);

        void CreateCommandPools();

        void CreateDescriptorPool(uint32_t maxDescriptorSets);

        void CreateGlobalDescriptors();

        void CreateCmdBuffers(uint32_t bufferCount = 1);

        void CreateFences(uint32_t fenceCount);

        void CreateSemaphores(uint32_t semaphoresCount);

        void CreateDepth();

        void Init(SDL_Window *window);

        void Destroy();

        InstanceHandle instance;
        DebugMessengerHandle debugMessenger;
        GpuHandle gpu;
        std::string gpuName;
        DeviceHandle device;
        DeviceQueueHandle graphicsQueue, computeQueue, transferQueue;
        CommandPool *commandPool;
        CommandPool *commandPool2;
        CommandPool *commandPoolTransfer;
        DescriptorPool *descriptorPool;
        DescriptorLayout *globalDescriptorLayout;
        Descriptor *globalDescriptor;
        // Descriptor *descriptorUniformBuffers;
        // Descriptor *descriptorStorageBuffers;
        // Descriptor *descriptorStorageTexelBuffers;
        // Descriptor *descriptorCombinedImageSamplers;
        std::vector<CommandBuffer *> dynamicCmdBuffers;
        std::vector<CommandBuffer *> shadowCmdBuffers;
        std::vector<Fence *> fences;
        std::vector<Semaphore *> semaphores;
        AllocatorHandle allocator;

        SDL_Window *window;
        Surface *surface;
        Swapchain *swapchain;
        Image *depth;
        int graphicsFamilyId, computeFamilyId, transferFamilyId;

        // Helpers
        void Submit(
            uint32_t commandBufferCount, CommandBuffer **commandBuffer,
            PipelineStageFlags *waitStage,
            uint32_t waitSemaphoreCount, Semaphore **waitSemaphore,
            uint32_t signalSemaphoreCount, Semaphore **signalSemaphore,
            Fence *signalFence,
            bool useGraphicsQueue = true);

        void WaitFence(Fence *fence);

        void SubmitAndWaitFence(
            uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
            PipelineStageFlags *waitStages,
            uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
            uint32_t signalSemaphoresCount, Semaphore **signalSemaphores,
            bool useGraphicsQueue = true);

        void Present(
            uint32_t swapchainCount, Swapchain **swapchains,
            uint32_t *imageIndices,
            uint32_t semaphorescount, Semaphore **semaphores);

#if _DEBUG
        void CreateDebugMessenger();

        void DestroyDebugMessenger();
#endif
    private:
        static inline std::mutex m_submit_mutex{};
        bool m_HasDebugUtils = false;

    public:
        void WaitAndLockSubmits();

        void WaitDeviceIdle();

        void WaitGraphicsQueue();

        void WaitTransferQueue();

        void UnlockSubmits();

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
    };
}