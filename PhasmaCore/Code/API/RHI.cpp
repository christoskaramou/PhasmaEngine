#include "API/RHI.h"
#include "API/Command.h"
#include "API/Semaphore.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Swapchain.h"
#include "API/Queue.h"
#include "API/Buffer.h"
#include "API/Surface.h"
#include "API/Downsampler/Downsampler.h"

#if defined(PE_WIN32)
// On Windows, Vulkan commands use the stdcall convention
#define VKAPI_CALL __stdcall
#else
// On other platforms, use the default calling convention
#define VKAPI_CALL
#endif

// System + Process RAM (Windows)
#if defined(PE_WIN32)
#pragma comment(lib, "psapi.lib")
#endif

namespace pe
{
    static inline uint32_t VkVendorID(const vk::PhysicalDevice &gpu)
    {
        return gpu.getProperties().vendorID; // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
    }

    static bool GetVkPciBusId(vk::PhysicalDevice gpu, uint32_t &domain, uint32_t &bus, uint32_t &device, uint32_t &function)
    {
#if defined(VK_EXT_pci_bus_info)
        vk::PhysicalDevicePCIBusInfoPropertiesEXT pci{};
        vk::PhysicalDeviceProperties2 props2{};
        props2.pNext = &pci;
        gpu.getProperties2(&props2);
        if (pci.sType == vk::StructureType::ePhysicalDevicePciBusInfoPropertiesEXT)
        {
            domain = pci.pciDomain;
            bus = pci.pciBus;
            device = pci.pciDevice;
            function = pci.pciFunction;
            return true;
        }
#endif
        domain = bus = device = function = 0;
        return false;
    }

    // =========================================================
    // Global VRAM backends (runtime/optional) with fallbacks
    // =========================================================

    // ---------- NVIDIA: NVML (Win/Linux, runtime load) ----------
    struct NvmlAPI
    {
#if defined(PE_WIN32)
        HMODULE dll = nullptr;
        FARPROC L(const char *n) { return GetProcAddress(dll, n); }
        bool Load()
        {
            if (dll)
                return true;
            dll = LoadLibraryA("nvml.dll");
            return dll != nullptr;
        }
#elif defined(PE_LINUX)
        void *so = nullptr;
        void *L(const char *n) { return so ? dlsym(so, n) : nullptr; }
        bool Load()
        {
            if (so)
                return true;
            so = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
            return so != nullptr;
        }
#else
        bool Load() { return false; }
#endif
        int (*nvmlInit_v2)() = nullptr;
        int (*nvmlShutdown)() = nullptr;
        int (*nvmlDeviceGetHandleByPciBusId_v2)(const char *, void **) = nullptr;
        int (*nvmlDeviceGetMemoryInfo)(void *, void *) = nullptr;

        bool Init()
        {
            if (!Load())
                return false;
            nvmlInit_v2 = (int (*)())L("nvmlInit_v2");
            nvmlShutdown = (int (*)())L("nvmlShutdown");
            nvmlDeviceGetHandleByPciBusId_v2 = (int (*)(const char *, void **))L("nvmlDeviceGetHandleByPciBusId_v2");
            nvmlDeviceGetMemoryInfo = (int (*)(void *, void *))L("nvmlDeviceGetMemoryInfo");
            return nvmlInit_v2 && nvmlShutdown && nvmlDeviceGetHandleByPciBusId_v2 && nvmlDeviceGetMemoryInfo && nvmlInit_v2() == 0;
        }
    } g_nvml;

    static bool NvmlGlobalVramUsedByBDF(uint32_t dom, uint32_t bus, uint32_t dev, uint64_t &used, uint64_t &total)
    {
        used = total = 0;
        if (!g_nvml.Init())
            return false;
        char pciStr[32];
        snprintf(pciStr, sizeof(pciStr), "%04x:%02x:%02x.%u", dom, bus, dev, 0u);
        void *h = nullptr;
        if (g_nvml.nvmlDeviceGetHandleByPciBusId_v2(pciStr, &h) != 0 || !h)
            return false;
        struct
        {
            unsigned long long total, free, used;
        } mem{};
        if (g_nvml.nvmlDeviceGetMemoryInfo(h, &mem) != 0)
            return false;
        total = (uint64_t)mem.total;
        used = (uint64_t)mem.used;
        return true;
    }

// ---------- AMD: Linux sysfs amdgpu ----------
#if defined(PE_LINUX)
    static bool AmdSysfsGlobalVramByBDF(uint32_t dom, uint32_t bus, uint32_t dev, uint64_t &used, uint64_t &total)
    {
        used = total = 0;
        char bdf[32];
        snprintf(bdf, sizeof(bdf), "%04x:%02x:%02x.%u", dom, bus, dev, 0u);
        std::string base = std::string("/sys/bus/pci/devices/") + bdf + "/";
        std::ifstream fu(base + "mem_info_vram_used");
        std::ifstream ft(base + "mem_info_vram_total");
        if (!fu || !ft)
            return false;
        fu >> used;
        ft >> total; // bytes
        return (used > 0 && total > 0);
    }
#endif

