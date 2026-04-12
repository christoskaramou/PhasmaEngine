#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "Sampler.h"
#include "BindGroup.h"
#include "PipelineLayout.h"
#include "ShaderModule.h"
#include "RenderPipeline.h"
#include "ComputePipeline.h"
#include "CommandEncoder.h"
#include "RenderBundle.h"
#include "QuerySet.h"
#include "WGPULimits.h"
#include "Utils.h"

namespace
{
    bool DeviceCanCreate(WGPUDeviceImpl *device, const void *descriptor,
                         const char *apiName, bool requireDescriptor)
    {
        if (!device)
            return false;
        if (device->destroyed)
        {
            std::string what = std::string("PhasmaWebGPU: ") + apiName +
                               " called on a destroyed device";
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(what));
            return false;
        }
        if (requireDescriptor && !descriptor)
        {
            std::string what = std::string("PhasmaWebGPU: ") + apiName +
                               " called with null descriptor";
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(what));
            return false;
        }
        return true;
    }
} // namespace

void WGPUDeviceImpl::reportError(WGPUErrorType type, WGPUStringView message)
{
    // Drop the lock before firing the uncaptured-error callback — it may re-enter device APIs.
    {
        std::lock_guard<std::mutex> lock(errorScopeMutex);
        for (int i = static_cast<int>(errorScopeStack.size()) - 1; i >= 0; --i)
        {
            pwgpu::ErrorScope &scope = errorScopeStack[static_cast<size_t>(i)];
            bool captures = false;
            switch (scope.filter)
            {
            case WGPUErrorFilter_Validation:
                captures = (type == WGPUErrorType_Validation);
                break;
            case WGPUErrorFilter_OutOfMemory:
                captures = (type == WGPUErrorType_OutOfMemory);
                break;
            case WGPUErrorFilter_Internal:
                captures = (type == WGPUErrorType_Internal);
                break;
            default:
                break;
            }
            if (captures && !scope.hasCapturedError)
            {
                scope.hasCapturedError = true;
                scope.capturedErrorType = type;
                scope.capturedErrorMessage = pwgpu::ToString(message);
                return;
            }
        }
    }
    if (uncapturedErrorCallbackInfo.callback)
    {
        WGPUDevice selfHandle = this;
        uncapturedErrorCallbackInfo.callback(&selfHandle,
                                             type,
                                             message,
                                             uncapturedErrorCallbackInfo.userdata1,
                                             uncapturedErrorCallbackInfo.userdata2);
    }
}

