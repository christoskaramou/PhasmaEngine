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

#include "Renderer/RHI.h"
#include "Systems/RendererSystem.h"
#include "Renderer/Command.h"
#include "Renderer/Fence.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Fence.h"
#include "Renderer/Image.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Queue.h"

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
        device = {};
        semaphores = {};

        window = nullptr;

        uniformBuffers = {};
        uniformImages = {};
    }

    RHI::~RHI()
    {
    }

    void RHI::Init(SDL_Window *window)
    {
        this->window = window;

        CreateInstance(window);
        CreateSurface();
        GetGpu();
        GetSurfaceProperties();
        CreateDevice();
        CreateAllocator();
        GetQueues();
        CreateCommandPools();
        CreateCmdBuffers(SWAPCHAIN_IMAGES);
        CreateSwapchain(surface);
        CreateDescriptorPool(15000); // max number of all descriptor sets to allocate
        CreateSemaphores(SWAPCHAIN_IMAGES * 3);
    }

    void RHI::Destroy()
    {
        WaitDeviceIdle();
        Queue::Clear();
        CommandBuffer::Clear();
        CommandPool::Clear();
        Fence::Clear();
        for (auto *semaphore : semaphores)
            Semaphore::Destroy(semaphore);
        DescriptorPool::Destroy(descriptorPool);
        Swapchain::Destroy(swapchain);
        if (device)
            vkDestroyDevice(device, nullptr);
        Surface::Destroy(surface);
        Debug::DestroyDebugMessenger();
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }

    void RHI::CreateInstance(SDL_Window *window)
    {
        std::vector<const char *> instanceExtensions{};
        std::vector<const char *> instanceLayers{};

        // === Extentions ==============================
        unsigned extCount;
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr))
            PE_ERROR(SDL_GetError());
        instanceExtensions.resize(extCount);
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, instanceExtensions.data()))
            PE_ERROR(SDL_GetError());

        if (IsInstanceExtensionValid("VK_KHR_get_physical_device_properties2"))
            instanceExtensions.push_back("VK_KHR_get_physical_device_properties2");
        // =============================================

        // === Debugging ===============================
        Debug::GetInstanceUtils(instanceExtensions, instanceLayers);
        std::vector<VkValidationFeatureEnableEXT> enabledFeatures{};
        VkValidationFeaturesEXT validationFeatures{};
        if (IsInstanceLayerValid("VK_LAYER_KHRONOS_validation"))
        {
            enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
            enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            // enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
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
        instance = instanceVK;

        Debug::Init(instance);
        Debug::CreateDebugMessenger();
    }

    void RHI::CreateSurface()
    {
        surface = Surface::Create(window);
    }

    void RHI::GetSurfaceProperties()
    {
        surface->FindProperties();
    }

    void RHI::GetGpu()
    {
        uint32_t gpuCount;
        vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
        vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);

        std::vector<VkPhysicalDevice> gpuList(gpuCount);
        vkEnumeratePhysicalDevices(instance, &gpuCount, gpuList.data());

        std::vector<VkPhysicalDevice> validGpuList{};
        VkPhysicalDevice discreteGpu;

        for (auto &GPU : gpuList)
        {
            uint32_t queueFamPropCount;
            vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamPropCount, nullptr);

            std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
            vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamPropCount, queueFamilyProperties.data());

            VkQueueFlags flags{};

            for (auto &qfp : queueFamilyProperties)
            {
                if (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    flags |= VK_QUEUE_GRAPHICS_BIT;
                if (qfp.queueFlags & VK_QUEUE_COMPUTE_BIT)
                    flags |= VK_QUEUE_COMPUTE_BIT;
                if (qfp.queueFlags & VK_QUEUE_TRANSFER_BIT)
                    flags |= VK_QUEUE_TRANSFER_BIT;
            }

            if (flags & VK_QUEUE_GRAPHICS_BIT &&
                flags & VK_QUEUE_COMPUTE_BIT &&
                flags & VK_QUEUE_TRANSFER_BIT)
            {
                VkPhysicalDeviceProperties properties;
                vkGetPhysicalDeviceProperties(GPU, &properties);
                if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    discreteGpu = GPU;
                    break;
                }

                validGpuList.push_back(GPU);
            }
        }

        gpu = discreteGpu ? discreteGpu : validGpuList[0];

        VkPhysicalDeviceProperties gpuPropertiesVK;
        vkGetPhysicalDeviceProperties(gpu, &gpuPropertiesVK);
        gpuName = gpuPropertiesVK.deviceName;
    }

    bool RHI::IsInstanceExtensionValid(const char *name)
    {
        uint32_t count;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);

        std::vector<VkExtensionProperties> extensions(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

        // === Debug Extensions ========================
        for (auto &extension : extensions)
        {
            if (std::string(extension.extensionName) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsInstanceLayerValid(const char *name)
    {
        uint32_t count;
        vkEnumerateInstanceLayerProperties(&count, nullptr);

        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());

        for (auto &layer : layers)
        {
            if (std::string(layer.layerName) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsDeviceExtensionValid(const char *name)
    {
        if (!gpu)
            PE_ERROR("No GPU found!");

        uint32_t count;
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);

        std::vector<VkExtensionProperties> extensions(count);
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data());

        for (auto &extension : extensions)
            if (std::string(extension.extensionName) == name)
                return true;

        return false;
    }

    void RHI::CreateDevice()
    {
        std::vector<const char *> deviceExtensions{};

        if (IsDeviceExtensionValid(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, queueFamilyProperties.data());

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

        // Indexing features
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

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

        // Queue for supported features
        vkGetPhysicalDeviceFeatures2(gpu, &deviceFeatures2);

        // Check for bindless descriptors
        if (!(deviceFeatures12.descriptorBindingPartiallyBound &&
              deviceFeatures12.runtimeDescriptorArray &&
              deviceFeatures12.shaderSampledImageArrayNonUniformIndexing &&
              deviceFeatures12.descriptorBindingVariableDescriptorCount))
            PE_ERROR("Bindless descriptors are not supported on this device!");

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.pNext = &deviceFeatures2;

        VkDevice deviceVK;
        PE_CHECK(vkCreateDevice(gpu, &deviceCreateInfo, nullptr, &deviceVK));
        device = deviceVK;

        // Debug::SetObjectName(instance, VK_OBJECT_TYPE_INSTANCE, "RHI_instance");
        Debug::SetObjectName(surface->Handle(), VK_OBJECT_TYPE_SURFACE_KHR, "RHI_surface");
        Debug::SetObjectName(gpu, VK_OBJECT_TYPE_PHYSICAL_DEVICE, "RHI_gpu");
        Debug::SetObjectName(device, VK_OBJECT_TYPE_DEVICE, "RHI_device");
    }

    void RHI::CreateAllocator()
    {
        uint32_t apiVersion;
        vkEnumerateInstanceVersion(&apiVersion);

        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.physicalDevice = gpu;
        allocator_info.device = device;
        allocator_info.instance = instance;
        allocator_info.vulkanApiVersion = apiVersion;

        VmaAllocator allocatorVK;
        PE_CHECK(vmaCreateAllocator(&allocator_info, &allocatorVK));
        allocator = allocatorVK;
    }

    void RHI::GetQueues()
    {
        Queue::Init(gpu, device, surface->Handle());
        generalQueue = Queue::GetNext(QueueType::GraphicsBit, 1);
    }

    void RHI::CreateSwapchain(Surface *surface)
    {
        swapchain = Swapchain::Create(surface, "RHI_swapchain");
    }

    void RHI::CreateCommandPools()
    {
        CommandPool::Init(gpu);
    }

    void RHI::CreateDescriptorPool(uint32_t maxDescriptorSets)
    {
        descriptorPool = DescriptorPool::Create(maxDescriptorSets, "RHI_descriptor_pool");
    }

    void RHI::CreateCmdBuffers(uint32_t bufferCount)
    {
        CommandBuffer::Init(gpu, bufferCount);
        generalCmd = CommandBuffer::GetNext(generalQueue->GetFamilyId());
    }

    void RHI::CreateSemaphores(uint32_t semaphoresCount)
    {
        semaphores.resize(semaphoresCount);
        for (uint32_t i = 0; i < semaphoresCount; i++)
            semaphores[i] = Semaphore::Create("RHI_semaphore_" + std::to_string(i));
    }

    Format RHI::GetDepthFormat()
    {
        static VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        if (depthFormat == VK_FORMAT_UNDEFINED)
        {
            std::vector<VkFormat> candidates = {
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D24_UNORM_S8_UINT};

            for (auto &df : candidates)
            {
                VkFormatProperties props;
                vkGetPhysicalDeviceFormatProperties(gpu, df, &props);
                if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                {
                    depthFormat = df;
                    break;
                }
            }

            if (depthFormat == VK_FORMAT_UNDEFINED)
                PE_ERROR("Depth format is undefined");
        }

        return (Format)depthFormat;
    }

    void RHI::WaitDeviceIdle()
    {
        vkDeviceWaitIdle(device);
    }
}
