#include "Adapter.h"
#include "Device.h"
#include "WGPULimits.h"
#include "Utils.h"

namespace
{

    bool AdapterSupportsFeature(const WGPUAdapterImpl &a, WGPUFeatureName feature)
    {
        switch (feature)
        {
        case WGPUFeatureName_TextureCompressionBC:
            return a.vkFeatures.textureCompressionBC != 0;
        case WGPUFeatureName_TextureCompressionETC2:
            return a.vkFeatures.textureCompressionETC2 != 0;
        case WGPUFeatureName_TextureCompressionASTC:
            return a.vkFeatures.textureCompressionASTC_LDR != 0;
        case WGPUFeatureName_IndirectFirstInstance:
            return a.vkFeatures.drawIndirectFirstInstance != 0;
        case WGPUFeatureName_TimestampQuery:
            return a.vkProps.limits.timestampComputeAndGraphics != 0;
        case WGPUFeatureName_DualSourceBlending:
            return a.vkFeatures.dualSrcBlend != 0;
        case WGPUFeatureName_ClipDistances:
            return a.vkFeatures.shaderClipDistance != 0;
        case WGPUFeatureName_ShaderF16:
            return a.chainedCaps.shaderFloat16;
        case WGPUFeatureName_DepthClipControl:
            return a.chainedCaps.depthClipEnable;
        // Format-properties-dependent features deferred until the format pipeline lands.
        default:
            return false;
        }
    }

    void CollectSupportedFeatures(const WGPUAdapterImpl &a, std::vector<WGPUFeatureName> &out)
    {
        out.push_back(WGPUFeatureName_CoreFeaturesAndLimits);

        static constexpr WGPUFeatureName kCandidates[] = {
            WGPUFeatureName_TextureCompressionBC,
            WGPUFeatureName_TextureCompressionETC2,
            WGPUFeatureName_TextureCompressionASTC,
            WGPUFeatureName_IndirectFirstInstance,
            WGPUFeatureName_TimestampQuery,
            WGPUFeatureName_ShaderF16,
            WGPUFeatureName_DepthClipControl,
            WGPUFeatureName_DualSourceBlending,
            WGPUFeatureName_ClipDistances,
        };
        for (WGPUFeatureName f : kCandidates)
        {
            if (AdapterSupportsFeature(a, f))
                out.push_back(f);
        }
    }

} // namespace

void pwgpu_PopulateAdapterFeatureCache(WGPUAdapterImpl &a)
{
    a.supportedFeatures.clear();
    CollectSupportedFeatures(a, a.supportedFeatures);
}