    // ---------- Intel: Level Zero Sysman (Win/Linux, runtime) ----------
    struct L0API
    {
#if defined(PE_WIN32)
        HMODULE so = nullptr;
        FARPROC L(const char *n) { return GetProcAddress(so, n); }
        bool Load()
        {
            if (so)
                return true;
            so = LoadLibraryA("ze_loader.dll");
            return so != nullptr;
        }
#else
        void *so = nullptr;
        void *L(const char *n) { return so ? dlsym(so, n) : nullptr; }
        bool Load()
        {
            if (so)
                return true;
            so = dlopen("libze_loader.so.1", RTLD_LAZY);
            return so != nullptr;
        }
#endif
        // core
        int (*zeInit)(uint32_t) = nullptr;
        int (*zeDriverGet)(uint32_t *, void **) = nullptr;
        int (*zeDeviceGet)(void *, uint32_t *, void **) = nullptr;
        int (*zeDeviceGetProperties)(void *, void *) = nullptr;
        // sysman
        int (*zesInit)(uint32_t) = nullptr;
        int (*zesDeviceGet)(void *, void **) = nullptr;
        int (*zesDeviceEnumMemoryModules)(void *, uint32_t *, void **) = nullptr;
        int (*zesMemoryGetState)(void *, void *) = nullptr;

        bool Init()
        {
            if (!Load())
                return false;
            zeInit = (int (*)(uint32_t))L("zeInit");
            zeDriverGet = (int (*)(uint32_t *, void **))L("zeDriverGet");
            zeDeviceGet = (int (*)(void *, uint32_t *, void **))L("zeDeviceGet");
            zeDeviceGetProperties = (int (*)(void *, void *))L("zeDeviceGetProperties");
            zesInit = (int (*)(uint32_t))L("zesInit");
            zesDeviceGet = (int (*)(void *, void **))L("zesDeviceGet");
            zesDeviceEnumMemoryModules = (int (*)(void *, uint32_t *, void **))L("zesDeviceEnumMemoryModules");
            zesMemoryGetState = (int (*)(void *, void *))L("zesMemoryGetState");
            if (!zeInit || !zeDriverGet || !zeDeviceGet || !zeDeviceGetProperties || !zesInit || !zesDeviceGet || !zesDeviceEnumMemoryModules || !zesMemoryGetState)
                return false;
            return zeInit(0) == 0 && zesInit(0) == 0;
        }
    } g_l0;

