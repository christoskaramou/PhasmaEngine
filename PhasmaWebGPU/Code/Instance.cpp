#include "Instance.h"
#include "Adapter.h"
#include "Device.h"
#include "Surface.h"
#include "Utils.h"
#include "WGPULimits.h"
#include "API/RHI.h"

namespace
{

    const char *VendorIdToString(uint32_t vendorID)
    {
        switch (vendorID)
        {
        case 0x1002:
            return "amd";
        case 0x10DE:
            return "nvidia";
        case 0x8086:
            return "intel";
        case 0x13B5:
            return "arm";
        case 0x5143:
            return "qualcomm";
        case 0x1010:
            return "imgtec";
        case 0x106B:
            return "apple";
        case 0x14E4:
            return "broadcom";
        default:
            return "";
        }
    }

    WGPUAdapterType VkDeviceTypeToAdapterType(VkPhysicalDeviceType t)
    {
        switch (t)
        {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return WGPUAdapterType_IntegratedGPU;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return WGPUAdapterType_DiscreteGPU;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return WGPUAdapterType_CPU;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        default:
            return WGPUAdapterType_Unknown;
        }
    }

} // namespace

extern "C"
{

    WGPUInstance wgpuCreateInstance(WGPUInstanceDescriptor const *descriptor)
    {
        (void)descriptor;
        auto *inst = new WGPUInstanceImpl();
        inst->rhi = &pe::RHII;
        return inst;
    }

    void wgpuInstanceAddRef(WGPUInstance instance)
    {
        if (instance)
            instance->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuInstanceRelease(WGPUInstance instance)
    {
        if (!instance)
            return;
        if (instance->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete instance;
    }

    WGPUSurface wgpuInstanceCreateSurface(WGPUInstance instance, WGPUSurfaceDescriptor const *descriptor)
    {
        // Prefer the instance's own RHI so multi-instance embeddings don't latch onto the global.
        pe::RHI *rhi = instance ? instance->rhi : &pe::RHII;
        if (!rhi)
            return nullptr;
        auto *surf = new WGPUSurfaceImpl();
        surf->surface = rhi->GetSurface();
        surf->swapchain = rhi->GetSwapchain();
        if (descriptor && descriptor->label.data)
            surf->label = pwgpu::ToString(descriptor->label);
        return surf;
    }

    void wgpuGetInstanceFeatures(WGPUSupportedInstanceFeatures *features)
    {
        if (!features)
            return;
        static const WGPUInstanceFeatureName s_instanceFeatures[] = {
            WGPUInstanceFeatureName_ShaderSourceSPIRV,
        };
        features->featureCount = 1;
        features->features = s_instanceFeatures;
    }

    WGPUStatus wgpuGetInstanceLimits(WGPUInstanceLimits *limits)
    {
        if (!limits)
            return WGPUStatus_Error;
        limits->nextInChain = nullptr;
        limits->timedWaitAnyMaxCount = 0;
        return WGPUStatus_Success;
    }

    WGPUBool wgpuHasInstanceFeature(WGPUInstanceFeatureName feature)
    {
        // We support SPIR-V shader sources natively (Vulkan backend)
        if (feature == WGPUInstanceFeatureName_ShaderSourceSPIRV)
            return WGPU_TRUE;
        return WGPU_FALSE;
    }

    // *FreeMembers are no-ops: the arrays we expose are static, not heap-allocated.
    // The symbols must exist for ABI compatibility.

    void wgpuAdapterInfoFreeMembers(WGPUAdapterInfo adapterInfo)
    {
        (void)adapterInfo;
    }

    void wgpuSupportedFeaturesFreeMembers(WGPUSupportedFeatures supportedFeatures)
    {
        (void)supportedFeatures;
    }

    void wgpuSupportedInstanceFeaturesFreeMembers(WGPUSupportedInstanceFeatures supportedInstanceFeatures)
    {
        (void)supportedInstanceFeatures;
    }

    void wgpuSupportedWGSLLanguageFeaturesFreeMembers(WGPUSupportedWGSLLanguageFeatures supportedWGSLLanguageFeatures)
    {
        (void)supportedWGSLLanguageFeatures;
    }

    void wgpuSurfaceCapabilitiesFreeMembers(WGPUSurfaceCapabilities surfaceCapabilities)
    {
        delete[] surfaceCapabilities.formats;
        delete[] surfaceCapabilities.presentModes;
        delete[] surfaceCapabilities.alphaModes;
    }

    WGPUBool wgpuInstanceHasWGSLLanguageFeature(WGPUInstance instance, WGPUWGSLLanguageFeatureName feature)
    {
        (void)instance;
        (void)feature;
        return WGPU_FALSE;
    }

    void wgpuInstanceGetWGSLLanguageFeatures(WGPUInstance instance, WGPUSupportedWGSLLanguageFeatures *features)
    {
        (void)instance;
        if (features)
        {
            features->featureCount = 0;
            features->features = nullptr;
        }
    }

    void wgpuInstanceProcessEvents(WGPUInstance instance)
    {
        if (!instance)
            return;
        instance->futures.ProcessEvents();
    }

    WGPUFuture wgpuInstanceRequestAdapter(WGPUInstance instance,
                                          WGPURequestAdapterOptions const *options,
                                          WGPURequestAdapterCallbackInfo callbackInfo)
    {
        // Helper: register an error callback into the instance's future registry.
        auto trackError = [&](const char *msg) -> WGPUFuture
        {
            if (!instance || !callbackInfo.callback)
                return instance ? instance->futures.NextId() : WGPUFuture{0};
            auto cb = callbackInfo.callback;
            auto u1 = callbackInfo.userdata1;
            auto u2 = callbackInfo.userdata2;
            std::string captured(msg);
            return instance->futures.TrackEvent(callbackInfo.mode,
                                                [cb, u1, u2, captured]()
                                                {
                                                    WGPUStringView sv{captured.c_str(), captured.size()};
                                                    cb(WGPURequestAdapterStatus_Unavailable, nullptr, sv, u1, u2);
                                                });
        };

        if (options)
        {
            if (options->forceFallbackAdapter)
                return trackError("PhasmaWebGPU: no fallback (software) adapter available");
            if (options->backendType != WGPUBackendType_Undefined &&
                options->backendType != WGPUBackendType_Vulkan)
                return trackError("PhasmaWebGPU: only WGPUBackendType_Vulkan is available on this host");
        }

        pe::RHI *rhi = instance ? instance->rhi : &pe::RHII;
        if (!rhi || !rhi->GetGpu())
            return trackError("PhasmaWebGPU: pe::RHI is not initialized");

        auto *adapter = new WGPUAdapterImpl();
        adapter->rhi = rhi;
        adapter->instance = instance;
        wgpuInstanceAddRef(instance);
        adapter->gpu = rhi->GetGpu();

        VkPhysicalDevice vkGpu = static_cast<VkPhysicalDevice>(adapter->gpu);
        vkGetPhysicalDeviceProperties(vkGpu, &adapter->vkProps);
        vkGetPhysicalDeviceFeatures(vkGpu, &adapter->vkFeatures);

        // Extended caps outside core VkPhysicalDeviceFeatures (shaderFloat16, depthClipEnable).
        // Safe to chain unsupported extension structs — the driver leaves the bool as 0.
        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceDepthClipEnableFeaturesEXT depthClipFeatures{};
        depthClipFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
        vk12Features.pNext = &depthClipFeatures;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vk12Features;
        vkGetPhysicalDeviceFeatures2(vkGpu, &features2);
        adapter->chainedCaps.shaderFloat16 = vk12Features.shaderFloat16 != 0;
        adapter->chainedCaps.depthClipEnable = depthClipFeatures.depthClipEnable != 0;

        adapter->deviceName = adapter->vkProps.deviceName;
        adapter->vendorName = VendorIdToString(adapter->vkProps.vendorID);
        adapter->architecture = "";
        adapter->driverDescription = "Vulkan";
        adapter->adapterType = VkDeviceTypeToAdapterType(adapter->vkProps.deviceType);
        adapter->backendType = WGPUBackendType_Vulkan;

        pwgpu_PopulateAdapterFeatureCache(*adapter);

        // Spec §4.2.1: adapter must expose BC or (ETC2 AND ASTC).
        const bool hasBC = adapter->vkFeatures.textureCompressionBC != 0;
        const bool hasETC2 = adapter->vkFeatures.textureCompressionETC2 != 0;
        const bool hasASTC = adapter->vkFeatures.textureCompressionASTC_LDR != 0;
        if (!hasBC && !(hasETC2 && hasASTC))
        {
            delete adapter;
            return trackError(
                "PhasmaWebGPU: GPU fails W3C §4.2.1 adapter capability guarantee "
                "(needs BC or (ETC2 + ASTC) texture compression)");
        }

        {
            WGPULimits adapterLim{};
            pwgpu::FillLimits(adapterLim, adapter->vkProps.limits);
            std::string badField;
            if (pwgpu::ValidateAdapterLimits(adapterLim, badField) != WGPUStatus_Success)
            {
                delete adapter;
                std::string errMsg = "PhasmaWebGPU: GPU fails W3C §4.2.1 default-or-better limit '" +
                                     badField + "'";
                return trackError(errMsg.c_str());
            }
        }

        if (!instance || !callbackInfo.callback)
            return instance ? instance->futures.NextId() : WGPUFuture{0};

        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        return instance->futures.TrackEvent(callbackInfo.mode,
                                            [cb, u1, u2, adapter]()
                                            {
                                                cb(WGPURequestAdapterStatus_Success, adapter, {nullptr, 0}, u1, u2);
                                            });
    }

    WGPUWaitStatus wgpuInstanceWaitAny(WGPUInstance instance,
                                       size_t futureCount,
                                       WGPUFutureWaitInfo *futures,
                                       uint64_t timeoutNS)
    {
        if (!instance)
            return WGPUWaitStatus_Error;
        return instance->futures.WaitAny(futureCount, futures, timeoutNS);
    }

} // extern "C"
