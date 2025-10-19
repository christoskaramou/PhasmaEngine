#pragma once

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
        uint64_t used = 0;   // heapUsage (ALL apps) from VK_EXT_memory_budget
        uint64_t budget = 0; // min(heapBudget, heapSize)
        uint64_t size = 0;   // heap size
        uint64_t app = 0;    // OUR committed bytes (from VMA)
        uint64_t other = 0;  // used - app (clamped >= 0)
        uint32_t heaps = 0;
    };

    struct GpuMemorySnapshot
    {
        MemoryInfo vram; // DEVICE_LOCAL
        MemoryInfo host; // non-DEVICE_LOCAL
    };

    struct SystemProcMem
    {
        // System
        uint64_t sysTotal = 0;
        uint64_t sysUsed = 0;

        // Our process
        uint64_t procWorkingSet = 0;
        uint64_t procPrivateBytes = 0;
        uint64_t procCommit = 0;
        uint64_t procPeakWS = 0;
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
        vk::Format GetDepthFormat();
        void WaitDeviceIdle();
        void NextFrame() { m_frameCounter++; }
        uint32_t GetFrameCounter() { return m_frameCounter; }
        uint32_t GetFrameIndex() { return m_frameCounter % GetSwapchainImageCount(); }
        // For dynamic uniform buffers that are dependent on the frame index
        uint32_t GetFrameDynamicOffset(size_t size, uint32_t frameIndex) { return static_cast<uint32_t>(size / GetSwapchainImageCount()) * frameIndex; }
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
        vk::Instance GetInstance() { return m_instance; }
        vk::PhysicalDevice GetGpu() { return m_gpu; }
        const std::string &GetGpuName() { return m_gpuName; }
        vk::Device GetDevice() { return m_device; }
        DescriptorPool *GetDescriptorPool() { return m_descriptorPool; }
        Semaphore *AcquireBinarySemaphore(uint32_t frame);
        void ReturnBinarySemaphores(uint32_t frame);
        VmaAllocator GetAllocator() { return m_allocator; }
        Queue *GetMainQueue() { return m_mainQueue; }
        SDL_Window *GetWindow() { return m_window; }
        Surface *GetSurface() { return m_surface; }
        Swapchain *GetSwapchain() { return m_swapchain; }
        uint32_t GetSwapchainImageCount();
        SystemProcMem GetSystemAndProcessMemory();
        GpuMemorySnapshot GetGpuMemorySnapshot();
        void ChangePresentMode(vk::PresentModeKHR mode);
        const char *PresentModeToString(vk::PresentModeKHR presentMode);

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        float GetWidthf() const;
        float GetHeightf() const;

    private:
        RHI() = default;

        vk::Instance m_instance;
        vk::PhysicalDevice m_gpu;
        std::string m_gpuName;
        vk::Device m_device;
        DescriptorPool *m_descriptorPool;
        std::mutex m_binarySemaphoresMutex;
        std::vector<std::stack<Semaphore *>> m_binarySemaphores;
        std::vector<std::stack<Semaphore *>> m_usedBinarySemaphores;
        VmaAllocator m_allocator;
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

    inline RHI &RHII = *RHI::Get();
}