    static bool L0GlobalVramUsed(uint64_t &used, uint64_t &total)
    {
        used = total = 0;
        if (!g_l0.Init())
            return false;

        uint32_t nDrivers = 0;
        if (g_l0.zeDriverGet(&nDrivers, nullptr) != 0 || nDrivers == 0)
            return false;
        std::vector<void *> drivers(nDrivers);
        g_l0.zeDriverGet(&nDrivers, drivers.data());

        struct ze_device_properties_t
        {
            uint32_t stype;
            const void *pNext;
            uint32_t type;
            uint32_t vendorId;
            uint32_t deviceId;
            char name[256];
            uint32_t flags;
            uint32_t coreClockRate;
            uint32_t reserved[32];
        } props{};
        props.stype = 1;
        props.pNext = nullptr;

        for (auto d : drivers)
        {
            uint32_t nDevices = 0;
            if (g_l0.zeDeviceGet(d, &nDevices, nullptr) != 0 || nDevices == 0)
                continue;
            std::vector<void *> devs(nDevices);
            g_l0.zeDeviceGet(d, &nDevices, devs.data());
            for (auto devH : devs)
            {
                g_l0.zeDeviceGetProperties(devH, &props);
                if (props.vendorId != 0x8086)
                    continue; // Intel only

                void *sysmanDev = nullptr;
                if (g_l0.zesDeviceGet(devH, &sysmanDev) != 0 || !sysmanDev)
                    continue;
                uint32_t nMods = 0;
                if (g_l0.zesDeviceEnumMemoryModules(sysmanDev, &nMods, nullptr) != 0 || nMods == 0)
                    continue;
                std::vector<void *> mods(nMods);
                g_l0.zesDeviceEnumMemoryModules(sysmanDev, &nMods, mods.data());

                struct zes_mem_state_t
                {
                    uint32_t stype;
                    const void *pNext;
                    uint32_t type;
                    uint64_t physicalSize;
                    uint64_t free;
                    uint64_t reserved[8];
                } state{};
                state.stype = 1;
                state.pNext = nullptr;

                uint64_t u = 0, tot = 0;
                for (auto m : mods)
                {
                    if (g_l0.zesMemoryGetState(m, &state) == 0)
                    {
                        tot += state.physicalSize;
                        u += (state.physicalSize - state.free);
                    }
                }
                if (tot > 0)
                {
                    used = u;
                    total = tot;
                    return true;
                }
            }
        }
        return false;
    }

// ---------- AMD Windows: ADLX (optional) ----------
#if defined(PE_WIN32) && defined(PE_USE_ADLX)
#include <ADLXHelper.h>
#include <interfaces/IADLXSystem.h>
#include <interfaces/IADLXPerformanceMonitoring.h>
    static bool AdlxGlobalVramUsedByAdapter(uint64_t &usedOut, uint64_t &totalOut)
    {
        usedOut = totalOut = 0;

        static adlx::ADLXHelper g_adlx;
        if (g_adlx.Initialize() != ADLX_OK)
            return false;

        adlx::IADLXSystem *sys = g_adlx.GetSystemServices();
        if (!sys)
            return false;

        adlx::IADLXPerformanceMonitoringServices *pmon = nullptr;
        if (sys->GetPerformanceMonitoringServices(&pmon) != ADLX_OK || !pmon)
            return false;

        adlx::IADLXGPUs *gpus = nullptr;
        if (sys->GetGPUs(&gpus) != ADLX_OK || !gpus)
        {
            pmon->Release();
            return false;
        }

        adlx_uint count = 0;
        gpus->Size(&count);
        if (count == 0)
        {
            gpus->Release();
            pmon->Release();
            return false;
        }

        // TODO: match your Vulkan GPU via PCI if multi-GPU. For now, first GPU.
        adlx::IADLXGPU *gpu = nullptr;
        gpus->At(0, &gpu);
        if (!gpu)
        {
            gpus->Release();
            pmon->Release();
            return false;
        }

        adlx::IADLXPerformanceMetrics *metrics = nullptr;
        if (pmon->GetCurrentPerformanceMetrics(gpu, &metrics) != ADLX_OK || !metrics)
        {
            gpu->Release();
            gpus->Release();
            pmon->Release();
            return false;
        }

        adlx_int vramUsedMB = 0, vramTotalMB = 0;
        metrics->VRAMUsage(&vramUsedMB);
        metrics->VRAMTotal(&vramTotalMB);

        usedOut = (uint64_t)vramUsedMB * 1024ull * 1024ull;
        totalOut = (uint64_t)vramTotalMB * 1024ull * 1024ull;

        metrics->Release();
        gpu->Release();
        gpus->Release();
        pmon->Release();
        return usedOut > 0 && totalOut > 0;
    }
#else
    static bool AdlxGlobalVramUsedByAdapter(uint64_t &usedOut, uint64_t &totalOut)
    {
        usedOut = totalOut = 0;
        return false;
    }
#endif

    void RHI::Init(SDL_Window *window)
    {
        m_window = window;
        m_frameCounter = 0;

        Debug::InitCaptureApi();

        CreateInstance(window);
        CreateSurface();
        FindGpu();
        GetSurfaceProperties();
        CreateDevice();
        CreateAllocator();
        CreateSwapchain(m_surface);
        CreateDescriptorPool(150); // General purpose descriptor pool
        InitDownSampler();
    }

    void RHI::Destroy()
    {
        WaitDeviceIdle();

        Queue::Destroy(m_mainQueue);
        PeHandleBase::DestroyAllIHandles();
        vmaDestroyAllocator(m_allocator);
        if (m_device)
            m_device.destroy();
        Debug::DestroyDebugMessenger();
        Debug::DestroyCaptureApi();
        if (m_instance)
            m_instance.destroy();
    }

