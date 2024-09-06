#if PE_VULKAN
#include "Renderer/RHI.h"
#include "Systems/RendererSystem.h"
#include "Renderer/Command.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Queue.h"
#include "Renderer/Buffer.h"
#include "Utilities/Downsampler.h"

#if defined(_WIN32)
// On Windows, Vulkan commands use the stdcall convention
#define VKAPI_CALL __stdcall
#else
// On other platforms, use the default calling convention
#define VKAPI_CALL
#endif

namespace pe
{
    RHI::RHI()
    {
        m_device = {};
        m_window = nullptr;
        m_uniformBuffers = {};
        m_uniformImages = {};
        m_maxUniformBufferSize = 0;
        m_maxStorageBufferSize = 0;
        m_minUniformBufferOffsetAlignment = 0;
        m_minStorageBufferOffsetAlignment = 0;
    }

    RHI::~RHI()
    {
    }

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
        InitQueues();
        InitCommandPools();
        InitCmdBuffers(SWAPCHAIN_IMAGES);
        CreateSwapchain(m_surface);
        CreateDescriptorPool(150); // General purpose descriptor pool
        CreateSemaphores(50);
        InitDownSampler();
    }

    void RHI::Destroy()
    {
        WaitDeviceIdle();

        Queue::Clear();
        CommandBuffer::Clear();
        CommandPool::Clear();
        IHandleBase::DestroyAllIHandles();
        vmaDestroyAllocator(m_allocator);
        if (m_device)
            vkDestroyDevice(m_device, nullptr);
        Debug::DestroyDebugMessenger();
        Debug::DestroyCaptureApi();
        if (m_instance)
            vkDestroyInstance(m_instance, nullptr);
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

        if (IsInstanceExtensionValid(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        // =============================================

        // === Debugging ===============================
        Debug::GetInstanceUtils(instanceExtensions, instanceLayers);
        std::vector<VkValidationFeatureEnableEXT> enabledFeatures{};
        VkValidationFeaturesEXT validationFeatures{};
        if (IsInstanceLayerValid("VK_LAYER_KHRONOS_validation"))
        {
            enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
            enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
            // enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);
            enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);

            validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledFeatures.size());
            validationFeatures.pEnabledValidationFeatures = enabledFeatures.data();
        }
        // =============================================

        uint32_t apiVersion;
        vkEnumerateInstanceVersion(&apiVersion);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "PhasmaEngine";
        appInfo.pEngineName = "PhasmaEngine";
        appInfo.apiVersion = apiVersion;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pNext = &validationFeatures;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
        instInfo.ppEnabledLayerNames = instanceLayers.data();
        instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        instInfo.ppEnabledExtensionNames = instanceExtensions.data();

        VkInstance instanceVK;
        PE_CHECK(vkCreateInstance(&instInfo, nullptr, &instanceVK));
        m_instance = instanceVK;

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

    void RHI::FindGpu()
    {
        uint32_t gpuCount;
        vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);
        vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);

        std::vector<VkPhysicalDevice> gpuList(gpuCount);
        vkEnumeratePhysicalDevices(m_instance, &gpuCount, gpuList.data());

        std::vector<VkPhysicalDevice> validGpuList{};
        VkPhysicalDevice preferredGpu;
        VkPhysicalDeviceType preferredGpuType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        // VkPhysicalDeviceType preferredGpuType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

        for (auto &GPU : gpuList)
        {
            uint32_t queueFamPropCount;
            vkGetPhysicalDeviceQueueFamilyProperties2(GPU, &queueFamPropCount, nullptr);

            std::vector<VkQueueFamilyProperties2> queueFamilyProperties(queueFamPropCount);
            for (auto &qfp : queueFamilyProperties)
                qfp.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            vkGetPhysicalDeviceQueueFamilyProperties2(GPU, &queueFamPropCount, queueFamilyProperties.data());

            VkQueueFlags flags{};

            for (auto &qfp : queueFamilyProperties)
            {
                if (qfp.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    flags |= VK_QUEUE_GRAPHICS_BIT;
                if (qfp.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT)
                    flags |= VK_QUEUE_COMPUTE_BIT;
                if (qfp.queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT)
                    flags |= VK_QUEUE_TRANSFER_BIT;
            }

            if (flags & VK_QUEUE_GRAPHICS_BIT &&
                flags & VK_QUEUE_COMPUTE_BIT &&
                flags & VK_QUEUE_TRANSFER_BIT)
            {
                VkPhysicalDeviceProperties2 properties{};
                properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

                vkGetPhysicalDeviceProperties2(GPU, &properties);
                if (properties.properties.deviceType == preferredGpuType)
                {
                    preferredGpu = GPU;
                    break;
                }

                validGpuList.push_back(GPU);
            }
        }

        m_gpu = preferredGpu ? preferredGpu : validGpuList[0];

        VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProperties{};
        pushDescriptorProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;

        VkPhysicalDeviceProperties2 gpuPropertiesVK;
        gpuPropertiesVK.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        gpuPropertiesVK.pNext = &pushDescriptorProperties;

        vkGetPhysicalDeviceProperties2(m_gpu, &gpuPropertiesVK);

        m_gpuName = gpuPropertiesVK.properties.deviceName;
        m_maxUniformBufferSize = gpuPropertiesVK.properties.limits.maxUniformBufferRange;
        m_maxStorageBufferSize = gpuPropertiesVK.properties.limits.maxStorageBufferRange;
        m_minUniformBufferOffsetAlignment = gpuPropertiesVK.properties.limits.minUniformBufferOffsetAlignment;
        m_minStorageBufferOffsetAlignment = gpuPropertiesVK.properties.limits.minStorageBufferOffsetAlignment;
        m_maxPushDescriptorsPerSet = pushDescriptorProperties.maxPushDescriptors;
    }

    bool RHI::IsInstanceExtensionValid(const char *name)
    {
        static std::vector<VkExtensionProperties> extensions;
        if (extensions.empty())
        {
            uint32_t count;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            extensions.resize(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
        }

        for (auto &extension : extensions)
        {
            if (std::string(extension.extensionName) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsInstanceLayerValid(const char *name)
    {
        static std::vector<VkLayerProperties> layers;
        if (layers.empty())
        {
            uint32_t count;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            layers.resize(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
        }

        for (auto &layer : layers)
        {
            if (std::string(layer.layerName) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsDeviceExtensionValid(const char *name)
    {
        PE_ERROR_IF(!m_gpu, "Must find GPU before checking device extensions!");

        static std::vector<VkExtensionProperties> extensions;
        if (extensions.empty())
        {
            uint32_t count;
            vkEnumerateDeviceExtensionProperties(m_gpu, nullptr, &count, nullptr);
            extensions.resize(count);
            vkEnumerateDeviceExtensionProperties(m_gpu, nullptr, &count, extensions.data());
        }

        for (auto &extension : extensions)
            if (std::string(extension.extensionName) == name)
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
        deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &queueFamPropCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &queueFamPropCount, queueFamilyProperties.data());

        std::vector<std::vector<float>> priorities(queueFamPropCount);
        for (uint32_t i = 0; i < queueFamPropCount; i++)
        {
            priorities[i] = std::vector<float>(queueFamilyProperties[i].queueCount, 1.f);

            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = i;
            queueCreateInfo.queueCount = queueFamilyProperties[i].queueCount;
            queueCreateInfo.pQueuePriorities = priorities[i].data();
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // Vulkan 1.2 features
        VkPhysicalDeviceVulkan12Features deviceFeatures12{};
        deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        // Vulkan 1.3 features
        VkPhysicalDeviceVulkan13Features deviceFeatures13{};
        deviceFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        deviceFeatures13.pNext = &deviceFeatures12;

        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.pNext = &deviceFeatures13;

        // Supported features
        vkGetPhysicalDeviceFeatures2(m_gpu, &deviceFeatures2);
        Settings::Get<GlobalSettings>().dynamic_rendering = deviceFeatures13.dynamicRendering;

        // Check needed features
        PE_ERROR_IF(!deviceFeatures12.descriptorBindingPartiallyBound &&
                        !deviceFeatures12.runtimeDescriptorArray &&
                        !deviceFeatures12.shaderSampledImageArrayNonUniformIndexing &&
                        !deviceFeatures12.descriptorBindingVariableDescriptorCount,
                    "Bindless descriptors are not supported on this device!");
        PE_ERROR_IF(!deviceFeatures2.features.shaderInt64 && !deviceFeatures2.features.shaderInt16, "shaderInt64 and shaderInt16 are not supported!");
        PE_ERROR_IF(!deviceFeatures12.separateDepthStencilLayouts, "Separate depth stencil layouts are not supported!");

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.pNext = &deviceFeatures2;

        VkDevice deviceVK;
        PE_CHECK(vkCreateDevice(m_gpu, &deviceCreateInfo, nullptr, &deviceVK));
        m_device = deviceVK;

        Debug::SetObjectName(m_surface->ApiHandle(), "RHI_surface");
        Debug::SetObjectName(m_gpu, "RHI_gpu");
        Debug::SetObjectName(m_device, "RHI_device");
    }

    void RHI::CreateAllocator()
    {
        uint32_t apiVersion;
        vkEnumerateInstanceVersion(&apiVersion);

        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.physicalDevice = m_gpu;
        allocator_info.device = m_device;
        allocator_info.instance = m_instance;
        allocator_info.vulkanApiVersion = apiVersion;

        VmaAllocator allocatorVK;
        PE_CHECK(vmaCreateAllocator(&allocator_info, &allocatorVK));
        m_allocator = allocatorVK;
    }

    void RHI::InitQueues()
    {
        Queue::Init(m_gpu, m_device, m_surface->ApiHandle());

        m_renderQueue = Queue::Get(QueueType::GraphicsBit | QueueType::PresentBit, 1);
        m_computeQueue = Queue::Get(QueueType::ComputeBit, 1);
        m_transferQueue = Queue::Get(QueueType::TransferBit, 1);
        m_presentQueue = Queue::Get(QueueType::PresentBit, 1); // needs VK_SHARING_MODE_CONCURRENT for different queue families
    }

    void RHI::CreateSwapchain(Surface *surface)
    {
        m_swapchain = Swapchain::Create(surface, "RHI_swapchain");
    }

    void RHI::InitCommandPools()
    {
        CommandPool::Init(m_gpu);
    }

    void RHI::CreateDescriptorPool(uint32_t maxDescriptorSets)
    {
        DescriptorPoolSize descPoolsizes[5];
        descPoolsizes[0].type = DescriptorType::UniformBuffer;
        descPoolsizes[0].descriptorCount = maxDescriptorSets;
        descPoolsizes[1].type = DescriptorType::StorageBuffer;
        descPoolsizes[1].descriptorCount = maxDescriptorSets;
        descPoolsizes[2].type = DescriptorType::StorageTexelBuffer;
        descPoolsizes[2].descriptorCount = maxDescriptorSets;
        descPoolsizes[3].type = DescriptorType::CombinedImageSampler;
        descPoolsizes[3].descriptorCount = maxDescriptorSets;
        descPoolsizes[4].type = DescriptorType::UniformBufferDynamic;
        descPoolsizes[4].descriptorCount = maxDescriptorSets;
        m_descriptorPool = DescriptorPool::Create(5, descPoolsizes, "RHI_descriptor_pool");
    }

    void RHI::InitCmdBuffers(uint32_t bufferCount)
    {
        CommandBuffer::Init(m_gpu, bufferCount);
    }

    void RHI::CreateSemaphores(uint32_t semaphoresCount)
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_semaphores[i].resize(semaphoresCount);
            for (uint32_t j = 0; j < semaphoresCount; j++)
                m_semaphores[i][j] = Semaphore::Create(false, "RHI_semaphore_" + std::to_string(i) + "_" + std::to_string(j));
        }
    }

    void RHI::InitDownSampler()
    {
        Downsampler::Init();
    }

    Format RHI::GetDepthFormat()
    {
        static Format depthFormat = Format::Undefined;

        if (depthFormat == Format::Undefined)
        {
            std::vector<VkFormat> candidates = {
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D24_UNORM_S8_UINT,
            };

            for (auto &df : candidates)
            {
                VkFormatProperties props;
                vkGetPhysicalDeviceFormatProperties(m_gpu, df, &props);
                if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                {
                    depthFormat = Translate<Format>(df);
                    break;
                }
            }

            PE_ERROR_IF(depthFormat == Format::Undefined, "Depth format is undefined");
        }

        return depthFormat;
    }

    void RHI::WaitDeviceIdle()
    {
        PE_CHECK(vkDeviceWaitIdle(m_device));
    }

    size_t RHI::CreateUniformBufferInfo()
    {
        size_t key = ID::NextID();
        m_uniformBuffers[key] = UniformBufferInfo{};
        return key;
    }

    void RHI::RemoveUniformBufferInfo(size_t key)
    {
        if (m_uniformBuffers.find(key) != m_uniformBuffers.end())
            m_uniformBuffers.erase(key);
    }

    size_t RHI::CreateUniformImageInfo()
    {
        size_t key = ID::NextID();
        m_uniformImages[key] = UniformImageInfo{};
        return key;
    }

    void RHI::RemoveUniformImageInfo(size_t key)
    {
        if (m_uniformImages.find(key) != m_uniformImages.end())
            m_uniformImages.erase(key);
    }

    uint64_t RHI::GetMemoryUsageSnapshot()
    {
        static bool hasMemoryBudgetExt = IsDeviceExtensionValid(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

        if (hasMemoryBudgetExt)
            return 0;

        VkPhysicalDeviceMemoryBudgetPropertiesEXT memoryBudgetProperties{};
        memoryBudgetProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

        VkPhysicalDeviceMemoryProperties2 memoryProperties{};
        memoryProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memoryProperties.pNext = &memoryBudgetProperties;

        vkGetPhysicalDeviceMemoryProperties2(m_gpu, &memoryProperties);

        uint64_t memoryUsage = 0;
        for (uint32_t i = 0; i < memoryProperties.memoryProperties.memoryTypeCount; i++)
            memoryUsage += memoryBudgetProperties.heapUsage[i];

        return memoryUsage;
    }
}
#endif
