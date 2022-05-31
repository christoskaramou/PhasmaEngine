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
#include "Renderer/Debug.h"

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
        dynamicCmdBuffers = {};
        shadowCmdBuffers = {};
        fences = {};
        semaphores = {};

        window = nullptr;
        graphicsFamilyId = 0;
        computeFamilyId = 0;
        transferFamilyId = 0;

        uniformBuffers = {};
        uniformImages = {};
    }

    RHI::~RHI()
    {
    }

    void RHI::CreateInstance(SDL_Window *window)
    {
        std::vector<const char *> instanceExtensions;
        std::vector<const char *> instanceLayers;
        std::vector<VkValidationFeatureEnableEXT> enabledFeatures;
        VkValidationFeaturesEXT validationFeatures{};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;

        // === Extentions ==============================
        unsigned extCount;
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr))
            PE_ERROR(SDL_GetError());
        instanceExtensions.resize(extCount);
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, instanceExtensions.data()))
            PE_ERROR(SDL_GetError());
        // =============================================
            
        uint32_t propertyCount;
        vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, nullptr);

        std::vector<VkExtensionProperties> extensions(propertyCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, extensions.data());

        for (auto &extension : extensions)
        {
            if (std::string(extension.extensionName) == "VK_KHR_get_physical_device_properties2")
            {
                instanceExtensions.push_back("VK_KHR_get_physical_device_properties2");
            }
        }

#ifdef _DEBUG
        Debug::GetInstanceUtils(instanceExtensions, instanceLayers);

        // === Validation Features =====================
        enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
        // enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
        // enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);
        enabledFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);

        validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledFeatures.size());
        validationFeatures.pEnabledValidationFeatures = enabledFeatures.data();
        // =============================================
#endif

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
        vkCreateInstance(&instInfo, nullptr, &instanceVK);
        instance = instanceVK;

#ifdef _DEBUG
        Debug::Init(instance);
        Debug::CreateDebugMessenger();
#endif
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
        GetGraphicsFamilyId();
        GetComputeFamilyId();
        GetTransferFamilyId();

        VkPhysicalDeviceProperties gpuPropertiesVK;
        vkGetPhysicalDeviceProperties(gpu, &gpuPropertiesVK);
        gpuName = gpuPropertiesVK.deviceName;
    }

    void RHI::GetGraphicsFamilyId()
    {
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE
        VkQueueFlags flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
#else
        VkQueueFlags flags = VK_QUEUE_GRAPHICS_BIT;
#endif
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, queueFamilyProperties.data());

        auto &properties = queueFamilyProperties;
        for (uint32_t i = 0; i < properties.size(); i++)
        {
            // find graphics queue family index
            VkBool32 suported;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface->Handle(), &suported);
            if (properties[i].queueFlags & flags && suported)
            {
                graphicsFamilyId = i;
                return;
            }
        }
        graphicsFamilyId = -1;
    }

    void RHI::GetTransferFamilyId()
    {
#ifdef UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE1
        transferFamilyId = graphicsFamilyId;
#else
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, queueFamilyProperties.data());

        VkQueueFlags flags = VK_QUEUE_TRANSFER_BIT;
        auto &properties = queueFamilyProperties;
        // prefer different families for different queue types, thus the reverse check
        for (int i = static_cast<int>(properties.size()) - 1; i >= 0; --i)
        {
            // find transfer queue family index
            if (properties[i].queueFlags & flags)
            {
                transferFamilyId = i;
                return;
            }
        }
        transferFamilyId = -1;
