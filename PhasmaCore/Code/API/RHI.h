#pragma once

#define RHII (*RHI::Get()) // RHI Instance
#define WIDTH RHII.GetSurface()->GetActualExtent().width
#define HEIGHT RHII.GetSurface()->GetActualExtent().height
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

    struct MemoryInfo
    {
        uint64_t total = 0;
        uint64_t used = 0;
    };

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
        void CreateSwapchain(Surface *surface);
        void CreateDescriptorPool(uint32_t maxDescriptorSets);
        void CreateGlobalDescriptors();
        void CreateSemaphores(uint32_t semaphoresCount);
        void InitDownSampler();
        Format GetDepthFormat();
        void WaitDeviceIdle();
        void NextFrame() { m_frameCounter++; }
        uint32_t GetFrameCounter() { return m_frameCounter; }
        uint32_t GetFrameIndex() { return m_frameCounter % SWAPCHAIN_IMAGES; }
        // For dynamic uniform buffers that are dependent on the frame index
        uint32_t GetFrameDynamicOffset(size_t size, uint32_t frameIndex) { return static_cast<uint32_t>(size / SWAPCHAIN_IMAGES) * frameIndex; }
        uint32_t GetMaxUniformBufferSize() { return m_maxUniformBufferSize; }
        uint32_t GetMaxStorageBufferSize() { return m_maxStorageBufferSize; }
        uint32_t GetMaxDrawIndirectCount() { return m_maxDrawIndirectCount; }
        uint64_t GetMinUniformBufferOffsetAlignment() { return m_minUniformBufferOffsetAlignment; }
        uint64_t GetMinStorageBufferOffsetAlignment() { return m_minStorageBufferOffsetAlignment; }
        size_t AlignUniform(size_t size) { return (size + m_minUniformBufferOffsetAlignment - 1) & ~(m_minUniformBufferOffsetAlignment - 1); }
        size_t AlignStorage(size_t size) { return (size + m_minStorageBufferOffsetAlignment - 1) & ~(m_minStorageBufferOffsetAlignment - 1); }
        template <size_t As>
        size_t AlignStorageAs(size_t size)
        {
            size_t alignStorage = (size + As - 1) & ~(As - 1);
            PE_ERROR_IF(alignStorage != AlignStorage(alignStorage), "Alignment error");
            return alignStorage;
        }
        uint32_t GetMaxPushDescriptorsPerSet() { return m_maxPushDescriptorsPerSet; }
        uint32_t GetMaxPushConstantsSize() { return m_maxPushConstantsSize; }
        const InstanceApiHandle &GetInstance() { return m_instance; }
        const GpuApiHandle &GetGpu() { return m_gpu; }
        const std::string &GetGpuName() { return m_gpuName; }
        const DeviceApiHandle &GetDevice() { return m_device; }
        DescriptorPool *GetDescriptorPool() { return m_descriptorPool; }
        Semaphore *GetFreeBinarySemaphore(uint32_t frame);
        void ClaimUsedBinarySemaphores(uint32_t frame);
        const AllocatorApiHandle &GetAllocator() { return m_allocator; }
        Queue *GetMainQueue() { return m_mainQueue; }
        SDL_Window *GetWindow() { return m_window; }
        Surface *GetSurface() { return m_surface; }
        Swapchain *GetSwapchain() { return m_swapchain; }
        MemoryInfo GetMemoryUsageSnapshot();
        void ChangePresentMode(PresentMode mode);

    private:
        RHI() = default;

        InstanceApiHandle m_instance;
        GpuApiHandle m_gpu;
        std::string m_gpuName;
        DeviceApiHandle m_device;
        DescriptorPool *m_descriptorPool;
        std::mutex m_binarySemaphoresMutex;
        std::stack<Semaphore *> m_binarySemaphores[SWAPCHAIN_IMAGES];
        std::stack<Semaphore *> m_usedBinarySemaphores[SWAPCHAIN_IMAGES];
        AllocatorApiHandle m_allocator;
        Queue *m_mainQueue;
        SDL_Window *m_window;
        Surface *m_surface;
        Swapchain *m_swapchain;
        uint32_t m_frameCounter;

        // Limits
        uint32_t m_maxUniformBufferSize;
        uint32_t m_maxStorageBufferSize;
        uint64_t m_minUniformBufferOffsetAlignment;
        uint64_t m_minStorageBufferOffsetAlignment;
        uint32_t m_maxPushDescriptorsPerSet;
        uint32_t m_maxPushConstantsSize;
        uint32_t m_maxDrawIndirectCount;
    };
}