extern "C"
{

    void wgpuAdapterAddRef(WGPUAdapter adapter)
    {
        if (adapter)
            adapter->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuAdapterRelease(WGPUAdapter adapter)
    {
        if (!adapter)
            return;
        if (adapter->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete adapter;
    }

    void wgpuAdapterGetFeatures(WGPUAdapter adapter, WGPUSupportedFeatures *features)
    {
        if (!features)
            return;
        features->featureCount = 0;
        features->features = nullptr;
        if (!adapter)
            return;

        if (adapter->supportedFeatures.empty())
            pwgpu_PopulateAdapterFeatureCache(*adapter);

        features->featureCount = adapter->supportedFeatures.size();
        features->features = adapter->supportedFeatures.empty()
                                 ? nullptr
                                 : adapter->supportedFeatures.data();
    }

    WGPUStatus wgpuAdapterGetInfo(WGPUAdapter adapter, WGPUAdapterInfo *info)
    {
        if (!adapter || !info)
            return WGPUStatus_Error;

        info->nextInChain = nullptr;
        info->vendor = pwgpu::ToStringView(adapter->vendorName);
        info->architecture = pwgpu::ToStringView(adapter->architecture);
        info->device = pwgpu::ToStringView(adapter->deviceName);
        info->description = pwgpu::ToStringView(adapter->driverDescription);
        info->backendType = adapter->backendType;
        info->adapterType = adapter->adapterType;
        info->vendorID = adapter->vkProps.vendorID;
        info->deviceID = adapter->vkProps.deviceID;
        info->subgroupMinSize = 4;
        info->subgroupMaxSize = 128;

        return WGPUStatus_Success;
    }

    WGPUStatus wgpuAdapterGetLimits(WGPUAdapter adapter, WGPULimits *limits)
    {
        if (!adapter || !limits)
            return WGPUStatus_Error;
        pwgpu::FillLimits(*limits, adapter->vkProps.limits);
        return WGPUStatus_Success;
    }

    WGPUBool wgpuAdapterHasFeature(WGPUAdapter adapter, WGPUFeatureName feature)
    {
        if (!adapter)
            return WGPU_FALSE;
        return AdapterSupportsFeature(*adapter, feature) ? WGPU_TRUE : WGPU_FALSE;
    }

    WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter adapter,
                                        WGPUDeviceDescriptor const *descriptor,
                                        WGPURequestDeviceCallbackInfo callbackInfo)
    {
        if (!adapter || adapter->consumed)
        {
            if (callbackInfo.callback)
            {
                WGPUStringView msg = pwgpu::ToStringView(
                    "PhasmaWebGPU: adapter is null, expired, or already consumed by a prior requestDevice");
                callbackInfo.callback(WGPURequestDeviceStatus_Error, nullptr, msg,
                                      callbackInfo.userdata1, callbackInfo.userdata2);
            }
            return WGPUFuture{pwgpu::NextFutureId()};
        }

        if (adapter->supportedFeatures.empty())
            pwgpu_PopulateAdapterFeatureCache(*adapter);

        if (descriptor && descriptor->requiredFeatureCount > 0 && descriptor->requiredFeatures)
        {
            for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i)
            {
                WGPUFeatureName req = descriptor->requiredFeatures[i];
                if (!AdapterSupportsFeature(*adapter, req))
                {
                    if (callbackInfo.callback)
                    {
                        WGPUStringView msg = pwgpu::ToStringView(
                            "PhasmaWebGPU: requiredFeatures contains a feature not supported by this adapter");
                        callbackInfo.callback(WGPURequestDeviceStatus_Error, nullptr, msg,
                                              callbackInfo.userdata1, callbackInfo.userdata2);
                    }
                    return WGPUFuture{pwgpu::NextFutureId()};
                }
            }
        }

        WGPULimits adapterLim{};
        pwgpu::FillLimits(adapterLim, adapter->vkProps.limits);

        if (descriptor && descriptor->requiredLimits)
        {
            std::string bad;
            if (pwgpu::ValidateRequestedLimits(adapterLim, *descriptor->requiredLimits, bad) != WGPUStatus_Success)
            {
                if (callbackInfo.callback)
                {
                    std::string what = "PhasmaWebGPU: requiredLimits violation on '" + bad + "'";
                    WGPUStringView msg = pwgpu::ToStringView(what);
                    callbackInfo.callback(WGPURequestDeviceStatus_Error, nullptr, msg,
                                          callbackInfo.userdata1, callbackInfo.userdata2);
                }
                return WGPUFuture{pwgpu::NextFutureId()};
            }
        }

        const bool canFulfill = adapter->rhi && adapter->rhi->GetDevice();

        auto *dev = new WGPUDeviceImpl();
        dev->rhi = adapter->rhi;
        dev->peQueue = canFulfill ? adapter->rhi->GetMainQueue() : nullptr;

        auto appendFeatureUnique = [](std::vector<WGPUFeatureName> &v, WGPUFeatureName f)
        {
            for (WGPUFeatureName existing : v)
                if (existing == f)
                    return;
            v.push_back(f);
        };
        if (descriptor && descriptor->requiredFeatureCount > 0 && descriptor->requiredFeatures)
        {
            for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i)
                appendFeatureUnique(dev->features, descriptor->requiredFeatures[i]);
        }
        appendFeatureUnique(dev->features, WGPUFeatureName_CoreFeaturesAndLimits);

        dev->limits = pwgpu::ResolveDeviceLimits(adapterLim,
                                                 descriptor ? descriptor->requiredLimits : nullptr);

        dev->adapterVendor = adapter->vendorName;
        dev->adapterArchitecture = adapter->architecture;
        dev->adapterDeviceName = adapter->deviceName;
        dev->adapterDescription = adapter->driverDescription;
        dev->adapterType = adapter->adapterType;
        dev->adapterBackend = adapter->backendType;
        dev->adapterVendorID = adapter->vkProps.vendorID;
        dev->adapterDeviceID = adapter->vkProps.deviceID;

        if (descriptor)
        {
            if (descriptor->label.data)
                dev->label = pwgpu::ToString(descriptor->label);
            if (descriptor->deviceLostCallbackInfo.callback)
                dev->deviceLostCallbackInfo = descriptor->deviceLostCallbackInfo;
            if (descriptor->uncapturedErrorCallbackInfo.callback)
                dev->uncapturedErrorCallbackInfo = descriptor->uncapturedErrorCallbackInfo;
        }

        adapter->consumed = true;

        if (!canFulfill)
        {
            // Device is born lost: success callback must fire BEFORE the lost
            // callback so the consumer receives its handle before the loss notification.
            dev->destroyed = true;
            if (callbackInfo.callback)
                callbackInfo.callback(WGPURequestDeviceStatus_Success, dev, {nullptr, 0},
                                      callbackInfo.userdata1, callbackInfo.userdata2);
            if (dev->deviceLostCallbackInfo.callback)
            {
                WGPUStringView lostMsg = pwgpu::ToStringView(
                    "PhasmaWebGPU: adapter has no backing pe::RHI device; device lost at creation");
                WGPUDevice selfHandle = dev;
                dev->deviceLostCallbackInfo.callback(&selfHandle,
                                                     WGPUDeviceLostReason_Unknown,
                                                     lostMsg,
                                                     dev->deviceLostCallbackInfo.userdata1,
                                                     dev->deviceLostCallbackInfo.userdata2);
            }
            return WGPUFuture{pwgpu::NextFutureId()};
        }

        dev->queue = new WGPUQueueImpl();
        dev->queue->device = dev;
        dev->queue->peQueue = dev->peQueue;

        if (callbackInfo.callback)
            callbackInfo.callback(WGPURequestDeviceStatus_Success, dev, {nullptr, 0},
                                  callbackInfo.userdata1, callbackInfo.userdata2);

        return WGPUFuture{pwgpu::NextFutureId()};
    }

} // extern "C"
