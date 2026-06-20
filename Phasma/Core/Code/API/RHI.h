#pragma once

#include "API/RHITypes.h"

#include <string_view>

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
    class StagingManager;

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

    struct GpuLimits
    {
        uint32_t maxTextureDimension1D = 0;
        uint32_t maxTextureDimension2D = 0;
        uint32_t maxTextureDimension3D = 0;
        uint32_t maxTextureArrayLayers = 0;
        uint32_t maxBindGroups = 0;
        uint32_t maxDynamicUniformBuffersPerPipelineLayout = 0;
        uint32_t maxDynamicStorageBuffersPerPipelineLayout = 0;
        uint32_t maxSampledTexturesPerShaderStage = 0;
        uint32_t maxSamplersPerShaderStage = 0;
        uint32_t maxStorageBuffersPerShaderStage = 0;
        uint32_t maxStorageTexturesPerShaderStage = 0;
        uint32_t maxUniformBuffersPerShaderStage = 0;
        uint32_t maxUniformBufferBindingSize = 0;
        uint32_t maxStorageBufferBindingSize = 0;
        uint32_t minUniformBufferOffsetAlignment = 0;
        uint32_t minStorageBufferOffsetAlignment = 0;
        uint32_t maxVertexBuffers = 0;
        uint64_t maxBufferSize = 0;
        uint32_t maxVertexAttributes = 0;
        uint32_t maxVertexBufferArrayStride = 0;
        uint32_t maxInterStageShaderVariables = 0;
        uint32_t maxColorAttachments = 0;
        uint32_t maxComputeWorkgroupStorageSize = 0;
        uint32_t maxComputeInvocationsPerWorkgroup = 0;
        uint32_t maxComputeWorkgroupSizeX = 0;
        uint32_t maxComputeWorkgroupSizeY = 0;
        uint32_t maxComputeWorkgroupSizeZ = 0;
        uint32_t maxComputeWorkgroupsPerDimension = 0;
    };

    enum class GpuAdapterType
    {
        Unknown,
        IntegratedGpu,
        DiscreteGpu,
        Cpu,
    };

    enum class GpuAdapterPreference
    {
        Auto,
        IntegratedGpu,
        DiscreteGpu,
        Cpu,
    };

    inline constexpr const char *kGpuAdapterPreferenceSettingsKey = "gpu_adapter_preference";
    inline constexpr const char *kGpuAdapterPreferenceEnvVar = "PHASMA_GPU_ADAPTER";
    inline constexpr const char *kVulkanCpuIcdEnvVar = "PHASMA_VULKAN_CPU_ICD";

    struct GpuAdapterInfo
    {
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        GpuAdapterType type = GpuAdapterType::Unknown;
    };

    struct GpuFeatureSupport
    {
        bool textureCompressionBC = false;
        bool textureCompressionETC2 = false;
        bool textureCompressionASTC = false;
        bool drawIndirectFirstInstance = false;
        bool timestampQuery = false;
        bool occlusionQuery = false;        // boolean/sample-count occlusion queries supported
        bool preciseOcclusionQuery = false; // exact sample counts (VK occlusionQueryPrecise; always true on DX12)
        bool binaryOcclusionQuery = false;  // distinct 0/1 occlusion query type (DX12 only; VK has none)
        bool queryPoolHostReset = false;    // query pools can be reset on the host (VK hostQueryReset; DX12 n/a)
        bool conditionalRendering = false;  // GPU predication available (reserved for a later predication phase)
        bool dualSourceBlending = false;
        bool shaderClipDistance = false;
        bool shaderFloat16 = false;
        bool storageInputOutput16 = false;
        bool depthClipControl = false;
        bool depthClamp = false;
        bool geometryShader = false;
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

    struct DeletionQueue
    {
        std::deque<std::function<void()>> deletors;
        std::mutex mutex;
        void Push(std::function<void()> &&deletor);
        void Flush();
    };

    PE_API bool TryParseGpuAdapterPreferenceName(std::string_view value, GpuAdapterPreference &preference);
    PE_API const char *GpuAdapterPreferenceConfigName(GpuAdapterPreference preference);
    PE_API const char *GpuAdapterPreferenceDisplayName(GpuAdapterPreference preference);
    PE_API GpuAdapterPreference ResolveGpuAdapterPreference(std::string *warning = nullptr);

    class RHI : public NoCopy, public NoMove
    {
    public:
        struct Impl;

        struct Caps
        {
            struct DescriptorBuffer
            {
                bool supported = false;
                bool robustBufferAccess = false;
                uint64_t descriptorBufferOffsetAlignment = 1;
                size_t samplerDescriptorSize = 0;
                size_t combinedImageSamplerDescriptorSize = 0;
                size_t sampledImageDescriptorSize = 0;
                size_t storageImageDescriptorSize = 0;
                size_t uniformBufferDescriptorSize = 0;
                size_t robustUniformBufferDescriptorSize = 0;
                size_t storageBufferDescriptorSize = 0;
                size_t robustStorageBufferDescriptorSize = 0;
            };

            bool rayTracing = false;
            bool dynamicRendering = false;
            bool sync2 = false;
            bool copyCommands2 = false;
            bool extendedDynamicState = false;
            bool descriptorUpdateAfterBind = false;
            bool indirectCount = false;
            bool meshShaders = false;
            bool pushDescriptor = false;
            bool maintenance5 = false;
            uint32_t loaderApiVersion = 0;
            uint32_t instanceApiVersion = 0;
            uint32_t deviceApiVersion = 0;
            uint32_t effectiveApiVersion = 0;
            uint32_t spirvTargetVulkanVersion = 0;
            uint32_t maxPushConstantsBytes = 0;
            uint32_t maxBindlessTextures = 0;
            DescriptorBuffer descriptorBuffer{};
        };

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

        void Init(SDL_Window *window, PeGraphicsApi api = PE_GRAPHICS_API_VULKAN);
        PeGraphicsApi GetApi() const { return m_api; }
        bool UsesDozenVulkan() const;
        Impl *GetImpl() const { return m_impl; }
        void Destroy();
        void CreateInstance(SDL_Window *window);
        void CreateSurface();
        void FindGpu();
        void GetGraphicsFamilyId();
        void GetTransferFamilyId();
        void GetComputeFamilyId();
        bool IsInstanceExtensionValid(const char *name);
        bool IsInstanceLayerValid(const char *name);
        bool IsDeviceExtensionValid(const char *name);
        void CreateDevice();
        void CreateAllocator();
        void InitSwapchain();
        void CreateSwapchain(Surface *surface);
        void CreateDescriptorPool(uint32_t maxDescriptorSets);
        void CreateGlobalDescriptors();
        void CreateSemaphores(uint32_t semaphoresCount);
        void WaitDeviceIdle();
        void NextFrame() { m_frameCounter++; }
        uint32_t GetFrameCounter() { return m_frameCounter; }
        uint32_t GetFrameIndex() { return m_frameCounter % GetSwapchainImageCount(); }
        uint32_t GetMaxUniformBufferSize() { return m_maxUniformBufferSize; }
        uint32_t GetMaxStorageBufferSize() { return m_maxStorageBufferSize; }
        uint32_t GetMaxDrawIndirectCount() { return m_maxDrawIndirectCount; }
        size_t Align(size_t size, size_t alignment) { return alignment > 1 ? (size + (alignment - 1)) & ~(alignment - 1) : size; }
        size_t AlignUniform(size_t size) { return Align(size, m_minUniformBufferOffsetAlignment); }
        size_t AlignStorage(size_t size) { return Align(size, m_minStorageBufferOffsetAlignment); }
        size_t AlignStorageAs(size_t size, size_t alignment) { return AlignStorage(Align(size, alignment)); } // Aligned also to min storage alignment
        size_t AlignTextureRowPitch(size_t rowBytes) { return Align(rowBytes, m_textureDataPitchAlignment); }
        uint32_t GetMaxPushConstantsSize() { return m_maxPushConstantsSize; }
        const Caps &GetCaps() const { return m_caps; }
        const GpuLimits &GetGpuLimits() const { return m_gpuLimits; }
        const GpuAdapterInfo &GetGpuAdapterInfo() const { return m_gpuAdapterInfo; }
        const GpuFeatureSupport &GetGpuFeatureSupport() const { return m_gpuFeatureSupport; }
        const std::string &GetGpuName() { return m_gpuName; }
        DescriptorPool *GetDescriptorPool() { return m_descriptorPool; }
        Queue *GetMainQueue() { return m_mainQueue; }
        SDL_Window *GetWindow() { return m_window; }
        Surface *GetSurface() { return m_surface; }
        Swapchain *GetSwapchain() { return m_swapchain; }
        uint32_t GetSwapchainImageCount();
        SystemProcMem GetSystemAndProcessMemory();
        GpuMemorySnapshot GetGpuMemorySnapshot();
        void ChangePresentMode(PePresentMode mode);
        const char *PresentModeToString(PePresentMode presentMode);
        StagingManager *GetStagingManager() { return m_stagingManager; }
        void AddToDeletionQueue(std::function<void()> &&deletor);
        void FlushDeletionQueue(uint32_t frameIndex);

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        float GetWidthf() const;
        float GetHeightf() const;

        ::PeFormat GetSwapchainFormat();
        ::PeFormat GetDepthFormat();

    private:
        RHI() = default;

        std::string m_gpuName;
        DescriptorPool *m_descriptorPool;
        Queue *m_mainQueue;
        SDL_Window *m_window;
        Surface *m_surface;
        Swapchain *m_swapchain;
        uint32_t m_frameCounter;
        StagingManager *m_stagingManager;
        std::vector<DeletionQueue *> m_deletionQueues;

        // Limits
        uint32_t m_maxUniformBufferSize;
        uint32_t m_maxStorageBufferSize;
        uint64_t m_minUniformBufferOffsetAlignment;
        uint64_t m_minStorageBufferOffsetAlignment;
        uint64_t m_textureDataPitchAlignment = 1;
        uint32_t m_maxPushConstantsSize;
        uint32_t m_maxDrawIndirectCount;

        Caps m_caps;
        GpuLimits m_gpuLimits;
        GpuAdapterInfo m_gpuAdapterInfo;
        GpuFeatureSupport m_gpuFeatureSupport;
        PeGraphicsApi m_api = PE_GRAPHICS_API_VULKAN;
        Impl *m_impl = nullptr;
    };

    PE_API RHI &GetRHI();
    extern PE_API RHI &RHII;
} // namespace pe