    void RHI::CreateInstance(SDL_Window *window)
    {
        std::vector<const char *> instanceExtensions{};
        std::vector<const char *> instanceLayers{};

        // === Extentions ==============================
        unsigned extCount;
        PE_ERROR_IF(!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr), SDL_GetError());
        instanceExtensions.resize(extCount);
        PE_ERROR_IF(!SDL_Vulkan_GetInstanceExtensions(window, &extCount, instanceExtensions.data()), SDL_GetError());
        // =============================================

#if !defined(PE_RELEASE) || !defined(PE_MINSIZEREL)
        // === Debugging ===============================
        if (RHII.IsInstanceExtensionValid("VK_EXT_debug_utils"))
            instanceExtensions.push_back("VK_EXT_debug_utils");
        // =============================================
#endif

        // uint32_t apiVersion;
        // vkEnumerateInstanceVersion(&apiVersion);

        vk::ApplicationInfo appInfo{};
        appInfo.sType = vk::StructureType::eApplicationInfo;
        appInfo.pApplicationName = "PhasmaEngine";
        appInfo.pEngineName = "PhasmaEngine";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        vk::InstanceCreateInfo instInfo{};
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
        instInfo.ppEnabledLayerNames = instanceLayers.data();
        instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        instInfo.ppEnabledExtensionNames = instanceExtensions.data();

        m_instance = vk::createInstance(instInfo);

