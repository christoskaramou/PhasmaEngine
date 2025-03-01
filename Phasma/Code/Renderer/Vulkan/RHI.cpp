#ifdef PE_VULKAN
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Queue.h"
#include "Renderer/Buffer.h"
#include "Renderer/Surface.h"
#include "Renderer/Downsampler.h"

#if defined(_WIN32)
// On Windows, Vulkan commands use the stdcall convention
#define VKAPI_CALL __stdcall
#else
// On other platforms, use the default calling convention
#define VKAPI_CALL
#endif

namespace pe
{
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
        CreateSwapchain(m_surface);
        CreateDescriptorPool(150); // General purpose descriptor pool
        InitDownSampler();
    }

    void RHI::Destroy()
    {
        WaitDeviceIdle();

        Queue::Clear();
        PeHandleBase::DestroyAllIHandles();
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
        // =============================================

#if !defined(PE_RELEASE) || !defined(PE_MINSIZEREL)
        // === Debugging ===============================
        if (RHII.IsInstanceExtensionValid("VK_EXT_debug_utils"))
            instanceExtensions.push_back("VK_EXT_debug_utils");
        // =============================================
#endif

        // uint32_t apiVersion;
        // vkEnumerateInstanceVersion(&apiVersion);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "PhasmaEngine";
        appInfo.pEngineName = "PhasmaEngine";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
        m_maxPushConstantsSize = gpuPropertiesVK.properties.limits.maxPushConstantsSize;
        m_maxDrawIndirectCount = gpuPropertiesVK.properties.limits.maxDrawIndirectCount;
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
        deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        if (IsDeviceExtensionValid(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
            deviceExtensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
        }

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

        // Vulkan 1.1 features
        VkPhysicalDeviceVulkan11Features deviceFeatures11{};
        deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        // Vulkan 1.2 features
        VkPhysicalDeviceVulkan12Features deviceFeatures12{};
        deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        deviceFeatures12.pNext = &deviceFeatures11;
        // Vulkan 1.3 features
        VkPhysicalDeviceVulkan13Features deviceFeatures13{};
        deviceFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        deviceFeatures13.pNext = &deviceFeatures12;

        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.pNext = &deviceFeatures13;
        // Supported features
        vkGetPhysicalDeviceFeatures2(m_gpu, &deviceFeatures2);

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

        // main queue
        m_mainQueue = Queue::Get(QueueType::GraphicsBit | QueueType::PresentBit | QueueType::ComputeBit | QueueType::TransferBit, 1);

        // render queue
        m_renderQueue = Queue::Get(QueueType::GraphicsBit | QueueType::PresentBit, 1, {m_mainQueue});
        if (!m_renderQueue)
            m_renderQueue = Queue::Get(QueueType::GraphicsBit | QueueType::PresentBit, 1);

        // compute queue
        m_computeQueue = Queue::Get(QueueType::ComputeBit, 1, {m_mainQueue, m_renderQueue});
        if (!m_computeQueue)
            m_computeQueue = Queue::Get(QueueType::ComputeBit, 1, {m_renderQueue});
        if (!m_computeQueue)
            m_computeQueue = Queue::Get(QueueType::ComputeBit, 1);

        // transfer queue
        m_transferQueue = Queue::Get(QueueType::TransferBit, 1, {m_mainQueue, m_renderQueue, m_computeQueue});
        if (!m_transferQueue)
            m_transferQueue = Queue::Get(QueueType::TransferBit, 1, {m_renderQueue, m_computeQueue});
        if (!m_transferQueue)
            m_transferQueue = Queue::Get(QueueType::TransferBit, 1, {m_computeQueue});
        if (!m_transferQueue)
            m_transferQueue = Queue::Get(QueueType::TransferBit, 1);
    }

    void RHI::CreateSwapchain(Surface *surface)
    {
        m_swapchain = Swapchain::Create(surface, "RHI_swapchain");
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

    Semaphore *RHI::GetFreeBinarySemaphore(uint32_t frame)
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

    void RHI::ClaimUsedBinarySemaphores(uint32_t frame)
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

    void RHI::ChangePresentMode(PresentMode mode)
    {
        WaitDeviceIdle();

        m_surface->SetPresentMode(mode);
        Swapchain::Destroy(m_swapchain);
        CreateSwapchain(m_surface);

        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
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

        EventSystem::DispatchEvent(EventSetWindowTitle, title);
    }
}
#endif