extern "C"
{

    void wgpuDeviceAddRef(WGPUDevice device)
    {
        if (device)
            device->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuDeviceRelease(WGPUDevice device)
    {
        if (!device)
            return;
        if (device->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (device->queue)
            {
                // Clear back-pointer before release so a late wgpuQueueRelease won't dereference us.
                device->queue->device = nullptr;
                wgpuQueueRelease(device->queue);
                device->queue = nullptr;
            }
            delete device;
        }
    }

    void wgpuDeviceDestroy(WGPUDevice device)
    {
        if (!device || device->destroyed)
            return;
        device->destroyed = true;
        if (device->deviceLostCallbackInfo.callback)
        {
            WGPUStringView msg = pwgpu::ToStringView("Device destroyed.");
            device->deviceLostCallbackInfo.callback(&device,
                                                    WGPUDeviceLostReason_Destroyed,
                                                    msg,
                                                    device->deviceLostCallbackInfo.userdata1,
                                                    device->deviceLostCallbackInfo.userdata2);
        }
    }

    WGPUQueue wgpuDeviceGetQueue(WGPUDevice device)
    {
        if (!device)
            return nullptr;
        wgpuQueueAddRef(device->queue);
        return device->queue;
    }

    void wgpuDeviceGetFeatures(WGPUDevice device, WGPUSupportedFeatures *features)
    {
        if (!features)
            return;
        features->featureCount = 0;
        features->features = nullptr;
        if (!device)
            return;
        features->featureCount = device->features.size();
        features->features = device->features.empty() ? nullptr : device->features.data();
    }

    WGPUStatus wgpuDeviceGetLimits(WGPUDevice device, WGPULimits *limits)
    {
        if (!device || !limits)
            return WGPUStatus_Error;
        *limits = device->limits;
        return WGPUStatus_Success;
    }

    WGPUStatus wgpuDeviceGetAdapterInfo(WGPUDevice device, WGPUAdapterInfo *info)
    {
        if (!device || !info)
            return WGPUStatus_Error;
        info->nextInChain = nullptr;
        info->vendor = pwgpu::ToStringView(device->adapterVendor);
        info->architecture = pwgpu::ToStringView(device->adapterArchitecture);
        info->device = pwgpu::ToStringView(device->adapterDeviceName);
        info->description = pwgpu::ToStringView(device->adapterDescription);
        info->adapterType = device->adapterType;
        info->backendType = device->adapterBackend;
        info->vendorID = device->adapterVendorID;
        info->deviceID = device->adapterDeviceID;
        info->subgroupMinSize = 4;
        info->subgroupMaxSize = 128;
        return WGPUStatus_Success;
    }

    WGPUBool wgpuDeviceHasFeature(WGPUDevice device, WGPUFeatureName feature)
    {
        if (!device)
            return WGPU_FALSE;
        for (WGPUFeatureName f : device->features)
        {
            if (f == feature)
                return WGPU_TRUE;
        }
        return WGPU_FALSE;
    }

    void wgpuDeviceSetLabel(WGPUDevice device, WGPUStringView label)
    {
        if (device)
            device->label = pwgpu::ToString(label);
    }

    void wgpuDevicePushErrorScope(WGPUDevice device, WGPUErrorFilter filter)
    {
        if (!device)
            return;
        pwgpu::ErrorScope scope;
        scope.filter = filter;
        std::lock_guard<std::mutex> lock(device->errorScopeMutex);
        device->errorScopeStack.push_back(scope);
    }

    WGPUFuture wgpuDevicePopErrorScope(WGPUDevice device, WGPUPopErrorScopeCallbackInfo callbackInfo)
    {
        pwgpu::ErrorScope scope;
        bool haveScope = false;
        if (device)
        {
            std::lock_guard<std::mutex> lock(device->errorScopeMutex);
            if (!device->errorScopeStack.empty())
            {
                scope = device->errorScopeStack.back();
                device->errorScopeStack.pop_back();
                haveScope = true;
            }
        }
        if (!haveScope)
        {
            if (callbackInfo.callback)
                callbackInfo.callback(WGPUPopErrorScopeStatus_Error, WGPUErrorType_NoError, {nullptr, 0},
                                      callbackInfo.userdata1, callbackInfo.userdata2);
            return WGPUFuture{pwgpu::NextFutureId()};
        }
        if (callbackInfo.callback)
        {
            WGPUStringView msg = pwgpu::ToStringView(scope.capturedErrorMessage);
            callbackInfo.callback(WGPUPopErrorScopeStatus_Success,
                                  scope.hasCapturedError ? scope.capturedErrorType : WGPUErrorType_NoError,
                                  msg,
                                  callbackInfo.userdata1,
                                  callbackInfo.userdata2);
        }
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    WGPUFuture wgpuDeviceGetLostFuture(WGPUDevice device)
    {
        (void)device;
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, WGPUBufferDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateBuffer", true))
            return nullptr;

        const WGPUBufferUsage usage = descriptor->usage;
        const uint64_t size = descriptor->size;
        const bool mappedAtCreation = descriptor->mappedAtCreation != WGPU_FALSE;

        auto reportValidation = [&](const char *what) -> WGPUBuffer
        {
            std::string msg = std::string("wgpuDeviceCreateBuffer: ") + what;
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
            auto *bad = new WGPUBufferImpl();
            bad->device = device;
            wgpuDeviceAddRef(device);
            bad->usage = usage;
            bad->size = size;
            bad->invalid = true;
            bad->mapState = mappedAtCreation ? WGPUBufferMapState_Mapped : WGPUBufferMapState_Unmapped;
            if (descriptor->label.data)
                bad->label = pwgpu::ToString(descriptor->label);
            return bad;
        };

        if (usage == WGPUBufferUsage_None)
            return reportValidation("descriptor.usage must not be 0");

        const WGPUBufferUsage kMapReadAllowed = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        const WGPUBufferUsage kMapWriteAllowed = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc;
        if ((usage & WGPUBufferUsage_MapRead) && ((usage & ~kMapReadAllowed) != 0))
            return reportValidation("MAP_READ may only combine with COPY_DST");
        if ((usage & WGPUBufferUsage_MapWrite) && ((usage & ~kMapWriteAllowed) != 0))
            return reportValidation("MAP_WRITE may only combine with COPY_SRC");

        if (size > device->limits.maxBufferSize)
            return reportValidation("descriptor.size exceeds device.limits.maxBufferSize");

        if (mappedAtCreation && (size % 4 != 0))
            return reportValidation("mappedAtCreation requires size to be a multiple of 4");

        vk::BufferUsageFlags2 vkUsage{};
        if (usage & WGPUBufferUsage_CopySrc)
            vkUsage |= vk::BufferUsageFlagBits2::eTransferSrc;
        if (usage & WGPUBufferUsage_CopyDst)
            vkUsage |= vk::BufferUsageFlagBits2::eTransferDst;
        if (usage & WGPUBufferUsage_Index)
            vkUsage |= vk::BufferUsageFlagBits2::eIndexBuffer;
        if (usage & WGPUBufferUsage_Vertex)
            vkUsage |= vk::BufferUsageFlagBits2::eVertexBuffer;
        if (usage & WGPUBufferUsage_Uniform)
            vkUsage |= vk::BufferUsageFlagBits2::eUniformBuffer;
        if (usage & WGPUBufferUsage_Storage)
            vkUsage |= vk::BufferUsageFlagBits2::eStorageBuffer;
        if (usage & WGPUBufferUsage_Indirect)
            vkUsage |= vk::BufferUsageFlagBits2::eIndirectBuffer;
        if (usage & WGPUBufferUsage_QueryResolve)
            vkUsage |= vk::BufferUsageFlagBits2::eTransferDst;
        if (usage & WGPUBufferUsage_MapRead)
            vkUsage |= vk::BufferUsageFlagBits2::eTransferDst;
        if (usage & WGPUBufferUsage_MapWrite)
            vkUsage |= vk::BufferUsageFlagBits2::eTransferSrc;

        if (!vkUsage)
            vkUsage = vk::BufferUsageFlagBits2::eTransferSrc;

        const bool needsHostAccess =
            (usage & (WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite)) != 0 || mappedAtCreation;

        VmaAllocationCreateFlags vmaFlags = 0;
        if (needsHostAccess)
        {
            if (usage & WGPUBufferUsage_MapRead)
                vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            else
                vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        auto *buf = new WGPUBufferImpl();
        buf->device = device;
        wgpuDeviceAddRef(device);
        buf->usage = usage;
        buf->size = size;
        buf->hostVisible = needsHostAccess;
        if (descriptor->label.data)
            buf->label = pwgpu::ToString(descriptor->label);

        const std::string peName = buf->label.empty() ? std::string("wgpu_buffer") : buf->label;
        try
        {
            buf->peBuffer = pe::Buffer::Create(size ? size : 1, vkUsage, vmaFlags, peName);
        }
        catch (...)
        {
            buf->peBuffer = nullptr;
        }

        if (!buf->peBuffer)
        {
            std::string msg = "wgpuDeviceCreateBuffer: backing allocation failed";
            device->reportError(WGPUErrorType_OutOfMemory, pwgpu::ToStringView(msg));
            buf->invalid = true;
            buf->mapState = mappedAtCreation ? WGPUBufferMapState_Mapped : WGPUBufferMapState_Unmapped;
            return buf;
        }

        if (needsHostAccess)
            buf->peBuffer->Map();

        if (mappedAtCreation)
        {
            buf->mapState = WGPUBufferMapState_Mapped;
            buf->mappedOffset = 0;
            buf->mappedSize = size;
            buf->mappedMode = WGPUMapMode_Write;
        }

        return buf;
    }

    WGPUTexture wgpuDeviceCreateTexture(WGPUDevice device, WGPUTextureDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateTexture", true))
            return nullptr;
        auto *tex = new WGPUTextureImpl();
        tex->format = descriptor->format;
        tex->usage = static_cast<WGPUTextureUsage>(descriptor->usage);
        tex->dimension = descriptor->dimension;
        tex->size = descriptor->size;
        tex->mipLevelCount = descriptor->mipLevelCount;
        tex->sampleCount = descriptor->sampleCount;
        if (descriptor->label.data)
            tex->label = pwgpu::ToString(descriptor->label);

        WGPUTextureViewDimension resolved = WGPUTextureViewDimension_Undefined;
        if (auto *ext = pwgpu::FindChained<WGPUTextureBindingViewDimension>(
                descriptor->nextInChain, WGPUSType_TextureBindingViewDimension))
        {
            resolved = ext->textureBindingViewDimension;
        }
        if (resolved == WGPUTextureViewDimension_Undefined)
        {
            switch (descriptor->dimension)
            {
            case WGPUTextureDimension_1D:
                resolved = WGPUTextureViewDimension_1D;
                break;
            case WGPUTextureDimension_3D:
                resolved = WGPUTextureViewDimension_3D;
                break;
            case WGPUTextureDimension_2D:
            default:
                resolved = (descriptor->size.depthOrArrayLayers > 1)
                               ? WGPUTextureViewDimension_2DArray
                               : WGPUTextureViewDimension_2D;
                break;
            }
        }
        tex->textureBindingViewDimension = resolved;
        return tex;
    }

    WGPUSampler wgpuDeviceCreateSampler(WGPUDevice device, WGPUSamplerDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateSampler", false))
            return nullptr;
        auto *smp = new WGPUSamplerImpl();
        if (descriptor && descriptor->label.data)
            smp->label = pwgpu::ToString(descriptor->label);
        return smp;
    }

    WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice device, WGPUBindGroupLayoutDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateBindGroupLayout", true))
            return nullptr;
        auto *bgl = new WGPUBindGroupLayoutImpl();
        if (descriptor->label.data)
            bgl->label = pwgpu::ToString(descriptor->label);
        return bgl;
    }

    WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, WGPUBindGroupDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateBindGroup", true))
            return nullptr;
        auto *bg = new WGPUBindGroupImpl();
        if (descriptor->label.data)
            bg->label = pwgpu::ToString(descriptor->label);
        if (descriptor->layout)
        {
            bg->layout = descriptor->layout;
            wgpuBindGroupLayoutAddRef(bg->layout);
        }
        return bg;
    }

    WGPUPipelineLayout wgpuDeviceCreatePipelineLayout(WGPUDevice device, WGPUPipelineLayoutDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreatePipelineLayout", true))
            return nullptr;
        auto *pl = new WGPUPipelineLayoutImpl();
        if (descriptor->label.data)
            pl->label = pwgpu::ToString(descriptor->label);
        return pl;
    }

    WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, WGPUShaderModuleDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateShaderModule", true))
            return nullptr;
        auto *sm = new WGPUShaderModuleImpl();
        if (descriptor->label.data)
            sm->label = pwgpu::ToString(descriptor->label);
        return sm;
    }

    WGPURenderPipeline wgpuDeviceCreateRenderPipeline(WGPUDevice device, WGPURenderPipelineDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateRenderPipeline", true))
            return nullptr;
        auto *rp = new WGPURenderPipelineImpl();
        if (descriptor->label.data)
            rp->label = pwgpu::ToString(descriptor->label);
        return rp;
    }

    WGPUFuture wgpuDeviceCreateRenderPipelineAsync(WGPUDevice device,
                                                   WGPURenderPipelineDescriptor const *descriptor,
                                                   WGPUCreateRenderPipelineAsyncCallbackInfo callbackInfo)
    {
        WGPURenderPipeline rp = wgpuDeviceCreateRenderPipeline(device, descriptor);
        if (callbackInfo.callback)
            callbackInfo.callback(rp ? WGPUCreatePipelineAsyncStatus_Success : WGPUCreatePipelineAsyncStatus_ValidationError,
                                  rp, {nullptr, 0}, callbackInfo.userdata1, callbackInfo.userdata2);
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    WGPUComputePipeline wgpuDeviceCreateComputePipeline(WGPUDevice device, WGPUComputePipelineDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateComputePipeline", true))
            return nullptr;
        auto *cp = new WGPUComputePipelineImpl();
        if (descriptor->label.data)
            cp->label = pwgpu::ToString(descriptor->label);
        return cp;
    }

    WGPUFuture wgpuDeviceCreateComputePipelineAsync(WGPUDevice device,
                                                    WGPUComputePipelineDescriptor const *descriptor,
                                                    WGPUCreateComputePipelineAsyncCallbackInfo callbackInfo)
    {
        WGPUComputePipeline cp = wgpuDeviceCreateComputePipeline(device, descriptor);
        if (callbackInfo.callback)
            callbackInfo.callback(cp ? WGPUCreatePipelineAsyncStatus_Success : WGPUCreatePipelineAsyncStatus_ValidationError,
                                  cp, {nullptr, 0}, callbackInfo.userdata1, callbackInfo.userdata2);
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, WGPUCommandEncoderDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateCommandEncoder", false))
            return nullptr;
        auto *enc = new WGPUCommandEncoderImpl();
        if (descriptor && descriptor->label.data)
            enc->label = pwgpu::ToString(descriptor->label);
        return enc;
    }

    WGPURenderBundleEncoder wgpuDeviceCreateRenderBundleEncoder(WGPUDevice device, WGPURenderBundleEncoderDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateRenderBundleEncoder", true))
            return nullptr;
        auto *rbe = new WGPURenderBundleEncoderImpl();
        if (descriptor->label.data)
            rbe->label = pwgpu::ToString(descriptor->label);
        rbe->sampleCount = descriptor->sampleCount;
        rbe->depthStencilFormat = descriptor->depthStencilFormat;
        for (size_t i = 0; i < descriptor->colorFormatCount; ++i)
            rbe->colorFormats.push_back(descriptor->colorFormats[i]);
        return rbe;
    }

    WGPUQuerySet wgpuDeviceCreateQuerySet(WGPUDevice device, WGPUQuerySetDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateQuerySet", true))
            return nullptr;
        auto *qs = new WGPUQuerySetImpl();
        qs->type = descriptor->type;
        qs->count = descriptor->count;
        if (descriptor->label.data)
            qs->label = pwgpu::ToString(descriptor->label);
        return qs;
    }

} // extern "C"