        Debug::Init(m_instance);
        Debug::CreateDebugMessenger();
    }

    void RHI::CreateSurface()
    {
        m_surface = Surface::Create(m_window);
    }

    void RHI::GetSurfaceProperties()
    {
        m_surface->FindProperties();
    }

    struct GPUScore
    {
        vk::PhysicalDevice gpu;
        uint32_t score;
    };

    void RHI::FindGpu()
    {
        auto gpuList = m_instance.enumeratePhysicalDevices();
        std::vector<GPUScore> gpuScores{};

        for (auto &gpu : gpuList)
        {
            auto queueFamilyProperties = gpu.getQueueFamilyProperties2();

            for (auto &qfp : queueFamilyProperties)
            {
                if (qfp.queueFamilyProperties.queueCount > 0 &&
                    qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics &&
                    qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eCompute &&
                    qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eTransfer)
                {
                    auto properties2 = gpu.getProperties2();

                    GPUScore gpuScore{};
                    gpuScore.gpu = gpu;
                    switch (properties2.properties.deviceType)
                    {
                    case vk::PhysicalDeviceType::eDiscreteGpu:
                        gpuScore.score = 4;
                        break;
                    case vk::PhysicalDeviceType::eIntegratedGpu:
                        gpuScore.score = 3;
                        break;
                    case vk::PhysicalDeviceType::eVirtualGpu:
                        gpuScore.score = 2;
                        break;
                    case vk::PhysicalDeviceType::eCpu:
                        gpuScore.score = 1;
                        break;
                    default:
                        continue;
                    }

                    gpuScores.push_back(gpuScore);
                    break;
                }
            }
        }

        PE_ERROR_IF(gpuScores.empty(), "No suitable GPU found!");
        std::sort(gpuScores.begin(), gpuScores.end(), [](const GPUScore &a, const GPUScore &b)
                  { return a.score > b.score; });
        m_gpu = gpuScores.front().gpu;

        vk::PhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProperties{};

        vk::PhysicalDeviceProperties2 gpuPropertiesVK{};
        gpuPropertiesVK.pNext = &pushDescriptorProperties;
        m_gpu.getProperties2(&gpuPropertiesVK);

        m_gpuName = gpuPropertiesVK.properties.deviceName.data();
        m_maxUniformBufferSize = gpuPropertiesVK.properties.limits.maxUniformBufferRange;
        m_maxStorageBufferSize = gpuPropertiesVK.properties.limits.maxStorageBufferRange;
        m_minUniformBufferOffsetAlignment = gpuPropertiesVK.properties.limits.minUniformBufferOffsetAlignment;
        m_minStorageBufferOffsetAlignment = gpuPropertiesVK.properties.limits.minStorageBufferOffsetAlignment;
        m_maxPushDescriptorsPerSet = pushDescriptorProperties.maxPushDescriptors;
        m_maxPushConstantsSize = gpuPropertiesVK.properties.limits.maxPushConstantsSize;
        m_maxDrawIndirectCount = gpuPropertiesVK.properties.limits.maxDrawIndirectCount;
    }

    bool RHI::IsInstanceExtensionValid(const char *name)
    {
        auto extensions = vk::enumerateInstanceExtensionProperties();
        for (auto &extension : extensions)
        {
            if (std::string(extension.extensionName.data()) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsInstanceLayerValid(const char *name)
    {
        auto layers = vk::enumerateInstanceLayerProperties();
        for (auto &layer : layers)
        {
            if (std::string(layer.layerName.data()) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsDeviceExtensionValid(const char *name)
    {
        PE_ERROR_IF(!m_gpu, "Must find gpu before checking device extensions!");

        auto extensions = m_gpu.enumerateDeviceExtensionProperties();
        for (auto &extension : extensions)
            if (std::string(extension.extensionName.data()) == name)
                return true;

        return false;
    }

    void RHI::CreateDevice()
    {
        PE_ERROR_IF(!IsDeviceExtensionValid(VK_KHR_SWAPCHAIN_EXTENSION_NAME), "Swapchain extension not supported!");
        PE_ERROR_IF(!IsDeviceExtensionValid(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME), "Synchronization2 extension not supported!");
        PE_ERROR_IF(!IsDeviceExtensionValid(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME), "Push descriptor extension not supported!");
        PE_ERROR_IF(!IsDeviceExtensionValid(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME), "Separate depth stencil layouts extension not supported!");

        std::vector<const char *> deviceExtensions{};
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        if (IsDeviceExtensionValid(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
            deviceExtensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
        }

        auto queueFamilyProperties = m_gpu.getQueueFamilyProperties();
        float priority = 1.f;
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
        {
            if (queueFamilyProperties[i].queueCount > 0 &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eTransfer)
            {
                queueCreateInfo.queueFamilyIndex = i;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &priority;
                break;
            }
        }

        // Vulkan 1.1 features
        vk::PhysicalDeviceVulkan11Features deviceFeatures11{};
        // Vulkan 1.2 features
        vk::PhysicalDeviceVulkan12Features deviceFeatures12{};
        deviceFeatures12.pNext = &deviceFeatures11;
        // Vulkan 1.3 features
        vk::PhysicalDeviceVulkan13Features deviceFeatures13{};
        deviceFeatures13.pNext = &deviceFeatures12;
        // Vulkan 1.4 features
        vk::PhysicalDeviceVulkan14Features deviceFeatures14{};
        deviceFeatures14.pNext = &deviceFeatures13;

        vk::PhysicalDeviceFeatures2 deviceFeatures2{};
        deviceFeatures2.pNext = &deviceFeatures14;
        // Supported features
        m_gpu.getFeatures2(&deviceFeatures2);

        Settings::Get<GlobalSettings>().dynamic_rendering &= static_cast<bool>(deviceFeatures13.dynamicRendering);

        // Check needed features
        PE_ERROR_IF(!deviceFeatures12.descriptorBindingPartiallyBound, "Partially bound descriptors are not supported on this device!");
        PE_ERROR_IF(!deviceFeatures12.runtimeDescriptorArray, "Runtime descriptor array is not supported on this device!");
        PE_ERROR_IF(!deviceFeatures12.shaderSampledImageArrayNonUniformIndexing, "Sampled image array non uniform indexing is not supported on this device!");
        PE_ERROR_IF(!deviceFeatures12.shaderStorageBufferArrayNonUniformIndexing, "Storage buffer array non uniform indexing is not supported on this device!");
        PE_ERROR_IF(!deviceFeatures12.descriptorBindingVariableDescriptorCount, "Variable descriptor count is not supported on this device!");
        PE_ERROR_IF(!deviceFeatures2.features.shaderInt64, "Int64 is not supported on this device!");
        PE_ERROR_IF(!deviceFeatures2.features.shaderInt16, "Int16 is not supported on this device!");
        PE_ERROR_IF(!deviceFeatures12.separateDepthStencilLayouts, "Separate depth stencil layouts are not supported!");
        PE_ERROR_IF(!deviceFeatures2.features.multiDrawIndirect, "Multi draw indirect is not supported!");
        PE_ERROR_IF(!deviceFeatures2.features.drawIndirectFirstInstance, "Draw indirect first instance is not supported!");

        vk::DeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.pNext = &deviceFeatures2;

        m_device = m_gpu.createDevice(deviceCreateInfo);

        Debug::SetObjectName(m_surface->ApiHandle(), "RHI_surface");
        Debug::SetObjectName(m_gpu, "RHI_gpu");
        Debug::SetObjectName(m_device, "RHI_device");

        m_mainQueue = Queue::Create(m_device, queueCreateInfo.queueFamilyIndex, "Main_queue");
    }

    void RHI::CreateAllocator()
    {
        uint32_t apiVersion = vk::enumerateInstanceVersion();

        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.flags = VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
        allocator_info.physicalDevice = m_gpu;
        allocator_info.device = m_device;
        allocator_info.instance = m_instance;
        allocator_info.vulkanApiVersion = apiVersion;

        PE_CHECK(vmaCreateAllocator(&allocator_info, &m_allocator));
    }

    void RHI::CreateSwapchain(Surface *surface)
    {
        m_swapchain = Swapchain::Create(surface, "RHI_swapchain");

        m_binarySemaphores.resize(GetSwapchainImageCount(), std::stack<Semaphore *>());
        m_usedBinarySemaphores.resize(GetSwapchainImageCount(), std::stack<Semaphore *>());
    }

    void RHI::CreateDescriptorPool(uint32_t maxDescriptorSets)
    {
        std::vector<vk::DescriptorPoolSize> descPoolsizes(5);
        descPoolsizes[0].type = vk::DescriptorType::eUniformBuffer;
        descPoolsizes[0].descriptorCount = maxDescriptorSets;
        descPoolsizes[1].type = vk::DescriptorType::eStorageBuffer;
        descPoolsizes[1].descriptorCount = maxDescriptorSets;
        descPoolsizes[2].type = vk::DescriptorType::eStorageTexelBuffer;
        descPoolsizes[2].descriptorCount = maxDescriptorSets;
        descPoolsizes[3].type = vk::DescriptorType::eCombinedImageSampler;
        descPoolsizes[3].descriptorCount = maxDescriptorSets;
        descPoolsizes[4].type = vk::DescriptorType::eUniformBufferDynamic;
        descPoolsizes[4].descriptorCount = maxDescriptorSets;
        m_descriptorPool = DescriptorPool::Create(descPoolsizes, "RHI_descriptor_pool");
    }

    void RHI::InitDownSampler()
    {
        Downsampler::Init();
    }

    vk::Format RHI::GetDepthFormat()
    {
        static vk::Format depthFormat = vk::Format::eUndefined;

        if (depthFormat == vk::Format::eUndefined)
        {
            std::vector<vk::Format> candidates = {
                vk::Format::eD32Sfloat,
                vk::Format::eD32SfloatS8Uint,
                vk::Format::eD24UnormS8Uint,
            };

            for (auto &df : candidates)
            {
                auto props = m_gpu.getFormatProperties(df);
                if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) ==
                    vk::FormatFeatureFlagBits::eDepthStencilAttachment)
                {
                    depthFormat = df;
                    break;
                }
            }

            PE_ERROR_IF(depthFormat == vk::Format::eUndefined, "Depth format is undefined");
        }

        return depthFormat;
    }

    void RHI::WaitDeviceIdle()
    {
        m_device.waitIdle();
    }

    Semaphore *RHI::AcquireBinarySemaphore(uint32_t frame)
    {
        std::lock_guard<std::mutex> lock(m_binarySemaphoresMutex);

        auto &frameBinarySemaphores = m_binarySemaphores[frame];
        auto &usedFrameBinarySemaphores = m_usedBinarySemaphores[frame];

        if (frameBinarySemaphores.empty())
            frameBinarySemaphores.push(Semaphore::Create(false, "RHI_binary_semaphore"));

        usedFrameBinarySemaphores.push(frameBinarySemaphores.top());
        frameBinarySemaphores.pop();

        return usedFrameBinarySemaphores.top();
    }

    void RHI::ReturnBinarySemaphores(uint32_t frame)
    {
        std::lock_guard<std::mutex> lock(m_binarySemaphoresMutex);

        auto &frameBinarySemaphores = m_binarySemaphores[frame];
        auto &usedFrameBinarySemaphores = m_usedBinarySemaphores[frame];

        while (!usedFrameBinarySemaphores.empty())
        {
            frameBinarySemaphores.push(usedFrameBinarySemaphores.top());
            usedFrameBinarySemaphores.pop();
        }
    }

    uint32_t RHI::GetSwapchainImageCount() { return m_swapchain->GetImageCount(); }

    SystemProcMem RHI::GetSystemAndProcessMemory()
    {
        SystemProcMem m{};

#if defined(PE_WIN32)
        // ---- System ----
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms))
        {
            m.sysTotal = (uint64_t)ms.ullTotalPhys;
            m.sysUsed = m.sysTotal - (uint64_t)ms.ullAvailPhys;
        }

        // ---- Process ----
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 (PROCESS_MEMORY_COUNTERS *)&pmc,
                                 sizeof(pmc)))
        {
            m.procWorkingSet = (uint64_t)pmc.WorkingSetSize;
            m.procPrivateBytes = (uint64_t)pmc.PrivateUsage; // “our” committed private memory
            m.procCommit = (uint64_t)pmc.PrivateUsage;
            m.procPeakWS = (uint64_t)pmc.PeakWorkingSetSize;
        }

#elif defined(PE_LINUX)
        // ---- System: MemTotal / MemAvailable from /proc/meminfo ----
        {
            std::ifstream mi("/proc/meminfo");
            uint64_t totalKB = 0, availKB = 0, val = 0;
            std::string key, unit;
            if (mi)
            {
                while (mi >> key >> val >> unit)
                {
                    if (key == "MemTotal:")
                        totalKB = val;
                    else if (key == "MemAvailable:")
                        availKB = val;
                    mi.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
            }
            if (totalKB && availKB)
            {
                m.sysTotal = totalKB * 1024ull;
                m.sysUsed = (totalKB - availKB) * 1024ull;
            }
            else
            {
                // Fallback: sysinfo (less accurate for caches/buffers)
                struct sysinfo si{};
                if (sysinfo(&si) == 0)
                {
                    m.sysTotal = (uint64_t)si.totalram * si.mem_unit;
                    uint64_t freeB = (uint64_t)si.freeram * si.mem_unit;
                    m.sysUsed = (m.sysTotal > freeB) ? (m.sysTotal - freeB) : 0;
                }
            }
        }

        // ---- Process: VmRSS + VmHWM from /proc/self/status ----
        uint64_t vmRSSB = 0, vmHWMB = 0;
        {
            std::ifstream st("/proc/self/status");
            if (st)
            {
                std::string k, unit;
                uint64_t vKB = 0;
                while (st >> k)
                {
                    if (k == "VmRSS:")
                    {
                        st >> vKB >> unit;
                        vmRSSB = vKB * 1024ull;
                    }
                    else if (k == "VmHWM:")
                    {
                        st >> vKB >> unit;
                        vmHWMB = vKB * 1024ull;
                    }
                    st.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
            }
        }

        // ---- Process: Private bytes from /proc/self/smaps_rollup (fallback: RSS) ----
        uint64_t privCleanB = 0, privDirtyB = 0;
        {
            std::ifstream sm("/proc/self/smaps_rollup");
            if (sm)
            {
                std::string line;
                while (std::getline(sm, line))
                {
                    if (line.rfind("Private_Clean:", 0) == 0)
                    {
                        uint64_t kb = 0;
                        std::istringstream(line.substr(14)) >> kb;
                        privCleanB = kb * 1024ull;
                    }
                    else if (line.rfind("Private_Dirty:", 0) == 0)
                    {
                        uint64_t kb = 0;
                        std::istringstream(line.substr(14)) >> kb;
                        privDirtyB = kb * 1024ull;
                    }
                }
            }
        }
        const uint64_t privTotalB = (privCleanB + privDirtyB) ? (privCleanB + privDirtyB) : vmRSSB;

        m.procWorkingSet = vmRSSB;       // Resident
        m.procPeakWS = vmHWMB;           // Peak resident
        m.procPrivateBytes = privTotalB; // “we use”
        m.procCommit = privTotalB;       // alias
#endif

        return m;
    }

    GpuMemorySnapshot RHI::GetGpuMemorySnapshot()
    {
        GpuMemorySnapshot snap{};

        if (!IsDeviceExtensionValid(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
            return snap;

        // --- Vulkan heaps/budgets baseline ---
        vk::PhysicalDeviceMemoryBudgetPropertiesEXT memBudget{};
        vk::PhysicalDeviceMemoryProperties2 props{};
        props.pNext = &memBudget;
        m_gpu.getMemoryProperties2(&props);

        const auto &heaps = props.memoryProperties.memoryHeaps;
        const uint32_t heapCount = props.memoryProperties.memoryHeapCount;

        // Our VMA per-heap committed memory
        std::vector<VmaBudget> vmaBudgets(heapCount);
        if (m_allocator)
            vmaGetHeapBudgets(m_allocator, vmaBudgets.data());

        for (uint32_t i = 0; i < heapCount; ++i)
        {
            const bool deviceLocal =
                (heaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) == vk::MemoryHeapFlagBits::eDeviceLocal;

            MemoryInfo &sec = deviceLocal ? snap.vram : snap.host;

            const uint64_t heapSize = heaps[i].size;
            const uint64_t heapBudget = std::min<uint64_t>(memBudget.heapBudget[i], heapSize);
            const uint64_t heapUsed = memBudget.heapUsage[i]; // ALL Vulkan apps
            const uint64_t ourCommit = m_allocator ? vmaBudgets[i].statistics.blockBytes : 0;

            sec.size += heapSize;
            sec.budget += heapBudget;
            sec.used += heapUsed; // baseline used
            sec.app += ourCommit; // our VMA-committed
            sec.heaps += 1;
        }

        // derive baseline "other" (still Vulkan-only at this point)
        auto finalize = [](MemoryInfo &mi)
        {
            mi.other = (mi.used > mi.app) ? (mi.used - mi.app) : 0;
        };
        finalize(snap.vram);
        finalize(snap.host);

        // ---- Try to override VRAM.used with cross-API global used ----
        uint32_t dom = 0, bus = 0, dev = 0, fn = 0;
        if (GetVkPciBusId(m_gpu, dom, bus, dev, fn))
        {
            const uint32_t vendor = VkVendorID(m_gpu);
            uint64_t gUsed = 0, gTotal = 0;
            bool ok = false;

            if (vendor == 0x10DE)
            { // NVIDIA
                ok = NvmlGlobalVramUsedByBDF(dom, bus, dev, gUsed, gTotal);
            }
#if defined(PE_LINUX)
            else if (vendor == 0x1002 || vendor == 0x1022)
            { // AMD Linux
                ok = AmdSysfsGlobalVramByBDF(dom, bus, dev, gUsed, gTotal);
            }
#endif
            else if (vendor == 0x8086)
            { // Intel Win/Linux
                ok = L0GlobalVramUsed(gUsed, gTotal);
            }
#if defined(PE_WIN32)
            else if (vendor == 0x1002 || vendor == 0x1022)
            { // AMD Windows
                ok = AdlxGlobalVramUsedByAdapter(gUsed, gTotal);
            }
#endif

            if (ok && gUsed > 0)
            {
                // Override VRAM.used globally; keep budget/size from Vulkan
                snap.vram.used = gUsed;
                snap.vram.other = (snap.vram.used > snap.vram.app) ? (snap.vram.used - snap.vram.app) : 0;
            }
        }

        return snap;
    }

    void RHI::ChangePresentMode(vk::PresentModeKHR mode)
    {
        WaitDeviceIdle();

        m_surface->SetPresentMode(mode);
        Swapchain::Destroy(m_swapchain);
        CreateSwapchain(m_surface);

        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + GetGpuName();
        title += " - API: Vulkan";
        title += " - Present Mode: " + std::string(PresentModeToString(m_surface->GetPresentMode()));
#if PE_DEBUG
        title += " - Debug";
#elif PE_RELEASE
        title += " - Release";
#elif PE_MINSIZEREL
        title += " - MinSizeRel";
#elif PE_RELWITHDEBINFO
        title += " - RelWithDebInfo";
#endif

        EventSystem::DispatchEvent(EventType::SetWindowTitle, title);
    }

    const char *RHI::PresentModeToString(vk::PresentModeKHR presentMode)
    {
        const char *modesNames[] = {"Immediate", "Mailbox", "Fifo", "FifoRelaxed", ""};
        switch (presentMode)
        {
        case vk::PresentModeKHR::eImmediate:
            return modesNames[0];
        case vk::PresentModeKHR::eMailbox:
            return modesNames[1];
        case vk::PresentModeKHR::eFifo:
            return modesNames[2];
        case vk::PresentModeKHR::eFifoRelaxed:
            return modesNames[3];
        default:
            break;
        }

        PE_ERROR("Unknown PresentMode");
        return modesNames[4];
    }

    uint32_t RHI::GetWidth() const { return m_surface->GetActualExtent().width; }
    uint32_t RHI::GetHeight() const { return m_surface->GetActualExtent().height; }
    float RHI::GetWidthf() const { return static_cast<float>(GetWidth()); }
    float RHI::GetHeightf() const { return static_cast<float>(GetHeight()); }
}
