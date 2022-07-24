#pragma once

#define RHII (*RHI::Get()) // RHI Instance
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
            static RHI *rhi = new RHI();
            return rhi;
        }

        static void Remove()
        {
            if (Get())
                delete Get();
        }

        ~RHI();

        void Init(SDL_Window *window);
        void Destroy();
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
        void InitQueues();
        void CreateSwapchain(Surface *surface);
        void InitCommandPools();
        void CreateDescriptorPool(uint32_t maxDescriptorSets);
        void CreateGlobalDescriptors();
        void InitCmdBuffers(uint32_t bufferCount = 0);
        void CreateSemaphores(uint32_t semaphoresCount);

        Format GetDepthFormat();

        void WaitDeviceIdle();

        void NextFrame() { m_frameCounter++; }
        uint32_t GetFrameCounter() { return m_frameCounter; }
        uint32_t GetFrameIndex() { return m_frameCounter % SWAPCHAIN_IMAGES; }
        // For dynamic uniform buffers that are dependent on the frame index
        uint32_t GetFrameDynamicOffset(size_t size, uint32_t frameIndex) { return static_cast<uint32_t>(size / SWAPCHAIN_IMAGES) * frameIndex; }
        uint32_t GetMaxUniformBufferSize() { return m_maxUniformBufferSize; }
        uint32_t GetMaxStorageBufferSize() { return m_maxStorageBufferSize; }
        uint64_t GetMinUniformBufferOffsetAlignment() { return m_minUniformBufferOffsetAlignment; }
        uint64_t GetMinStorageBufferOffsetAlignment() { return m_minStorageBufferOffsetAlignment; }
        size_t AlignUniform(size_t size) { return (size + m_minUniformBufferOffsetAlignment - 1) & ~(m_minUniformBufferOffsetAlignment - 1); }
        size_t AlignStorage(size_t size) { return (size + m_minStorageBufferOffsetAlignment - 1) & ~(m_minStorageBufferOffsetAlignment - 1); }

        const InstanceHandle &GetInstance() { return m_instance; }
        const GpuHandle &GetGpu() { return m_gpu; }
        const std::string &GetGpuName() { return m_gpuName; }
        const DeviceHandle &GetDevice() { return m_device; }
        DescriptorPool *GetDescriptorPool() { return m_descriptorPool; }
        std::vector<Semaphore *> &GetSemaphores() { return m_semaphores; }
        const AllocatorHandle &GetAllocator() { return m_allocator; }
        Queue *GetRenderQueue(uint32_t index) { return m_renderQueue[index]; }
        Queue *GetPresentQueue(uint32_t index) { return m_presentQueue[index]; }
        Queue *GetComputeQueue(uint32_t index) { return m_computeQueue[index]; }
        SDL_Window *GetWindow() { return m_window; }
        Surface *GetSurface() { return m_surface; }
        Swapchain *GetSwapchain() { return m_swapchain; }

        struct UniformBufferInfo
        {
            Buffer *buffer = nullptr;
            size_t size = 0;
            Descriptor *descriptor = nullptr;
        };

        size_t CreateUniformBufferInfo();
        void RemoveUniformBufferInfo(size_t index);
        UniformBufferInfo &GetUniformBufferInfo(size_t key) { return m_uniformBuffers[key]; }

        struct UniformImageInfo
        {
            uint32_t count = 0;
            Descriptor *descriptor = nullptr;
        };

        size_t CreateUniformImageInfo();
        void RemoveUniformImageInfo(size_t index);
        UniformImageInfo &GetUniformImageInfo(size_t key) { return m_uniformImages[key]; }

        uint64_t GetMemoryUsageSnapshot();

    private:
        RHI();

        InstanceHandle m_instance;
        GpuHandle m_gpu;
        std::string m_gpuName;
        DeviceHandle m_device;
        DescriptorPool *m_descriptorPool;
        std::vector<Semaphore *> m_semaphores;
        AllocatorHandle m_allocator;

        Queue *m_renderQueue[SWAPCHAIN_IMAGES];
        Queue *m_presentQueue[SWAPCHAIN_IMAGES];
        Queue *m_computeQueue[SWAPCHAIN_IMAGES];

        SDL_Window *m_window;
        Surface *m_surface;
        Swapchain *m_swapchain;

        uint32_t m_frameCounter;

        std::unordered_map<size_t, UniformBufferInfo> m_uniformBuffers;
        std::unordered_map<size_t, UniformImageInfo> m_uniformImages;

        // Limits
        uint32_t m_maxUniformBufferSize;
        uint32_t m_maxStorageBufferSize;
        uint64_t m_minUniformBufferOffsetAlignment;
        uint64_t m_minStorageBufferOffsetAlignment;
    };
}