#endif
    }

    void RHI::GetComputeFamilyId()
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, queueFamilyProperties.data());

        VkQueueFlags flags = VK_QUEUE_TRANSFER_BIT;
        auto &properties = queueFamilyProperties;
        // prefer different families for different queue types, thus the reverse check
        for (int i = static_cast<int>(properties.size()) - 1; i >= 0; --i)
        {
            // find compute queue family index
            if (properties[i].queueFlags & flags)
            {
                computeFamilyId = i;
                return;
            }
        }
        computeFamilyId = -1;
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
        
        float priorities[]{1.0f}; // range : [0.0, 1.0]

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

        // graphics queue
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsFamilyId;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = priorities;
        queueCreateInfos.push_back(queueCreateInfo);

        // compute queue
        if (computeFamilyId != graphicsFamilyId)
        {
            queueCreateInfo.queueFamilyIndex = computeFamilyId;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // transfer queue
        if (transferFamilyId != graphicsFamilyId && transferFamilyId != computeFamilyId)
        {
            queueCreateInfo.queueFamilyIndex = transferFamilyId;
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
        vkCreateDevice(gpu, &deviceCreateInfo, nullptr, &deviceVK);
        device = deviceVK;
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
        vmaCreateAllocator(&allocator_info, &allocatorVK);
        allocator = allocatorVK;
    }

    void RHI::GetGraphicsQueue()
    {
        VkQueue queue;
        vkGetDeviceQueue(device, graphicsFamilyId, 0, &queue);
        graphicsQueue = queue;
    }

    void RHI::GetTransferQueue()
    {
        VkQueue queue;
        vkGetDeviceQueue(device, transferFamilyId, 0, &queue);
        transferQueue = queue;
    }

    void RHI::GetComputeQueue()
    {
        VkQueue queue;
        vkGetDeviceQueue(device, computeFamilyId, 0, &queue);
        computeQueue = queue;
    }

    void RHI::GetQueues()
    {
        GetGraphicsQueue();
        GetTransferQueue();
        GetComputeQueue();
    }

    void RHI::CreateSwapchain(Surface *surface)
    {
        swapchain = Swapchain::Create(surface);
    }

    void RHI::CreateCommandPools()
    {
        commandPool = CommandPool::Create(graphicsFamilyId);
        commandPool2 = CommandPool::Create(graphicsFamilyId);
        commandPoolTransfer = CommandPool::Create(transferFamilyId);
    }

    void RHI::CreateDescriptorPool(uint32_t maxDescriptorSets)
    {
        descriptorPool = DescriptorPool::Create(maxDescriptorSets);
    }

    void RHI::CreateCmdBuffers(uint32_t bufferCount)
    {
        dynamicCmdBuffers.resize(bufferCount);
        for (uint32_t i = 0; i < bufferCount; i++)
            dynamicCmdBuffers[i] = CommandBuffer::Create(commandPool, "dynamicCmdBuffer" + std::to_string(i));

        shadowCmdBuffers.resize(bufferCount);
        for (uint32_t i = 0; i < bufferCount; i++)
        {
            shadowCmdBuffers[i] = std::array<CommandBuffer *, SHADOWMAP_CASCADES>{};
            for (int j = 0; j < SHADOWMAP_CASCADES; j++)
            {
                shadowCmdBuffers[i][j] = CommandBuffer::Create(commandPool, "shadowCmdBuffer" + std::to_string(i) + "_" + std::to_string(j));
            }
        }
    }

    void RHI::CreateFences(uint32_t fenceCount)
    {
        fences.resize(fenceCount);
        for (uint32_t i = 0; i < fenceCount; i++)
            fences[i] = Fence::Create(true);
    }

    void RHI::CreateSemaphores(uint32_t semaphoresCount)
    {
        semaphores.resize(semaphoresCount);
        for (uint32_t i = 0; i < semaphoresCount; i++)
            semaphores[i] = Semaphore::Create();
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
        CreateSwapchain(surface);
        CreateDescriptorPool(15000); // max number of all descriptor sets to allocate
        CreateCmdBuffers(SWAPCHAIN_IMAGES);
        CreateSemaphores(SWAPCHAIN_IMAGES * 3);
        CreateFences(SWAPCHAIN_IMAGES);
    }

    void RHI::Destroy()
    {
        WaitDeviceIdle();

        for (auto *fence : fences)
            fence->Destroy();

        for (auto *semaphore : semaphores)
            semaphore->Destroy();

        descriptorPool->Destroy();

        commandPool->Destroy();
        commandPool2->Destroy();

        swapchain->Destroy();

        if (device)
        {
            vkDestroyDevice(device, nullptr);
            device = {};
        }

        surface->Destroy();

#ifdef _DEBUG
        Debug::DestroyDebugMessenger();
#endif

        if (instance)
            vkDestroyInstance(instance, nullptr);
    }

    void RHI::Submit(
        uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
        PipelineStageFlags *waitStages,
        uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
        uint32_t signalSemaphoresCount, Semaphore **signalSemaphores,
        Fence *signalFence,
        bool useGraphicsQueue)
    {
        std::vector<VkCommandBuffer> commandBuffersVK(commandBuffersCount);
        for (uint32_t i = 0; i < commandBuffersCount; i++)
            commandBuffersVK[i] = commandBuffers[i]->Handle();

        std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoresCount);
        for (uint32_t i = 0; i < waitSemaphoresCount; i++)
            waitSemaphoresVK[i] = waitSemaphores[i]->Handle();

        std::vector<VkSemaphore> signalSemaphoresVK(signalSemaphoresCount);
        for (uint32_t i = 0; i < signalSemaphoresCount; i++)
            signalSemaphoresVK[i] = signalSemaphores[i]->Handle();

        VkFence fenceVK = nullptr;
        if (signalFence)
            fenceVK = signalFence->Handle();

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = waitSemaphoresCount;
        si.pWaitSemaphores = waitSemaphoresVK.data();
        si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = commandBuffersCount;
        si.pCommandBuffers = commandBuffersVK.data();
        si.signalSemaphoreCount = signalSemaphoresCount;
        si.pSignalSemaphores = signalSemaphoresVK.data();

        if (useGraphicsQueue)
            vkQueueSubmit(graphicsQueue, 1, &si, fenceVK);
        else
            vkQueueSubmit(transferQueue, 1, &si, fenceVK);
    }

    void RHI::WaitFence(Fence *fence)
    {
        fence->Wait();
        fence->Reset();
    }

    void RHI::SubmitAndWaitFence(
        uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
        PipelineStageFlags *waitStages,
        uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
        uint32_t signalSemaphoresCount, Semaphore **signalSemaphores,
        bool useGraphicsQueue)
    {
        Fence *fence = Fence::Create(false);

        Submit(commandBuffersCount, commandBuffers, waitStages, waitSemaphoresCount,
               waitSemaphores, signalSemaphoresCount, signalSemaphores, fence, useGraphicsQueue);

        VkFence fenceVK = fence->Handle();
        if (vkWaitForFences(device, 1, &fenceVK, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            PE_ERROR("wait fences error!");
        fence->Destroy();
    }

    void RHI::Present(
        uint32_t swapchainCount, Swapchain **swapchains,
        uint32_t *imageIndices,
        uint32_t waitSemaphoreCount, Semaphore **waitSemaphores)
    {
        std::vector<VkSwapchainKHR> swapchainsVK(swapchainCount);
        for (uint32_t i = 0; i < swapchainCount; i++)
            swapchainsVK[i] = swapchains[i]->Handle();

        std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoreCount);
        for (uint32_t i = 0; i < waitSemaphoreCount; i++)
            waitSemaphoresVK[i] = waitSemaphores[i]->Handle();

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = waitSemaphoreCount;
        pi.pWaitSemaphores = waitSemaphoresVK.data();
        pi.swapchainCount = swapchainCount;
        pi.pSwapchains = swapchainsVK.data();
        pi.pImageIndices = imageIndices;

        if (vkQueuePresentKHR(graphicsQueue, &pi) != VK_SUCCESS)
            PE_ERROR("Present error!");
    }

    void RHI::WaitAndLockSubmits()
    {
        m_submit_mutex.lock();
    }

    void RHI::UnlockSubmits()
    {
        m_submit_mutex.unlock();
    }

    void RHI::WaitDeviceIdle()
    {
        vkDeviceWaitIdle(device);
    }

    void RHI::WaitGraphicsQueue()
    {
        vkQueueWaitIdle(graphicsQueue);
    }

    void RHI::WaitTransferQueue()
    {
        vkQueueWaitIdle(transferQueue);
    }
}
