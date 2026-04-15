#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "API/Semaphore.h"
#include "API/Queue.h"
#include "FormatMap.h"
#include "Sampler.h"
#include "BindGroup.h"
#include "PipelineLayout.h"
#include "ShaderModule.h"
#include "RenderPipeline.h"
#include "ComputePipeline.h"
#include "CommandEncoder.h"
#include "Instance.h"
#include "API/Queue.h"
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
    bool DeviceHasFeature(WGPUDeviceImpl *device, WGPUFeatureName feature)
    {
        for (auto f : device->features)
        {
            if (f == feature)
                return true;
        }
        return false;
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

extern void TeardownTextureGpuResources(WGPUTextureImpl *texture);

void WGPUDeviceImpl::ReclaimCompletedTextureDeletions()
{
    if (!queue)
        return;
    pe::Semaphore *sem = queue->GetSemaphore();
    const uint64_t completed = sem ? sem->GetValue() : 0;

    std::lock_guard<std::mutex> lock(pendingTextureDeletionsMutex);
    auto it = pendingTextureDeletions.begin();
    while (it != pendingTextureDeletions.end())
    {
        if (it->serial <= completed)
        {
            TeardownTextureGpuResources(it->tex);
            it = pendingTextureDeletions.erase(it);
        }
        else
        {
            ++it;
        }
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
                if (device->queue->peQueue)
                {
                    device->queue->peQueue->WaitIdle();
                    device->queue->RecyclePendingSubmits();
                }
                device->ReclaimCompletedTextureDeletions();
                device->queue->device = nullptr;
                wgpuQueueRelease(device->queue);
                device->queue = nullptr;
            }
            WGPUInstance inst = device->instance;
            delete device;
            if (inst)
                wgpuInstanceRelease(inst);
        }
    }

    void wgpuDeviceDestroy(WGPUDevice device)
    {
        if (!device || device->destroyed)
            return;
        device->destroyed = true;

        if (device->queue && device->queue->peQueue)
        {
            device->queue->peQueue->WaitIdle();
            device->queue->RecyclePendingSubmits();
        }

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
        WGPUInstanceImpl *inst = device ? device->instance : nullptr;

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
            if (!inst || !callbackInfo.callback)
                return inst ? inst->futures.NextId() : WGPUFuture{0};
            auto cb = callbackInfo.callback;
            auto u1 = callbackInfo.userdata1;
            auto u2 = callbackInfo.userdata2;
            return inst->futures.TrackEvent(callbackInfo.mode,
                                            [cb, u1, u2]()
                                            {
                                                cb(WGPUPopErrorScopeStatus_Error, WGPUErrorType_NoError, {nullptr, 0}, u1, u2);
                                            });
        }

        if (!inst || !callbackInfo.callback)
            return inst ? inst->futures.NextId() : WGPUFuture{0};
        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        WGPUErrorType errType = scope.hasCapturedError ? scope.capturedErrorType : WGPUErrorType_NoError;
        std::string errMsg = scope.capturedErrorMessage;
        return inst->futures.TrackEvent(callbackInfo.mode,
                                        [cb, u1, u2, errType, errMsg]()
                                        {
                                            WGPUStringView sv{errMsg.c_str(), errMsg.size()};
                                            cb(WGPUPopErrorScopeStatus_Success, errType, sv, u1, u2);
                                        });
    }

    WGPUFuture wgpuDeviceGetLostFuture(WGPUDevice device)
    {
        if (!device || !device->instance)
            return WGPUFuture{0};
        // TODO: track as incomplete future, complete on device loss.
        return device->instance->futures.NextId();
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
            bad->internalState = BufferInternalState::Unavailable;
            bad->mapState = mappedAtCreation ? WGPUBufferMapState_Mapped : WGPUBufferMapState_Unmapped;
            if (mappedAtCreation && size > 0)
            {
                bad->shadowData.assign(static_cast<size_t>(size), 0);
                bad->mappedOffset = 0;
                bad->mappedSize = size;
                bad->mappedMode = WGPUMapMode_Write;
            }
            if (descriptor->label.data)
                bad->label = pwgpu::ToString(descriptor->label);
            return bad;
        };

        if (usage == WGPUBufferUsage_None)
            return reportValidation("descriptor.usage must not be 0");

        constexpr uint64_t kValidBufferUsageMask = 0x03FF;
        if (usage & ~kValidBufferUsageMask)
            return reportValidation("descriptor.usage contains invalid/reserved flags");

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
            buf->internalState = BufferInternalState::Unavailable;
            buf->mapState = mappedAtCreation ? WGPUBufferMapState_Mapped : WGPUBufferMapState_Unmapped;
            return buf;
        }

        if (needsHostAccess)
            buf->peBuffer->Map();

        if (buf->peBuffer->Data() && needsHostAccess)
        {
            std::memset(buf->peBuffer->Data(), 0, static_cast<size_t>(size));
            buf->peBuffer->Flush();
        }

        if (mappedAtCreation)
        {
            buf->internalState = BufferInternalState::Unavailable;
            buf->mapState = WGPUBufferMapState_Mapped;
            buf->mappedOffset = 0;
            buf->mappedSize = size;
            buf->mappedMode = WGPUMapMode_Write;
        }
        else
        {
            buf->internalState = BufferInternalState::Available;
        }

        return buf;
    }

    WGPUTexture wgpuDeviceCreateTexture(WGPUDevice device, WGPUTextureDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateTexture", true))
            return nullptr;

        const WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(descriptor->usage);
        const WGPUTextureDimension dim = descriptor->dimension;
        const WGPUTextureFormat fmt = descriptor->format;
        const uint32_t w = descriptor->size.width;
        const uint32_t h = descriptor->size.height;
        const uint32_t d = descriptor->size.depthOrArrayLayers;
        const uint32_t mips = descriptor->mipLevelCount;
        const uint32_t samples = descriptor->sampleCount;

        auto makeInvalid = [&](const char *what) -> WGPUTexture
        {
            std::string msg = std::string("wgpuDeviceCreateTexture: ") + what;
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
            auto *bad = new WGPUTextureImpl();
            bad->device = device;
            wgpuDeviceAddRef(device);
            bad->format = fmt;
            bad->usage = usage;
            bad->dimension = dim;
            bad->size = descriptor->size;
            bad->mipLevelCount = mips;
            bad->sampleCount = samples;
            bad->invalid = true;
            if (descriptor->label.data)
                bad->label = pwgpu::ToString(descriptor->label);
            return bad;
        };

        if (usage == WGPUTextureUsage_None)
            return makeInvalid("usage must not be 0");
        if (w == 0 || h == 0 || d == 0)
            return makeInvalid("size width/height/depthOrArrayLayers must be > 0");
        if (mips == 0)
            return makeInvalid("mipLevelCount must be > 0");
        if (samples != 1 && samples != 4)
            return makeInvalid("sampleCount must be 1 or 4");

        const WGPULimits &lim = device->limits;

        if (dim == WGPUTextureDimension_1D)
        {
            if (w > lim.maxTextureDimension1D)
                return makeInvalid("1D texture width exceeds maxTextureDimension1D");
            if (h != 1)
                return makeInvalid("1D texture height must be 1");
            if (d != 1)
                return makeInvalid("1D texture depthOrArrayLayers must be 1");
            if (samples != 1)
                return makeInvalid("1D texture sampleCount must be 1");
            if (pwgpu::IsCompressedFormat(fmt) || pwgpu::IsDepthStencilFormat(fmt))
                return makeInvalid("1D texture cannot use compressed or depth/stencil format");
        }
        else if (dim == WGPUTextureDimension_2D)
        {
            if (w > lim.maxTextureDimension2D)
                return makeInvalid("2D texture width exceeds maxTextureDimension2D");
            if (h > lim.maxTextureDimension2D)
                return makeInvalid("2D texture height exceeds maxTextureDimension2D");
            if (d > lim.maxTextureArrayLayers)
                return makeInvalid("2D texture depthOrArrayLayers exceeds maxTextureArrayLayers");
        }
        else if (dim == WGPUTextureDimension_3D)
        {
            if (w > lim.maxTextureDimension3D)
                return makeInvalid("3D texture width exceeds maxTextureDimension3D");
            if (h > lim.maxTextureDimension3D)
                return makeInvalid("3D texture height exceeds maxTextureDimension3D");
            if (d > lim.maxTextureDimension3D)
                return makeInvalid("3D texture depthOrArrayLayers exceeds maxTextureDimension3D");
            if (samples != 1)
                return makeInvalid("3D texture sampleCount must be 1");
            if (!pwgpu::Supports3DTexture(fmt))
                return makeInvalid("format does not support 3D textures");
        }

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(fmt, blockW, blockH);
        if (w % blockW != 0)
            return makeInvalid("width must be a multiple of texel block width");
        if (h % blockH != 0)
            return makeInvalid("height must be a multiple of texel block height");

        if (samples > 1)
        {
            if (mips != 1)
                return makeInvalid("multisampled texture mipLevelCount must be 1");
            if (d != 1)
                return makeInvalid("multisampled texture depthOrArrayLayers must be 1");
            if (usage & WGPUTextureUsage_StorageBinding)
                return makeInvalid("multisampled texture cannot include STORAGE_BINDING");
            if (!(usage & WGPUTextureUsage_RenderAttachment))
                return makeInvalid("multisampled texture must include RENDER_ATTACHMENT");
            if (!pwgpu::SupportsMultisampling(fmt))
                return makeInvalid("format does not support multisampling");
        }

        uint32_t maxMips = pwgpu::MaxMipLevelCount(dim, w, h, d);
        if (mips > maxMips)
            return makeInvalid("mipLevelCount exceeds maximum for this size/dimension");

        if (usage & WGPUTextureUsage_RenderAttachment)
        {
            if (!pwgpu::IsRenderableFormat(fmt))
                return makeInvalid("RENDER_ATTACHMENT requires a renderable format");
            if (dim != WGPUTextureDimension_2D && dim != WGPUTextureDimension_3D)
                return makeInvalid("RENDER_ATTACHMENT requires dimension 2d or 3d");
        }

        if (usage & WGPUTextureUsage_StorageBinding)
        {
            if (!pwgpu::SupportsStorageBinding(fmt))
                return makeInvalid("STORAGE_BINDING requires a storage-capable format");
        }

        if (usage & WGPUTextureUsage_TransientAttachment)
        {
            if (usage != (WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment))
                return makeInvalid("TRANSIENT_ATTACHMENT must combine only with RENDER_ATTACHMENT");
            if (dim != WGPUTextureDimension_2D)
                return makeInvalid("TRANSIENT_ATTACHMENT requires dimension 2d");
            if (mips != 1)
                return makeInvalid("TRANSIENT_ATTACHMENT requires mipLevelCount 1");
            if (d != 1)
                return makeInvalid("TRANSIENT_ATTACHMENT requires depthOrArrayLayers 1");
        }

        if (pwgpu::IsBCFormat(fmt) && !DeviceHasFeature(device, WGPUFeatureName_TextureCompressionBC))
            return makeInvalid("BC format requires TextureCompressionBC feature");
        if (pwgpu::IsETC2Format(fmt) && !DeviceHasFeature(device, WGPUFeatureName_TextureCompressionETC2))
            return makeInvalid("ETC2/EAC format requires TextureCompressionETC2 feature");
        if (pwgpu::IsASTCFormat(fmt) && !DeviceHasFeature(device, WGPUFeatureName_TextureCompressionASTC))
            return makeInvalid("ASTC format requires TextureCompressionASTC feature");

        if ((usage & WGPUTextureUsage_StorageBinding) && fmt == WGPUTextureFormat_BGRA8Unorm &&
            !DeviceHasFeature(device, WGPUFeatureName_BGRA8UnormStorage))
            return makeInvalid("BGRA8Unorm + STORAGE_BINDING requires BGRA8UnormStorage feature");

        if (descriptor->viewFormatCount > 0 && !descriptor->viewFormats)
            return makeInvalid("viewFormatCount > 0 but viewFormats is null");

        for (size_t i = 0; i < descriptor->viewFormatCount; ++i)
        {
            WGPUTextureFormat vf = descriptor->viewFormats[i];
            if (pwgpu::IsBCFormat(vf) && !DeviceHasFeature(device, WGPUFeatureName_TextureCompressionBC))
                return makeInvalid("viewFormats BC format requires TextureCompressionBC feature");
            if (pwgpu::IsETC2Format(vf) && !DeviceHasFeature(device, WGPUFeatureName_TextureCompressionETC2))
                return makeInvalid("viewFormats ETC2/EAC format requires TextureCompressionETC2 feature");
            if (pwgpu::IsASTCFormat(vf) && !DeviceHasFeature(device, WGPUFeatureName_TextureCompressionASTC))
                return makeInvalid("viewFormats ASTC format requires TextureCompressionASTC feature");
            if (!pwgpu::AreViewFormatCompatible(fmt, vf))
                return makeInvalid("viewFormats entry is not compatible with texture format");
        }

        auto *tex = new WGPUTextureImpl();
        tex->device = device;
        wgpuDeviceAddRef(device);
        tex->format = fmt;
        tex->usage = usage;
        tex->dimension = dim;
        tex->size = descriptor->size;
        tex->mipLevelCount = mips;
        tex->sampleCount = samples;
        if (descriptor->label.data)
            tex->label = pwgpu::ToString(descriptor->label);

        for (size_t i = 0; i < descriptor->viewFormatCount; ++i)
            tex->viewFormats.push_back(descriptor->viewFormats[i]);

        WGPUTextureViewDimension tbvd = WGPUTextureViewDimension_Undefined;
        if (auto *ext = pwgpu::FindChained<WGPUTextureBindingViewDimension>(
                descriptor->nextInChain, WGPUSType_TextureBindingViewDimension))
        {
            tbvd = ext->textureBindingViewDimension;
        }
        if (tbvd == WGPUTextureViewDimension_Undefined)
        {
            switch (dim)
            {
            case WGPUTextureDimension_1D:
                tbvd = WGPUTextureViewDimension_1D;
                break;
            case WGPUTextureDimension_3D:
                tbvd = WGPUTextureViewDimension_3D;
                break;
            case WGPUTextureDimension_2D:
            default:
                tbvd = (d > 1) ? WGPUTextureViewDimension_2DArray
                               : WGPUTextureViewDimension_2D;
                break;
            }
        }
        tex->textureBindingViewDimension = tbvd;

        VkFormat vkFmt = pwgpu::ToVkFormat(fmt);
        if (fmt == WGPUTextureFormat_Depth24Plus)
            vkFmt = device->resolvedDepth24Plus;
        else if (fmt == WGPUTextureFormat_Depth24PlusStencil8)
            vkFmt = device->resolvedDepth24PlusStencil8;

        vk::ImageCreateInfo ci = pe::Image::CreateInfoInit();
        ci.format = static_cast<vk::Format>(vkFmt);
        ci.extent = vk::Extent3D{w, h, (dim == WGPUTextureDimension_3D) ? d : 1u};
        ci.mipLevels = mips;
        ci.arrayLayers = (dim == WGPUTextureDimension_2D) ? d : 1u;
        ci.samples = static_cast<vk::SampleCountFlagBits>(samples);

        switch (dim)
        {
        case WGPUTextureDimension_1D:
            ci.imageType = vk::ImageType::e1D;
            break;
        case WGPUTextureDimension_3D:
            ci.imageType = vk::ImageType::e3D;
            break;
        default:
            ci.imageType = vk::ImageType::e2D;
            break;
        }

        vk::ImageUsageFlags vkUsage{};
        if (usage & WGPUTextureUsage_CopySrc)
            vkUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        if (usage & WGPUTextureUsage_CopyDst)
            vkUsage |= vk::ImageUsageFlagBits::eTransferDst;
        if (usage & WGPUTextureUsage_TextureBinding)
            vkUsage |= vk::ImageUsageFlagBits::eSampled;
        if (usage & WGPUTextureUsage_StorageBinding)
            vkUsage |= vk::ImageUsageFlagBits::eStorage;
        if (usage & WGPUTextureUsage_RenderAttachment)
        {
            if (pwgpu::IsDepthStencilFormat(fmt))
                vkUsage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
            else
                vkUsage |= vk::ImageUsageFlagBits::eColorAttachment;
        }
        if (usage & WGPUTextureUsage_TransientAttachment)
            vkUsage |= vk::ImageUsageFlagBits::eTransientAttachment;

        if (!vkUsage)
            vkUsage = vk::ImageUsageFlagBits::eTransferSrc;
        ci.usage = vkUsage;

        if (dim == WGPUTextureDimension_2D && w == h && d >= 6 && (d % 6 == 0))
            ci.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
        if (dim == WGPUTextureDimension_3D && (usage & WGPUTextureUsage_RenderAttachment))
            ci.flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;
        if (descriptor->viewFormatCount > 0 && descriptor->viewFormats)
            ci.flags |= vk::ImageCreateFlagBits::eMutableFormat;

        const std::string peName = tex->label.empty() ? std::string("wgpu_texture") : tex->label;
        std::string createError;
        try
        {
            tex->image = pe::Image::Create(ci, peName);
        }
        catch (const std::exception &e)
        {
            tex->image = nullptr;
            createError = e.what();
        }
        catch (...)
        {
            tex->image = nullptr;
            createError = "unknown exception";
        }

        if (!tex->image)
        {
            std::string msg = "wgpuDeviceCreateTexture: backing allocation failed";
            if (!createError.empty())
                msg += " (" + createError + ")";
            device->reportError(WGPUErrorType_OutOfMemory, pwgpu::ToStringView(msg));
            tex->invalid = true;
            return tex;
        }

        return tex;
    }

    WGPUSampler wgpuDeviceCreateSampler(WGPUDevice device, WGPUSamplerDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateSampler", false))
            return nullptr;

        auto makeInvalid = [&](const char *what) -> WGPUSampler
        {
            std::string msg = std::string("wgpuDeviceCreateSampler: ") + what;
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
            auto *bad = new WGPUSamplerImpl();
            bad->invalid = true;
            wgpuDeviceAddRef(device);
            bad->device = device;
            if (descriptor && descriptor->label.data)
                bad->label = pwgpu::ToString(descriptor->label);
            return bad;
        };

        WGPUAddressMode addrU = WGPUAddressMode_ClampToEdge;
        WGPUAddressMode addrV = WGPUAddressMode_ClampToEdge;
        WGPUAddressMode addrW = WGPUAddressMode_ClampToEdge;
        WGPUFilterMode magF = WGPUFilterMode_Nearest;
        WGPUFilterMode minF = WGPUFilterMode_Nearest;
        WGPUMipmapFilterMode mipF = WGPUMipmapFilterMode_Nearest;
        float lodMin = 0.0f;
        float lodMax = 32.0f;
        WGPUCompareFunction cmp = WGPUCompareFunction_Undefined;
        uint16_t maxAniso = 1;

        if (descriptor)
        {
            addrU = (descriptor->addressModeU != WGPUAddressMode_Undefined)
                        ? descriptor->addressModeU
                        : WGPUAddressMode_ClampToEdge;
            addrV = (descriptor->addressModeV != WGPUAddressMode_Undefined)
                        ? descriptor->addressModeV
                        : WGPUAddressMode_ClampToEdge;
            addrW = (descriptor->addressModeW != WGPUAddressMode_Undefined)
                        ? descriptor->addressModeW
                        : WGPUAddressMode_ClampToEdge;
            magF = (descriptor->magFilter != WGPUFilterMode_Undefined)
                       ? descriptor->magFilter
                       : WGPUFilterMode_Nearest;
            minF = (descriptor->minFilter != WGPUFilterMode_Undefined)
                       ? descriptor->minFilter
                       : WGPUFilterMode_Nearest;
            mipF = (descriptor->mipmapFilter != WGPUMipmapFilterMode_Undefined)
                       ? descriptor->mipmapFilter
                       : WGPUMipmapFilterMode_Nearest;
            lodMin = descriptor->lodMinClamp;
            lodMax = descriptor->lodMaxClamp;
            cmp = descriptor->compare;
            maxAniso = descriptor->maxAnisotropy;
        }

        if (std::isnan(lodMin) || std::isinf(lodMin))
            return makeInvalid("lodMinClamp is non-finite");
        if (std::isnan(lodMax) || std::isinf(lodMax))
            return makeInvalid("lodMaxClamp is non-finite");
        if (lodMin < 0.0f)
            return makeInvalid("lodMinClamp must be >= 0");
        if (lodMax < lodMin)
            return makeInvalid("lodMaxClamp must be >= lodMinClamp");
        if (maxAniso < 1)
            return makeInvalid("maxAnisotropy must be >= 1");
        if (maxAniso > 1)
        {
            if (magF != WGPUFilterMode_Linear)
                return makeInvalid("maxAnisotropy > 1 requires magFilter = linear");
            if (minF != WGPUFilterMode_Linear)
                return makeInvalid("maxAnisotropy > 1 requires minFilter = linear");
            if (mipF != WGPUMipmapFilterMode_Linear)
                return makeInvalid("maxAnisotropy > 1 requires mipmapFilter = linear");
        }

        bool isComparison = (cmp != WGPUCompareFunction_Undefined);
        bool isFiltering = (magF == WGPUFilterMode_Linear ||
                            minF == WGPUFilterMode_Linear ||
                            mipF == WGPUMipmapFilterMode_Linear);

        auto *smp = new WGPUSamplerImpl();
        wgpuDeviceAddRef(device);
        smp->device = device;
        smp->addressModeU = addrU;
        smp->addressModeV = addrV;
        smp->addressModeW = addrW;
        smp->magFilter = magF;
        smp->minFilter = minF;
        smp->mipmapFilter = mipF;
        smp->lodMinClamp = lodMin;
        smp->lodMaxClamp = lodMax;
        smp->compare = cmp;
        smp->maxAnisotropy = maxAniso;
        smp->isComparison = isComparison;
        smp->isFiltering = isFiltering;
        if (descriptor && descriptor->label.data)
            smp->label = pwgpu::ToString(descriptor->label);

        auto toVkAddressMode = [](WGPUAddressMode m) -> vk::SamplerAddressMode
        {
            switch (m)
            {
            case WGPUAddressMode_Repeat:
                return vk::SamplerAddressMode::eRepeat;
            case WGPUAddressMode_MirrorRepeat:
                return vk::SamplerAddressMode::eMirroredRepeat;
            case WGPUAddressMode_ClampToEdge:
            default:
                return vk::SamplerAddressMode::eClampToEdge;
            }
        };

        auto toVkFilter = [](WGPUFilterMode f) -> vk::Filter
        {
            return (f == WGPUFilterMode_Linear) ? vk::Filter::eLinear : vk::Filter::eNearest;
        };

        auto toVkMipMode = [](WGPUMipmapFilterMode f) -> vk::SamplerMipmapMode
        {
            return (f == WGPUMipmapFilterMode_Linear) ? vk::SamplerMipmapMode::eLinear
                                                      : vk::SamplerMipmapMode::eNearest;
        };

        auto toVkCompareOp = [](WGPUCompareFunction c) -> vk::CompareOp
        {
            switch (c)
            {
            case WGPUCompareFunction_Never:
                return vk::CompareOp::eNever;
            case WGPUCompareFunction_Less:
                return vk::CompareOp::eLess;
            case WGPUCompareFunction_Equal:
                return vk::CompareOp::eEqual;
            case WGPUCompareFunction_LessEqual:
                return vk::CompareOp::eLessOrEqual;
            case WGPUCompareFunction_Greater:
                return vk::CompareOp::eGreater;
            case WGPUCompareFunction_NotEqual:
                return vk::CompareOp::eNotEqual;
            case WGPUCompareFunction_GreaterEqual:
                return vk::CompareOp::eGreaterOrEqual;
            case WGPUCompareFunction_Always:
                return vk::CompareOp::eAlways;
            default:
                return vk::CompareOp::eNever;
            }
        };

        if (device->rhi && device->rhi->GetDevice())
        {
            vk::SamplerCreateInfo sci{};
            sci.magFilter = toVkFilter(magF);
            sci.minFilter = toVkFilter(minF);
            sci.mipmapMode = toVkMipMode(mipF);
            sci.addressModeU = toVkAddressMode(addrU);
            sci.addressModeV = toVkAddressMode(addrV);
            sci.addressModeW = toVkAddressMode(addrW);
            sci.mipLodBias = 0.0f;
            sci.minLod = lodMin;
            sci.maxLod = lodMax;

            if (maxAniso > 1)
            {
                sci.anisotropyEnable = VK_TRUE;
                float clampedAniso = static_cast<float>(maxAniso);
                VkPhysicalDeviceProperties gpuProps{};
                vkGetPhysicalDeviceProperties(device->rhi->GetGpu(), &gpuProps);
                float hwMax = gpuProps.limits.maxSamplerAnisotropy;
                if (clampedAniso > hwMax)
                    clampedAniso = hwMax;
                sci.maxAnisotropy = clampedAniso;
            }
            else
            {
                sci.anisotropyEnable = VK_FALSE;
                sci.maxAnisotropy = 1.0f;
            }

            if (isComparison)
            {
                sci.compareEnable = VK_TRUE;
                sci.compareOp = toVkCompareOp(cmp);
            }
            else
            {
                sci.compareEnable = VK_FALSE;
                sci.compareOp = vk::CompareOp::eNever;
            }

            sci.borderColor = vk::BorderColor::eFloatTransparentBlack;
            sci.unnormalizedCoordinates = VK_FALSE;

            const std::string samplerName = smp->label.empty() ? "wgpu_sampler" : smp->label;
            try
            {
                smp->sampler = pe::Sampler::Create(sci, samplerName);
            }
            catch (...)
            {
                smp->sampler = nullptr;
            }

            if (!smp->sampler)
            {
                device->reportError(WGPUErrorType_Internal,
                                    pwgpu::ToStringView("wgpuDeviceCreateSampler: native sampler creation failed"));
            }
        }

        return smp;
    }

    WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice device, WGPUBindGroupLayoutDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateBindGroupLayout", true))
            return nullptr;

        auto *bgl = new WGPUBindGroupLayoutImpl();
        wgpuDeviceAddRef(device);
        bgl->device = device;
        if (descriptor->label.data)
            bgl->label = pwgpu::ToString(descriptor->label);

        const WGPULimits &limits = device->limits;

        auto makeInvalid = [&](const char *msg) -> WGPUBindGroupLayout
        {
            std::string what = std::string("PhasmaWebGPU: createBindGroupLayout: ") + msg;
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(what));
            bgl->invalid = true;
            return bgl;
        };

        auto bufferProvided = [](const WGPUBufferBindingLayout &b)
        { return b.type != WGPUBufferBindingType_BindingNotUsed; };
        auto samplerProvided = [](const WGPUSamplerBindingLayout &s)
        { return s.type != WGPUSamplerBindingType_BindingNotUsed; };
        auto textureProvided = [](const WGPUTextureBindingLayout &t)
        { return t.sampleType != WGPUTextureSampleType_BindingNotUsed; };
        auto storageTexProvided = [](const WGPUStorageTextureBindingLayout &st)
        { return st.access != WGPUStorageTextureAccess_BindingNotUsed; };
        auto externalTexProvided = [](const WGPUBindGroupLayoutEntry &e)
        {
            const WGPUChainedStruct *chain = e.nextInChain;
            while (chain)
            {
                if (chain->sType == WGPUSType_ExternalTextureBindingLayout)
                    return true;
                chain = chain->next;
            }
            return false;
        };

        struct StageCounters
        {
            uint32_t uniformBuffers = 0;
            uint32_t storageBuffers = 0;
            uint32_t samplers = 0;
            uint32_t sampledTextures = 0;
            uint32_t storageTextures = 0;
        };
        StageCounters vertexCounters{}, fragmentCounters{}, computeCounters{};
        uint32_t dynamicUniformBuffers = 0;
        uint32_t dynamicStorageBuffers = 0;

        std::unordered_set<uint32_t> seenBindings;

        bgl->entries.reserve(descriptor->entryCount);

        for (size_t i = 0; i < descriptor->entryCount; ++i)
        {
            const WGPUBindGroupLayoutEntry &src = descriptor->entries[i];

            if (!seenBindings.insert(src.binding).second)
                return makeInvalid("duplicate binding index");
            if (src.binding >= limits.maxBindingsPerBindGroup)
                return makeInvalid("binding index >= maxBindingsPerBindGroup");

            const uint32_t validStages = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
            if (src.visibility & ~validStages)
                return makeInvalid("visibility contains invalid stage bits");

            bool hasBuf = bufferProvided(src.buffer);
            bool hasSamp = samplerProvided(src.sampler);
            bool hasTex = textureProvided(src.texture);
            bool hasStorTex = storageTexProvided(src.storageTexture);
            bool hasExtTex = externalTexProvided(src);

            int memberCount = (int)hasBuf + (int)hasSamp + (int)hasTex + (int)hasStorTex + (int)hasExtTex;
            if (memberCount != 1)
                return makeInvalid("exactly one of buffer/sampler/texture/storageTexture/externalTexture must be provided");

            WGPUBindGroupLayoutEntryResolved resolved{};
            resolved.binding = src.binding;
            resolved.visibility = src.visibility;

            if (hasBuf)
            {
                resolved.buffer = src.buffer;
                if (resolved.buffer.type == WGPUBufferBindingType_Undefined)
                    resolved.buffer.type = WGPUBufferBindingType_Uniform;

                if (src.visibility & WGPUShaderStage_Vertex)
                {
                    if (resolved.buffer.type != WGPUBufferBindingType_Uniform &&
                        resolved.buffer.type != WGPUBufferBindingType_ReadOnlyStorage)
                        return makeInvalid("VERTEX stage requires buffer type uniform or read-only-storage");
                }

                if (resolved.buffer.hasDynamicOffset)
                {
                    if (resolved.buffer.type == WGPUBufferBindingType_Uniform)
                        dynamicUniformBuffers++;
                    else
                        dynamicStorageBuffers++;
                }

                if (src.visibility & WGPUShaderStage_Vertex)
                {
                    if (resolved.buffer.type == WGPUBufferBindingType_Uniform)
                        vertexCounters.uniformBuffers++;
                    else
                        vertexCounters.storageBuffers++;
                }
                if (src.visibility & WGPUShaderStage_Fragment)
                {
                    if (resolved.buffer.type == WGPUBufferBindingType_Uniform)
                        fragmentCounters.uniformBuffers++;
                    else
                        fragmentCounters.storageBuffers++;
                }
                if (src.visibility & WGPUShaderStage_Compute)
                {
                    if (resolved.buffer.type == WGPUBufferBindingType_Uniform)
                        computeCounters.uniformBuffers++;
                    else
                        computeCounters.storageBuffers++;
                }
            }
            else if (hasSamp)
            {
                resolved.sampler = src.sampler;
                if (resolved.sampler.type == WGPUSamplerBindingType_Undefined)
                    resolved.sampler.type = WGPUSamplerBindingType_Filtering;

                if (src.visibility & WGPUShaderStage_Vertex)
                    vertexCounters.samplers++;
                if (src.visibility & WGPUShaderStage_Fragment)
                    fragmentCounters.samplers++;
                if (src.visibility & WGPUShaderStage_Compute)
                    computeCounters.samplers++;
            }
            else if (hasTex)
            {
                resolved.texture = src.texture;
                if (resolved.texture.sampleType == WGPUTextureSampleType_Undefined)
                    resolved.texture.sampleType = WGPUTextureSampleType_Float;
                if (resolved.texture.viewDimension == WGPUTextureViewDimension_Undefined)
                    resolved.texture.viewDimension = WGPUTextureViewDimension_2D;

                if (resolved.texture.multisampled)
                {
                    if (resolved.texture.viewDimension != WGPUTextureViewDimension_2D)
                        return makeInvalid("multisampled texture binding must have viewDimension = 2d");
                    if (resolved.texture.sampleType == WGPUTextureSampleType_Float)
                        return makeInvalid("multisampled texture binding must not have sampleType = float");
                }

                if (src.visibility & WGPUShaderStage_Vertex)
                    vertexCounters.sampledTextures++;
                if (src.visibility & WGPUShaderStage_Fragment)
                    fragmentCounters.sampledTextures++;
                if (src.visibility & WGPUShaderStage_Compute)
                    computeCounters.sampledTextures++;
            }
            else if (hasStorTex)
            {
                resolved.storageTexture = src.storageTexture;
                if (resolved.storageTexture.access == WGPUStorageTextureAccess_Undefined)
                    resolved.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                if (resolved.storageTexture.viewDimension == WGPUTextureViewDimension_Undefined)
                    resolved.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

                if (resolved.storageTexture.viewDimension == WGPUTextureViewDimension_Cube ||
                    resolved.storageTexture.viewDimension == WGPUTextureViewDimension_CubeArray)
                    return makeInvalid("storageTexture viewDimension must not be cube or cube-array");

                if (!pwgpu::SupportsStorageBinding(resolved.storageTexture.format))
                    return makeInvalid("storageTexture format does not support storage binding");

                if ((src.visibility & WGPUShaderStage_Vertex) &&
                    resolved.storageTexture.access != WGPUStorageTextureAccess_ReadOnly)
                    return makeInvalid("VERTEX stage requires storageTexture access = read-only");

                if (src.visibility & WGPUShaderStage_Vertex)
                    vertexCounters.storageTextures++;
                if (src.visibility & WGPUShaderStage_Fragment)
                    fragmentCounters.storageTextures++;
                if (src.visibility & WGPUShaderStage_Compute)
                    computeCounters.storageTextures++;
            }
            else if (hasExtTex)
            {
                resolved.hasExternalTexture = true;
                if (src.visibility & WGPUShaderStage_Vertex)
                {
                    vertexCounters.sampledTextures += 4;
                    vertexCounters.samplers++;
                    vertexCounters.uniformBuffers++;
                }
                if (src.visibility & WGPUShaderStage_Fragment)
                {
                    fragmentCounters.sampledTextures += 4;
                    fragmentCounters.samplers++;
                    fragmentCounters.uniformBuffers++;
                }
                if (src.visibility & WGPUShaderStage_Compute)
                {
                    computeCounters.sampledTextures += 4;
                    computeCounters.samplers++;
                    computeCounters.uniformBuffers++;
                }
            }

            bgl->entries.push_back(resolved);
        }

        if (vertexCounters.uniformBuffers > limits.maxUniformBuffersPerShaderStage)
            return makeInvalid("vertex stage exceeds maxUniformBuffersPerShaderStage");
        if (vertexCounters.storageBuffers > limits.maxStorageBuffersPerShaderStage)
            return makeInvalid("vertex stage exceeds maxStorageBuffersPerShaderStage");
        if (vertexCounters.samplers > limits.maxSamplersPerShaderStage)
            return makeInvalid("vertex stage exceeds maxSamplersPerShaderStage");
        if (vertexCounters.sampledTextures > limits.maxSampledTexturesPerShaderStage)
            return makeInvalid("vertex stage exceeds maxSampledTexturesPerShaderStage");
        if (vertexCounters.storageTextures > limits.maxStorageTexturesPerShaderStage)
            return makeInvalid("vertex stage exceeds maxStorageTexturesPerShaderStage");

        if (fragmentCounters.uniformBuffers > limits.maxUniformBuffersPerShaderStage)
            return makeInvalid("fragment stage exceeds maxUniformBuffersPerShaderStage");
        if (fragmentCounters.storageBuffers > limits.maxStorageBuffersPerShaderStage)
            return makeInvalid("fragment stage exceeds maxStorageBuffersPerShaderStage");
        if (fragmentCounters.samplers > limits.maxSamplersPerShaderStage)
            return makeInvalid("fragment stage exceeds maxSamplersPerShaderStage");
        if (fragmentCounters.sampledTextures > limits.maxSampledTexturesPerShaderStage)
            return makeInvalid("fragment stage exceeds maxSampledTexturesPerShaderStage");
        if (fragmentCounters.storageTextures > limits.maxStorageTexturesPerShaderStage)
            return makeInvalid("fragment stage exceeds maxStorageTexturesPerShaderStage");
        if (computeCounters.uniformBuffers > limits.maxUniformBuffersPerShaderStage)
            return makeInvalid("compute stage exceeds maxUniformBuffersPerShaderStage");
        if (computeCounters.storageBuffers > limits.maxStorageBuffersPerShaderStage)
            return makeInvalid("compute stage exceeds maxStorageBuffersPerShaderStage");
        if (computeCounters.samplers > limits.maxSamplersPerShaderStage)
            return makeInvalid("compute stage exceeds maxSamplersPerShaderStage");
        if (computeCounters.sampledTextures > limits.maxSampledTexturesPerShaderStage)
            return makeInvalid("compute stage exceeds maxSampledTexturesPerShaderStage");
        if (computeCounters.storageTextures > limits.maxStorageTexturesPerShaderStage)
            return makeInvalid("compute stage exceeds maxStorageTexturesPerShaderStage");

        if (dynamicUniformBuffers > limits.maxDynamicUniformBuffersPerPipelineLayout)
            return makeInvalid("exceeds maxDynamicUniformBuffersPerPipelineLayout");
        if (dynamicStorageBuffers > limits.maxDynamicStorageBuffersPerPipelineLayout)
            return makeInvalid("exceeds maxDynamicStorageBuffersPerPipelineLayout");

        bgl->dynamicOffsetCount = dynamicUniformBuffers + dynamicStorageBuffers;

        auto wgpuVisToVk = [](WGPUShaderStage vis) -> vk::ShaderStageFlags
        {
            vk::ShaderStageFlags f{};
            if (vis & WGPUShaderStage_Vertex)
                f |= vk::ShaderStageFlagBits::eVertex;
            if (vis & WGPUShaderStage_Fragment)
                f |= vk::ShaderStageFlagBits::eFragment;
            if (vis & WGPUShaderStage_Compute)
                f |= vk::ShaderStageFlagBits::eCompute;
            return f;
        };

        vk::ShaderStageFlags combinedStages{};
        std::vector<pe::DescriptorBindingInfo> vkBindings;
        vkBindings.reserve(bgl->entries.size());

        for (const auto &entry : bgl->entries)
        {
            pe::DescriptorBindingInfo info{};
            info.binding = entry.binding;
            info.count = 1;
            combinedStages |= wgpuVisToVk(entry.visibility);

            if (entry.buffer.type != WGPUBufferBindingType_BindingNotUsed)
            {
                switch (entry.buffer.type)
                {
                case WGPUBufferBindingType_Uniform:
                    info.type = entry.buffer.hasDynamicOffset
                                    ? vk::DescriptorType::eUniformBufferDynamic
                                    : vk::DescriptorType::eUniformBuffer;
                    break;
                case WGPUBufferBindingType_Storage:
                    info.type = entry.buffer.hasDynamicOffset
                                    ? vk::DescriptorType::eStorageBufferDynamic
                                    : vk::DescriptorType::eStorageBuffer;
                    break;
                case WGPUBufferBindingType_ReadOnlyStorage:
                    info.type = entry.buffer.hasDynamicOffset
                                    ? vk::DescriptorType::eStorageBufferDynamic
                                    : vk::DescriptorType::eStorageBuffer;
                    break;
                default:
                    break;
                }
            }
            else if (entry.sampler.type != WGPUSamplerBindingType_BindingNotUsed)
            {
                info.type = vk::DescriptorType::eSampler;
            }
            else if (entry.texture.sampleType != WGPUTextureSampleType_BindingNotUsed)
            {
                info.type = vk::DescriptorType::eSampledImage;
            }
            else if (entry.storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed)
            {
                info.type = vk::DescriptorType::eStorageImage;
            }
            else if (entry.hasExternalTexture)
            {
                info.type = vk::DescriptorType::eCombinedImageSampler;
            }

            vkBindings.push_back(info);
        }

        bgl->bindingInfos = vkBindings;
        bgl->stage = combinedStages;

        if (!vkBindings.empty())
        {
            bgl->layout = pe::DescriptorLayout::Create(
                vkBindings, combinedStages,
                bgl->label.empty() ? "wgpu_bgl" : bgl->label);
        }

        return bgl;
    }

    WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, WGPUBindGroupDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateBindGroup", true))
            return nullptr;

        auto *bg = new WGPUBindGroupImpl();
        wgpuDeviceAddRef(device);
        bg->device = device;
        if (descriptor->label.data)
            bg->label = pwgpu::ToString(descriptor->label);

        auto makeInvalid = [&](const char *msg) -> WGPUBindGroup
        {
            std::string what = std::string("PhasmaWebGPU: createBindGroup: ") + msg;
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(what));
            bg->invalid = true;
            return bg;
        };

        if (!descriptor->layout || descriptor->layout->invalid)
            return makeInvalid("layout is null or invalid");

        bg->layout = descriptor->layout;
        wgpuBindGroupLayoutAddRef(bg->layout);

        const auto &layoutEntries = descriptor->layout->entries;

        if (descriptor->entryCount != layoutEntries.size())
            return makeInvalid("entryCount does not match layout entry count");

        std::unordered_map<uint32_t, const WGPUBindGroupLayoutEntryResolved *> layoutMap;
        for (const auto &le : layoutEntries)
            layoutMap[le.binding] = &le;

        std::unordered_set<uint32_t> seenBindings;
        for (size_t i = 0; i < descriptor->entryCount; ++i)
        {
            const WGPUBindGroupEntry &entry = descriptor->entries[i];

            if (!seenBindings.insert(entry.binding).second)
                return makeInvalid("duplicate entry binding");

            auto it = layoutMap.find(entry.binding);
            if (it == layoutMap.end())
                return makeInvalid("entry binding has no corresponding layout entry");

            const WGPUBindGroupLayoutEntryResolved &le = *it->second;

            bool hasBuf = (entry.buffer != nullptr);
            bool hasSamp = (entry.sampler != nullptr);
            bool hasTexView = (entry.textureView != nullptr);

            if (le.buffer.type != WGPUBufferBindingType_BindingNotUsed)
            {

                if (!hasBuf)
                    return makeInvalid("layout expects buffer but entry has no buffer");
                WGPUBufferImpl *buf = entry.buffer;
                if (buf->invalid)
                    return makeInvalid("buffer resource is invalid");

                uint64_t offset = entry.offset;
                uint64_t size = entry.size;
                if (size == WGPU_WHOLE_SIZE)
                {
                    if (offset > buf->size)
                        return makeInvalid("buffer binding offset exceeds buffer size");
                    size = buf->size - offset;
                }

                if (size == 0)
                    return makeInvalid("buffer binding size is zero");
                if (offset > buf->size || size > buf->size - offset)
                    return makeInvalid("buffer binding range exceeds buffer size");

                if (le.buffer.minBindingSize > 0 && size < le.buffer.minBindingSize)
                    return makeInvalid("buffer binding size < layout minBindingSize");

                const WGPULimits &limits = device->limits;
                if (le.buffer.type == WGPUBufferBindingType_Uniform)
                {
                    if (!(buf->usage & WGPUBufferUsage_Uniform))
                        return makeInvalid("buffer missing UNIFORM usage for uniform binding");
                    if (size > limits.maxUniformBufferBindingSize)
                        return makeInvalid("buffer binding size exceeds maxUniformBufferBindingSize");
                    if (limits.minUniformBufferOffsetAlignment > 0 &&
                        (offset % limits.minUniformBufferOffsetAlignment) != 0)
                        return makeInvalid("buffer binding offset not aligned to minUniformBufferOffsetAlignment");
                }
                else // storage or read-only-storage
                {
                    if (!(buf->usage & WGPUBufferUsage_Storage))
                        return makeInvalid("buffer missing STORAGE usage for storage binding");
                    if (size > limits.maxStorageBufferBindingSize)
                        return makeInvalid("buffer binding size exceeds maxStorageBufferBindingSize");
                    if ((size % 4) != 0)
                        return makeInvalid("storage buffer binding size must be a multiple of 4");
                    if (limits.minStorageBufferOffsetAlignment > 0 &&
                        (offset % limits.minStorageBufferOffsetAlignment) != 0)
                        return makeInvalid("buffer binding offset not aligned to minStorageBufferOffsetAlignment");
                }
            }
            else if (le.sampler.type != WGPUSamplerBindingType_BindingNotUsed)
            {

                if (!hasSamp)
                    return makeInvalid("layout expects sampler but entry has no sampler");
                WGPUSamplerImpl *smp = entry.sampler;
                if (smp->invalid)
                    return makeInvalid("sampler resource is invalid");

                switch (le.sampler.type)
                {
                case WGPUSamplerBindingType_Filtering:
                    if (smp->isComparison)
                        return makeInvalid("filtering sampler binding requires non-comparison sampler");
                    break;
                case WGPUSamplerBindingType_NonFiltering:
                    if (smp->isFiltering || smp->isComparison)
                        return makeInvalid("non-filtering sampler binding requires non-filtering non-comparison sampler");
                    break;
                case WGPUSamplerBindingType_Comparison:
                    if (!smp->isComparison)
                        return makeInvalid("comparison sampler binding requires comparison sampler");
                    break;
                default:
                    break;
                }
            }
            else if (le.texture.sampleType != WGPUTextureSampleType_BindingNotUsed)
            {

                if (!hasTexView)
                    return makeInvalid("layout expects texture but entry has no textureView");
                WGPUTextureViewImpl *tv = entry.textureView;
                if (!tv->texture)
                    return makeInvalid("textureView has no backing texture");

                // viewDimension must match.
                if (tv->dimension != le.texture.viewDimension)
                    return makeInvalid("textureView dimension does not match layout");

                // Usage must include TEXTURE_BINDING.
                if (!(tv->usage & WGPUTextureUsage_TextureBinding))
                    return makeInvalid("textureView missing TEXTURE_BINDING usage");

                // Multisampled check.
                if (le.texture.multisampled)
                {
                    if (tv->texture->sampleCount <= 1)
                        return makeInvalid("layout requires multisampled but texture sampleCount is 1");
                }
                else
                {
                    if (tv->texture->sampleCount > 1)
                        return makeInvalid("layout requires non-multisampled but texture sampleCount > 1");
                }

                {
                    const WGPUTextureFormat fmt = tv->format;
                    const WGPUTextureSampleType st = le.texture.sampleType;

                    auto isDepth = pwgpu::IsDepthStencilFormat(fmt) && pwgpu::HasDepthAspect(fmt);
                    auto isStencilOnly = fmt == WGPUTextureFormat_Stencil8;
                    auto is32BitFloat = (fmt == WGPUTextureFormat_R32Float ||
                                         fmt == WGPUTextureFormat_RG32Float ||
                                         fmt == WGPUTextureFormat_RGBA32Float);
                    auto isSint = (fmt == WGPUTextureFormat_R8Sint || fmt == WGPUTextureFormat_RG8Sint ||
                                   fmt == WGPUTextureFormat_RGBA8Sint || fmt == WGPUTextureFormat_R16Sint ||
                                   fmt == WGPUTextureFormat_RG16Sint || fmt == WGPUTextureFormat_RGBA16Sint ||
                                   fmt == WGPUTextureFormat_R32Sint || fmt == WGPUTextureFormat_RG32Sint ||
                                   fmt == WGPUTextureFormat_RGBA32Sint);
                    auto isUint = (fmt == WGPUTextureFormat_R8Uint || fmt == WGPUTextureFormat_RG8Uint ||
                                   fmt == WGPUTextureFormat_RGBA8Uint || fmt == WGPUTextureFormat_R16Uint ||
                                   fmt == WGPUTextureFormat_RG16Uint || fmt == WGPUTextureFormat_RGBA16Uint ||
                                   fmt == WGPUTextureFormat_R32Uint || fmt == WGPUTextureFormat_RG32Uint ||
                                   fmt == WGPUTextureFormat_RGBA32Uint || fmt == WGPUTextureFormat_RGB10A2Uint);

                    bool float32Filterable = false;
                    if (device)
                    {
                        for (auto f : device->features)
                            if (f == WGPUFeatureName_Float32Filterable)
                            {
                                float32Filterable = true;
                                break;
                            }
                    }

                    bool ok = false;
                    if (isDepth)
                        ok = (st == WGPUTextureSampleType_Depth ||
                              st == WGPUTextureSampleType_UnfilterableFloat);
                    else if (isStencilOnly)
                        ok = (st == WGPUTextureSampleType_Uint);
                    else if (isSint)
                        ok = (st == WGPUTextureSampleType_Sint);
                    else if (isUint)
                        ok = (st == WGPUTextureSampleType_Uint);
                    else if (is32BitFloat)
                        ok = (st == WGPUTextureSampleType_UnfilterableFloat ||
                              (st == WGPUTextureSampleType_Float && float32Filterable));
                    else
                        ok = (st == WGPUTextureSampleType_Float ||
                              st == WGPUTextureSampleType_UnfilterableFloat);

                    if (!ok)
                        return makeInvalid("textureView format is not compatible with layout sampleType");
                }
            }
            else if (le.storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed)
            {

                if (!hasTexView)
                    return makeInvalid("layout expects storageTexture but entry has no textureView");
                WGPUTextureViewImpl *tv = entry.textureView;

                // viewDimension must match.
                if (tv->dimension != le.storageTexture.viewDimension)
                    return makeInvalid("storageTexture view dimension does not match layout");

                // Format must match.
                if (tv->format != le.storageTexture.format)
                    return makeInvalid("storageTexture view format does not match layout");

                // Usage must include STORAGE_BINDING.
                if (!(tv->usage & WGPUTextureUsage_StorageBinding))
                    return makeInvalid("textureView missing STORAGE_BINDING usage");

                // mipLevelCount must be 1.
                if (tv->mipLevelCount != 1)
                    return makeInvalid("storageTexture view must have mipLevelCount = 1");
            }
            else if (le.hasExternalTexture)
            {
                // External texture binding — not supported yet, always invalid.
                return makeInvalid("external texture bindings are not supported");
            }
        }

        // Create pe::Descriptor (VkDescriptorSet) if the layout has a VkDescriptorSetLayout.
        if (descriptor->layout->layout && !descriptor->layout->bindingInfos.empty())
        {
            bg->descriptor = pe::Descriptor::Create(
                descriptor->layout->bindingInfos,
                descriptor->layout->stage,
                false,
                bg->label.empty() ? "wgpu_bg" : bg->label);

            // Bind the actual resources to each descriptor binding.
            for (size_t i = 0; i < descriptor->entryCount; ++i)
            {
                const WGPUBindGroupEntry &entry = descriptor->entries[i];
                auto it = layoutMap.find(entry.binding);
                if (it == layoutMap.end())
                    continue;
                const WGPUBindGroupLayoutEntryResolved &le = *it->second;

                if (le.buffer.type != WGPUBufferBindingType_BindingNotUsed && entry.buffer)
                {
                    uint64_t offset = entry.offset;
                    uint64_t size = entry.size;
                    if (size == WGPU_WHOLE_SIZE)
                        size = entry.buffer->size - offset;
                    if (entry.buffer->peBuffer)
                        bg->descriptor->SetBuffer(entry.binding, entry.buffer->peBuffer, offset, size);
                }
                else if (le.sampler.type != WGPUSamplerBindingType_BindingNotUsed && entry.sampler)
                {
                    if (entry.sampler->sampler)
                        bg->descriptor->SetSampler(entry.binding, entry.sampler->sampler);
                }
                else if (le.texture.sampleType != WGPUTextureSampleType_BindingNotUsed && entry.textureView)
                {
                    if (entry.textureView->view)
                        bg->descriptor->SetImageView(entry.binding, entry.textureView->view);
                }
                else if (le.storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed && entry.textureView)
                {
                    if (entry.textureView->view)
                        bg->descriptor->SetImageView(entry.binding, entry.textureView->view);
                }
            }

            bg->descriptor->Update();
        }

        return bg;
    }

    WGPUPipelineLayout wgpuDeviceCreatePipelineLayout(WGPUDevice device, WGPUPipelineLayoutDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreatePipelineLayout", true))
            return nullptr;

        auto *pl = new WGPUPipelineLayoutImpl();
        wgpuDeviceAddRef(device);
        pl->device = device;
        if (descriptor->label.data)
            pl->label = pwgpu::ToString(descriptor->label);

        const WGPULimits &limits = device->limits;

        auto makeInvalid = [&](const char *msg) -> WGPUPipelineLayout
        {
            std::string what = std::string("PhasmaWebGPU: createPipelineLayout: ") + msg;
            device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(what));
            pl->invalid = true;
            return pl;
        };

        if (descriptor->bindGroupLayoutCount > limits.maxBindGroups)
            return makeInvalid("bindGroupLayoutCount exceeds maxBindGroups");

        struct StageCounters
        {
            uint32_t uniformBuffers = 0;
            uint32_t storageBuffers = 0;
            uint32_t samplers = 0;
            uint32_t sampledTextures = 0;
            uint32_t storageTextures = 0;
        };
        StageCounters vertexCounters{}, fragmentCounters{}, computeCounters{};
        uint32_t dynamicUniformBuffers = 0;
        uint32_t dynamicStorageBuffers = 0;

        pl->bindGroupLayouts.resize(descriptor->bindGroupLayoutCount, nullptr);

        for (size_t i = 0; i < descriptor->bindGroupLayoutCount; ++i)
        {
            WGPUBindGroupLayout bglHandle = descriptor->bindGroupLayouts[i];
            if (!bglHandle)
                continue;

            if (bglHandle->invalid)
                return makeInvalid("one of the bind group layouts is invalid");

            if (bglHandle->exclusivePipeline != nullptr)
                return makeInvalid("bind group layout has a non-null exclusivePipeline");

            if (bglHandle->entries.empty())
                continue;

            pl->bindGroupLayouts[i] = bglHandle;
            wgpuBindGroupLayoutAddRef(bglHandle);

            for (const auto &entry : bglHandle->entries)
            {
                bool hasBuf = (entry.buffer.type != WGPUBufferBindingType_BindingNotUsed);
                bool hasSamp = (entry.sampler.type != WGPUSamplerBindingType_BindingNotUsed);
                bool hasTex = (entry.texture.sampleType != WGPUTextureSampleType_BindingNotUsed);
                bool hasStorTex = (entry.storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed);
                bool hasExtTex = entry.hasExternalTexture;

                if (hasBuf && entry.buffer.hasDynamicOffset)
                {
                    if (entry.buffer.type == WGPUBufferBindingType_Uniform)
                        dynamicUniformBuffers++;
                    else
                        dynamicStorageBuffers++;
                }

                auto accumulate = [&](StageCounters &c)
                {
                    if (hasBuf)
                    {
                        if (entry.buffer.type == WGPUBufferBindingType_Uniform)
                            c.uniformBuffers++;
                        else
                            c.storageBuffers++;
                    }
                    else if (hasSamp)
                        c.samplers++;
                    else if (hasTex)
                        c.sampledTextures++;
                    else if (hasStorTex)
                        c.storageTextures++;
                    else if (hasExtTex)
                    {
                        c.sampledTextures += 4;
                        c.samplers++;
                        c.uniformBuffers++;
                    }
                };

                if (entry.visibility & WGPUShaderStage_Vertex)
                    accumulate(vertexCounters);
                if (entry.visibility & WGPUShaderStage_Fragment)
                    accumulate(fragmentCounters);
                if (entry.visibility & WGPUShaderStage_Compute)
                    accumulate(computeCounters);
            }
        }

        if (vertexCounters.uniformBuffers > limits.maxUniformBuffersPerShaderStage)
            return makeInvalid("combined vertex stage exceeds maxUniformBuffersPerShaderStage");
        if (vertexCounters.storageBuffers > limits.maxStorageBuffersPerShaderStage)
            return makeInvalid("combined vertex stage exceeds maxStorageBuffersPerShaderStage");
        if (vertexCounters.samplers > limits.maxSamplersPerShaderStage)
            return makeInvalid("combined vertex stage exceeds maxSamplersPerShaderStage");
        if (vertexCounters.sampledTextures > limits.maxSampledTexturesPerShaderStage)
            return makeInvalid("combined vertex stage exceeds maxSampledTexturesPerShaderStage");
        if (vertexCounters.storageTextures > limits.maxStorageTexturesPerShaderStage)
            return makeInvalid("combined vertex stage exceeds maxStorageTexturesPerShaderStage");

        if (fragmentCounters.uniformBuffers > limits.maxUniformBuffersPerShaderStage)
            return makeInvalid("combined fragment stage exceeds maxUniformBuffersPerShaderStage");
        if (fragmentCounters.storageBuffers > limits.maxStorageBuffersPerShaderStage)
            return makeInvalid("combined fragment stage exceeds maxStorageBuffersPerShaderStage");
        if (fragmentCounters.samplers > limits.maxSamplersPerShaderStage)
            return makeInvalid("combined fragment stage exceeds maxSamplersPerShaderStage");
        if (fragmentCounters.sampledTextures > limits.maxSampledTexturesPerShaderStage)
            return makeInvalid("combined fragment stage exceeds maxSampledTexturesPerShaderStage");
        if (fragmentCounters.storageTextures > limits.maxStorageTexturesPerShaderStage)
            return makeInvalid("combined fragment stage exceeds maxStorageTexturesPerShaderStage");

        if (computeCounters.uniformBuffers > limits.maxUniformBuffersPerShaderStage)
            return makeInvalid("combined compute stage exceeds maxUniformBuffersPerShaderStage");
        if (computeCounters.storageBuffers > limits.maxStorageBuffersPerShaderStage)
            return makeInvalid("combined compute stage exceeds maxStorageBuffersPerShaderStage");
        if (computeCounters.samplers > limits.maxSamplersPerShaderStage)
            return makeInvalid("combined compute stage exceeds maxSamplersPerShaderStage");
        if (computeCounters.sampledTextures > limits.maxSampledTexturesPerShaderStage)
            return makeInvalid("combined compute stage exceeds maxSampledTexturesPerShaderStage");
        if (computeCounters.storageTextures > limits.maxStorageTexturesPerShaderStage)
            return makeInvalid("combined compute stage exceeds maxStorageTexturesPerShaderStage");

        if (dynamicUniformBuffers > limits.maxDynamicUniformBuffersPerPipelineLayout)
            return makeInvalid("exceeds maxDynamicUniformBuffersPerPipelineLayout");
        if (dynamicStorageBuffers > limits.maxDynamicStorageBuffersPerPipelineLayout)
            return makeInvalid("exceeds maxDynamicStorageBuffersPerPipelineLayout");

        std::vector<vk::DescriptorSetLayout> vkSetLayouts;
        vkSetLayouts.reserve(pl->bindGroupLayouts.size());

        while (!pl->bindGroupLayouts.empty() && pl->bindGroupLayouts.back() == nullptr)
            pl->bindGroupLayouts.pop_back();

        for (auto *bglPtr : pl->bindGroupLayouts)
        {
            if (bglPtr && bglPtr->layout)
            {
                vkSetLayouts.push_back(bglPtr->layout->ApiHandle());
            }
            else
            {
                vk::DescriptorSetLayoutCreateInfo emptyCI{};
                auto emptyLayout = device->rhi->GetDevice().createDescriptorSetLayout(emptyCI);
                pl->ownedEmptySetLayouts.push_back(emptyLayout);
                vkSetLayouts.push_back(emptyLayout);
            }
        }

        {
            vk::PipelineLayoutCreateInfo ci{};
            ci.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
            ci.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
            pl->vkLayout = device->rhi->GetDevice().createPipelineLayout(ci);
        }

        return pl;
    }

    WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, WGPUShaderModuleDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateShaderModule", true))
            return nullptr;

        auto *sm = new WGPUShaderModuleImpl();
        sm->device = device;
        device->refCount.fetch_add(1, std::memory_order_relaxed);
        if (descriptor->label.data)
            sm->label = pwgpu::ToString(descriptor->label);

        auto *spirvDesc = pwgpu::FindChained<WGPUShaderSourceSPIRV>(
            descriptor->nextInChain, WGPUSType_ShaderSourceSPIRV);
        auto *wgslDesc = pwgpu::FindChained<WGPUShaderSourceWGSL>(
            descriptor->nextInChain, WGPUSType_ShaderSourceWGSL);

        if (!spirvDesc && !wgslDesc)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("wgpuDeviceCreateShaderModule: "
                                                    "descriptor chain must contain WGPUShaderSourceSPIRV "
                                                    "or WGPUShaderSourceWGSL"));
            sm->invalid = true;
            WGPUCompilationMessageStorage msg;
            msg.type = WGPUCompilationMessageType_Error;
            msg.message = "No shader source provided in descriptor chain";
            sm->compilationMessages.push_back(std::move(msg));
            return sm;
        }

        if (spirvDesc && wgslDesc)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("wgpuDeviceCreateShaderModule: "
                                                    "descriptor chain contains both SPIR-V and WGSL sources; "
                                                    "exactly one is required"));
            sm->invalid = true;
            WGPUCompilationMessageStorage msg;
            msg.type = WGPUCompilationMessageType_Error;
            msg.message = "Both SPIR-V and WGSL sources provided; exactly one required";
            sm->compilationMessages.push_back(std::move(msg));
            return sm;
        }

        if (wgslDesc)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("wgpuDeviceCreateShaderModule: "
                                                    "WGSL shader source is not supported; "
                                                    "use WGPUShaderSourceSPIRV instead"));
            sm->invalid = true;
            WGPUCompilationMessageStorage msg;
            msg.type = WGPUCompilationMessageType_Error;
            msg.message = "WGSL shader source is not supported by this implementation";
            sm->compilationMessages.push_back(std::move(msg));
            return sm;
        }

        if (!spirvDesc->code || spirvDesc->codeSize == 0)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("wgpuDeviceCreateShaderModule: "
                                                    "SPIR-V code is null or empty"));
            sm->invalid = true;
            WGPUCompilationMessageStorage msg;
            msg.type = WGPUCompilationMessageType_Error;
            msg.message = "SPIR-V code is null or empty";
            sm->compilationMessages.push_back(std::move(msg));
            return sm;
        }

        if (spirvDesc->codeSize < 5 || spirvDesc->code[0] != 0x07230203u)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("wgpuDeviceCreateShaderModule: "
                                                    "invalid SPIR-V: bad magic number or too short"));
            sm->invalid = true;
            WGPUCompilationMessageStorage msg;
            msg.type = WGPUCompilationMessageType_Error;
            msg.message = "Invalid SPIR-V: bad magic number or module too short";
            sm->compilationMessages.push_back(std::move(msg));
            return sm;
        }

        sm->spirv.assign(spirvDesc->code, spirvDesc->code + spirvDesc->codeSize);

        return sm;
    }

    // ======================================================================
    // ======================================================================

    static WGPUComputePipeline MakeInvalidComputePipeline(WGPUDevice device, const char *label)
    {
        auto *cp = new WGPUComputePipelineImpl();
        cp->device = device;
        cp->invalid = true;
        if (label)
            cp->label = label;
        wgpuDeviceAddRef(device);
        return cp;
    }

    static WGPURenderPipeline MakeInvalidRenderPipeline(WGPUDevice device, const char *label)
    {
        auto *rp = new WGPURenderPipelineImpl();
        rp->device = device;
        rp->invalid = true;
        if (label)
            rp->label = label;
        wgpuDeviceAddRef(device);
        return rp;
    }

    // Validate GPUPrimitiveState (§10.3.2)
    static bool ValidatePrimitiveState(WGPUDevice device,
                                       const WGPUPrimitiveState &prim)
    {
        auto topo = prim.topology;
        if (topo == WGPUPrimitiveTopology(0))
            topo = WGPUPrimitiveTopology_TriangleList;

        if (!pwgpu::IsStripTopology(topo) && prim.stripIndexFormat != WGPUIndexFormat_Undefined)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: stripIndexFormat must not be set for non-strip topologies"));
            return false;
        }

        if (prim.unclippedDepth && !DeviceHasFeature(device, WGPUFeatureName_DepthClipControl))
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: unclippedDepth requires depth-clip-control feature"));
            return false;
        }

        return true;
    }

    // Validate GPUMultisampleState (§10.3.3)
    static bool ValidateMultisampleState(WGPUDevice device,
                                         const WGPUMultisampleState &ms)
    {
        uint32_t count = ms.count ? ms.count : 1;
        if (count != 1 && count != 4)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: multisample.count must be 1 or 4"));
            return false;
        }
        if (ms.alphaToCoverageEnabled && count <= 1)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: alphaToCoverage requires count > 1"));
            return false;
        }
        return true;
    }

    // Validate GPUDepthStencilState (§10.3.6)
    static bool ValidateDepthStencilState(WGPUDevice device,
                                          const WGPUDepthStencilState &ds,
                                          WGPUPrimitiveTopology topology)
    {
        if (!pwgpu::IsDepthStencilFormat(ds.format))
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: depthStencil.format must be a depth-stencil format"));
            return false;
        }

        bool hasDepth = pwgpu::HasDepthAspect(ds.format);
        bool hasStencil = pwgpu::HasStencilAspect(ds.format);

        // If depthWriteEnabled or depthCompare != always, format must have depth
        if ((ds.depthWriteEnabled == WGPUOptionalBool_True ||
             (ds.depthCompare != WGPUCompareFunction_Undefined && ds.depthCompare != WGPUCompareFunction_Always)) &&
            !hasDepth)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: depthWrite/depthCompare used but format has no depth"));
            return false;
        }

        // Stencil front/back non-default requires stencil component
        auto isDefaultStencil = [](const WGPUStencilFaceState &s)
        {
            return (s.compare == WGPUCompareFunction_Always || s.compare == WGPUCompareFunction_Undefined) &&
                   (s.failOp == WGPUStencilOperation_Keep || s.failOp == WGPUStencilOperation(0)) &&
                   (s.depthFailOp == WGPUStencilOperation_Keep || s.depthFailOp == WGPUStencilOperation(0)) &&
                   (s.passOp == WGPUStencilOperation_Keep || s.passOp == WGPUStencilOperation(0));
        };

        if ((!isDefaultStencil(ds.stencilFront) || !isDefaultStencil(ds.stencilBack)) && !hasStencil)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: stencil ops used but format has no stencil"));
            return false;
        }

        // If format has depth, depthWriteEnabled must be provided
        if (hasDepth && ds.depthWriteEnabled == WGPUOptionalBool_Undefined)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: depthWriteEnabled must be provided when format has depth"));
            return false;
        }

        // If format has depth, depthCompare must be provided when depthWrite or stencil ops need it
        if (hasDepth)
        {
            bool needsCompare = (ds.depthWriteEnabled == WGPUOptionalBool_True) ||
                                (ds.stencilFront.depthFailOp != WGPUStencilOperation_Keep && ds.stencilFront.depthFailOp != WGPUStencilOperation(0)) ||
                                (ds.stencilBack.depthFailOp != WGPUStencilOperation_Keep && ds.stencilBack.depthFailOp != WGPUStencilOperation(0));
            if (needsCompare && ds.depthCompare == WGPUCompareFunction_Undefined)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: depthCompare must be provided"));
                return false;
            }
        }

        // Depth bias restrictions for point/line topologies
        auto topo = topology;
        if (topo == WGPUPrimitiveTopology(0))
            topo = WGPUPrimitiveTopology_TriangleList;
        if (pwgpu::IsLineOrPointTopology(topo))
        {
            if (ds.depthBias != 0 || ds.depthBiasSlopeScale != 0.0f || ds.depthBiasClamp != 0.0f)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: depthBias must be 0 for point/line topologies"));
                return false;
            }
        }

        return true;
    }

    // Validate GPUBlendComponent (§10.3.5.1)
    static bool ValidateBlendComponent(WGPUDevice device,
                                       const WGPUBlendComponent &comp)
    {
        auto op = comp.operation;
        if (op == WGPUBlendOperation(0))
            op = WGPUBlendOperation_Add;
        auto src = comp.srcFactor;
        if (src == WGPUBlendFactor(0))
            src = WGPUBlendFactor_One;
        auto dst = comp.dstFactor;
        if (dst == WGPUBlendFactor(0))
            dst = WGPUBlendFactor_Zero;

        if (op == WGPUBlendOperation_Min || op == WGPUBlendOperation_Max)
        {
            if (src != WGPUBlendFactor_One || dst != WGPUBlendFactor_One)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: min/max blend op requires srcFactor=one and dstFactor=one"));
                return false;
            }
        }

        // Dual-source blend factors require the feature
        auto isDualSrc = [](WGPUBlendFactor f)
        {
            return f == WGPUBlendFactor_Src1 || f == WGPUBlendFactor_OneMinusSrc1 ||
                   f == WGPUBlendFactor_Src1Alpha || f == WGPUBlendFactor_OneMinusSrc1Alpha;
        };
        if (isDualSrc(src) || isDualSrc(dst))
        {
            if (!DeviceHasFeature(device, WGPUFeatureName_DualSourceBlending))
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: dual-source blend factors require dual-source-blending feature"));
                return false;
            }
        }
        return true;
    }

    // Validate vertex buffer layout (§10.3.7)
    static bool ValidateVertexBufferLayout(WGPUDevice device,
                                           const WGPUVertexBufferLayout &buf,
                                           std::set<uint32_t> &usedLocations)
    {
        const auto &limits = device->limits;
        if (buf.arrayStride > limits.maxVertexBufferArrayStride)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: arrayStride exceeds maxVertexBufferArrayStride"));
            return false;
        }
        if (buf.arrayStride % 4 != 0)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: arrayStride must be a multiple of 4"));
            return false;
        }

        for (size_t i = 0; i < buf.attributeCount; ++i)
        {
            const auto &attr = buf.attributes[i];
            uint32_t byteSize = pwgpu::VertexFormatByteSize(attr.format);
            if (byteSize == 0)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: invalid vertex format"));
                return false;
            }

            if (buf.arrayStride == 0)
            {
                if (attr.offset + byteSize > limits.maxVertexBufferArrayStride)
                {
                    device->reportError(WGPUErrorType_Validation,
                                        pwgpu::ToStringView("createRenderPipeline: attribute exceeds maxVertexBufferArrayStride (zero stride)"));
                    return false;
                }
            }
            else
            {
                if (attr.offset + byteSize > buf.arrayStride)
                {
                    device->reportError(WGPUErrorType_Validation,
                                        pwgpu::ToStringView("createRenderPipeline: attribute offset+size exceeds arrayStride"));
                    return false;
                }
            }

            uint32_t minAlign = (std::min)(byteSize, 4u);
            if (attr.offset % minAlign != 0)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: attribute offset alignment violation"));
                return false;
            }

            if (attr.shaderLocation >= limits.maxVertexAttributes)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: shaderLocation >= maxVertexAttributes"));
                return false;
            }

            if (!usedLocations.insert(attr.shaderLocation).second)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: duplicate vertex attribute shaderLocation"));
                return false;
            }
        }
        return true;
    }

    // ======================================================================
    // ======================================================================

    WGPUComputePipeline wgpuDeviceCreateComputePipeline(WGPUDevice device, WGPUComputePipelineDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateComputePipeline", true))
            return nullptr;

        const char *labelStr = descriptor->label.data ? descriptor->label.data : nullptr;

        auto *pipeLayout = reinterpret_cast<WGPUPipelineLayoutImpl *>(descriptor->layout);
        if (!pipeLayout)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createComputePipeline: layout is null (auto layout not supported)"));
            return MakeInvalidComputePipeline(device, labelStr);
        }
        if (pipeLayout->invalid)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createComputePipeline: layout is invalid"));
            return MakeInvalidComputePipeline(device, labelStr);
        }

        auto *shaderModule = descriptor->compute.module;
        if (!shaderModule)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createComputePipeline: compute.module is null"));
            return MakeInvalidComputePipeline(device, labelStr);
        }
        if (shaderModule->invalid)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createComputePipeline: compute.module is invalid"));
            return MakeInvalidComputePipeline(device, labelStr);
        }
        if (shaderModule->spirv.empty())
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createComputePipeline: compute.module has no SPIR-V code"));
            return MakeInvalidComputePipeline(device, labelStr);
        }

        std::string entryPointName;
        if (descriptor->compute.entryPoint.data && descriptor->compute.entryPoint.length != 0)
            entryPointName = pwgpu::ToString(descriptor->compute.entryPoint);
        else
            entryPointName = "main";

        auto vkDev = device->rhi->GetDevice();

        vk::ShaderModuleCreateInfo smci{};
        smci.codeSize = shaderModule->spirv.size() * sizeof(uint32_t);
        smci.pCode = shaderModule->spirv.data();
        vk::ShaderModule vkShaderModule;
        try
        {
            vkShaderModule = vkDev.createShaderModule(smci);
        }
        catch (...)
        {
            device->reportError(WGPUErrorType_Internal,
                                pwgpu::ToStringView("createComputePipeline: failed to create VkShaderModule"));
            return MakeInvalidComputePipeline(device, labelStr);
        }

        vk::ComputePipelineCreateInfo cpci{};
        cpci.stage.stage = vk::ShaderStageFlagBits::eCompute;
        cpci.stage.module = vkShaderModule;
        cpci.stage.pName = entryPointName.c_str();
        cpci.layout = pipeLayout->vkLayout;

        vk::Pipeline vkPipeline;
        try
        {
            auto result = vkDev.createComputePipeline(nullptr, cpci);
            if (result.result != vk::Result::eSuccess)
            {
                vkDev.destroyShaderModule(vkShaderModule);
                device->reportError(WGPUErrorType_Internal,
                                    pwgpu::ToStringView("createComputePipeline: vkCreateComputePipelines failed"));
                return MakeInvalidComputePipeline(device, labelStr);
            }
            vkPipeline = result.value;
        }
        catch (...)
        {
            vkDev.destroyShaderModule(vkShaderModule);
            device->reportError(WGPUErrorType_Internal,
                                pwgpu::ToStringView("createComputePipeline: vkCreateComputePipelines threw"));
            return MakeInvalidComputePipeline(device, labelStr);
        }

        vkDev.destroyShaderModule(vkShaderModule);

        auto *cp = new WGPUComputePipelineImpl();
        cp->device = device;
        if (labelStr)
            cp->label = labelStr;
        cp->vkPipeline = vkPipeline;
        cp->layout = pipeLayout;
        cp->entryPoint = entryPointName;
        wgpuDeviceAddRef(device);
        wgpuPipelineLayoutAddRef(pipeLayout);

        return cp;
    }

    WGPUFuture wgpuDeviceCreateComputePipelineAsync(WGPUDevice device,
                                                    WGPUComputePipelineDescriptor const *descriptor,
                                                    WGPUCreateComputePipelineAsyncCallbackInfo callbackInfo)
    {
        WGPUComputePipeline cp = wgpuDeviceCreateComputePipeline(device, descriptor);
        bool valid = cp && !cp->invalid;
        WGPUInstanceImpl *inst = device ? device->instance : nullptr;
        if (!inst || !callbackInfo.callback)
            return inst ? inst->futures.NextId() : WGPUFuture{0};
        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        auto status = valid ? WGPUCreatePipelineAsyncStatus_Success : WGPUCreatePipelineAsyncStatus_ValidationError;
        return inst->futures.TrackEvent(callbackInfo.mode,
                                        [cb, u1, u2, status, cp]()
                                        {
                                            cb(status, cp, {nullptr, 0}, u1, u2);
                                        });
    }

    // ======================================================================
    // ======================================================================

    WGPURenderPipeline wgpuDeviceCreateRenderPipeline(WGPUDevice device, WGPURenderPipelineDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateRenderPipeline", true))
            return nullptr;

        const char *labelStr = descriptor->label.data ? descriptor->label.data : nullptr;
        auto vkDev = device->rhi->GetDevice();
        const auto &limits = device->limits;

        auto *pipeLayout = reinterpret_cast<WGPUPipelineLayoutImpl *>(descriptor->layout);
        if (!pipeLayout)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: layout is null (auto layout not supported)"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }
        if (pipeLayout->invalid)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: layout is invalid"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        auto *vertModule = descriptor->vertex.module;
        if (!vertModule || vertModule->invalid || vertModule->spirv.empty())
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: vertex.module is null/invalid/empty"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        std::string vertEntry;
        if (descriptor->vertex.entryPoint.data && descriptor->vertex.entryPoint.length != 0)
            vertEntry = pwgpu::ToString(descriptor->vertex.entryPoint);
        else
            vertEntry = "main";

        auto primTopology = descriptor->primitive.topology;
        if (primTopology == WGPUPrimitiveTopology(0))
            primTopology = WGPUPrimitiveTopology_TriangleList;
        if (!ValidatePrimitiveState(device, descriptor->primitive))
            return MakeInvalidRenderPipeline(device, labelStr);

        auto msCount = descriptor->multisample.count;
        if (msCount == 0)
            msCount = 1;
        if (!ValidateMultisampleState(device, descriptor->multisample))
            return MakeInvalidRenderPipeline(device, labelStr);

        bool hasDepthStencil = (descriptor->depthStencil != nullptr);
        if (hasDepthStencil)
        {
            if (!ValidateDepthStencilState(device, *descriptor->depthStencil, primTopology))
                return MakeInvalidRenderPipeline(device, labelStr);
        }

        if (descriptor->vertex.bufferCount > limits.maxVertexBuffers)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: vertex.bufferCount exceeds maxVertexBuffers"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        uint32_t totalAttributes = 0;
        std::set<uint32_t> usedShaderLocations;
        for (size_t i = 0; i < descriptor->vertex.bufferCount; ++i)
        {
            const auto &vbuf = descriptor->vertex.buffers[i];
            if (vbuf.attributeCount == 0 && vbuf.arrayStride == 0)
                continue; // null slot
            if (!ValidateVertexBufferLayout(device, vbuf, usedShaderLocations))
                return MakeInvalidRenderPipeline(device, labelStr);
            totalAttributes += static_cast<uint32_t>(vbuf.attributeCount);
        }
        if (totalAttributes > limits.maxVertexAttributes)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: total vertex attributes exceeds maxVertexAttributes"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        bool hasFragment = (descriptor->fragment != nullptr);
        WGPUShaderModuleImpl *fragModuleSrc = nullptr;
        std::string fragEntry;
        if (hasFragment)
        {
            fragModuleSrc = descriptor->fragment->module;
            if (!fragModuleSrc || fragModuleSrc->invalid || fragModuleSrc->spirv.empty())
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: fragment.module is null/invalid/empty"));
                return MakeInvalidRenderPipeline(device, labelStr);
            }
            if (descriptor->fragment->entryPoint.data && descriptor->fragment->entryPoint.length != 0)
                fragEntry = pwgpu::ToString(descriptor->fragment->entryPoint);
            else
                fragEntry = "main";
        }

        std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
        std::vector<vk::Format> colorVkFormats;
        uint32_t colorAttachmentBytesPerSample = 0;
        bool usesDualSourceBlending = false;

        if (hasFragment)
        {
            if (descriptor->fragment->targetCount > limits.maxColorAttachments)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: fragment.targetCount exceeds maxColorAttachments"));
                return MakeInvalidRenderPipeline(device, labelStr);
            }

            for (size_t i = 0; i < descriptor->fragment->targetCount; ++i)
            {
                const auto &ct = descriptor->fragment->targets[i];
                vk::PipelineColorBlendAttachmentState ba{};

                if (ct.format == WGPUTextureFormat_Undefined)
                {
                    blendAttachments.push_back(ba);
                    colorVkFormats.push_back(vk::Format::eUndefined);
                    continue;
                }

                if (!pwgpu::IsRenderableFormat(ct.format))
                {
                    device->reportError(WGPUErrorType_Validation,
                                        pwgpu::ToStringView("createRenderPipeline: color target format is not renderable"));
                    return MakeInvalidRenderPipeline(device, labelStr);
                }

                if (ct.writeMask > 0xF)
                {
                    device->reportError(WGPUErrorType_Validation,
                                        pwgpu::ToStringView("createRenderPipeline: color target writeMask > 0xF"));
                    return MakeInvalidRenderPipeline(device, labelStr);
                }

                ba.colorWriteMask = vk::ColorComponentFlags(static_cast<uint32_t>(ct.writeMask));

                if (ct.blend)
                {
                    if (!pwgpu::IsBlendableFormat(ct.format))
                    {
                        device->reportError(WGPUErrorType_Validation,
                                            pwgpu::ToStringView("createRenderPipeline: blend set but format not blendable"));
                        return MakeInvalidRenderPipeline(device, labelStr);
                    }

                    if (!ValidateBlendComponent(device, ct.blend->color) ||
                        !ValidateBlendComponent(device, ct.blend->alpha))
                        return MakeInvalidRenderPipeline(device, labelStr);

                    ba.blendEnable = VK_TRUE;

                    auto colorOp = ct.blend->color.operation;
                    if (colorOp == WGPUBlendOperation(0))
                        colorOp = WGPUBlendOperation_Add;
                    auto colorSrc = ct.blend->color.srcFactor;
                    if (colorSrc == WGPUBlendFactor(0))
                        colorSrc = WGPUBlendFactor_One;
                    auto colorDst = ct.blend->color.dstFactor;
                    if (colorDst == WGPUBlendFactor(0))
                        colorDst = WGPUBlendFactor_Zero;

                    ba.colorBlendOp = pwgpu::ToVkBlendOp(colorOp);
                    ba.srcColorBlendFactor = pwgpu::ToVkBlendFactor(colorSrc);
                    ba.dstColorBlendFactor = pwgpu::ToVkBlendFactor(colorDst);

                    auto alphaOp = ct.blend->alpha.operation;
                    if (alphaOp == WGPUBlendOperation(0))
                        alphaOp = WGPUBlendOperation_Add;
                    auto alphaSrc = ct.blend->alpha.srcFactor;
                    if (alphaSrc == WGPUBlendFactor(0))
                        alphaSrc = WGPUBlendFactor_One;
                    auto alphaDst = ct.blend->alpha.dstFactor;
                    if (alphaDst == WGPUBlendFactor(0))
                        alphaDst = WGPUBlendFactor_Zero;

                    ba.alphaBlendOp = pwgpu::ToVkBlendOp(alphaOp);
                    ba.srcAlphaBlendFactor = pwgpu::ToVkBlendFactor(alphaSrc);
                    ba.dstAlphaBlendFactor = pwgpu::ToVkBlendFactor(alphaDst);

                    auto isDualSrc = [](WGPUBlendFactor f)
                    {
                        return f == WGPUBlendFactor_Src1 || f == WGPUBlendFactor_OneMinusSrc1 ||
                               f == WGPUBlendFactor_Src1Alpha || f == WGPUBlendFactor_OneMinusSrc1Alpha;
                    };
                    if (isDualSrc(colorSrc) || isDualSrc(colorDst) || isDualSrc(alphaSrc) || isDualSrc(alphaDst))
                        usesDualSourceBlending = true;
                }

                blendAttachments.push_back(ba);
                colorVkFormats.push_back(static_cast<vk::Format>(pwgpu::ToVkFormat(ct.format)));
                colorAttachmentBytesPerSample += pwgpu::FormatBytesPerSample(ct.format);
            }

            if (usesDualSourceBlending && descriptor->fragment->targetCount > 1)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: dual-source blending requires exactly 1 color target"));
                return MakeInvalidRenderPipeline(device, labelStr);
            }

            if (colorAttachmentBytesPerSample > limits.maxColorAttachmentBytesPerSample)
            {
                device->reportError(WGPUErrorType_Validation,
                                    pwgpu::ToStringView("createRenderPipeline: color attachment bytes per sample exceeds limit"));
                return MakeInvalidRenderPipeline(device, labelStr);
            }

            if (descriptor->multisample.alphaToCoverageEnabled)
            {
                if (descriptor->fragment->targetCount == 0 || descriptor->fragment->targets[0].format == WGPUTextureFormat_Undefined)
                {
                    device->reportError(WGPUErrorType_Validation,
                                        pwgpu::ToStringView("createRenderPipeline: alphaToCoverage requires non-null targets[0]"));
                    return MakeInvalidRenderPipeline(device, labelStr);
                }
                if (!pwgpu::IsBlendableFormat(descriptor->fragment->targets[0].format) ||
                    !pwgpu::FormatHasAlphaChannel(descriptor->fragment->targets[0].format))
                {
                    device->reportError(WGPUErrorType_Validation,
                                        pwgpu::ToStringView("createRenderPipeline: alphaToCoverage targets[0] must be blendable with alpha"));
                    return MakeInvalidRenderPipeline(device, labelStr);
                }
            }
        }

        bool hasAnyColorTarget = false;
        if (hasFragment)
        {
            for (size_t i = 0; i < descriptor->fragment->targetCount; ++i)
            {
                if (descriptor->fragment->targets[i].format != WGPUTextureFormat_Undefined)
                {
                    hasAnyColorTarget = true;
                    break;
                }
            }
        }
        if (!hasAnyColorTarget && !hasDepthStencil)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: must have at least one color or depth-stencil attachment"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        if (descriptor->multisample.alphaToCoverageEnabled && !hasFragment)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("createRenderPipeline: alphaToCoverage requires fragment stage"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        std::vector<vk::ShaderModule> tempModules;
        stages.reserve(2);

        auto createModule = [&](const std::vector<uint32_t> &spirv) -> vk::ShaderModule
        {
            vk::ShaderModuleCreateInfo ci{};
            ci.codeSize = spirv.size() * sizeof(uint32_t);
            ci.pCode = spirv.data();
            return vkDev.createShaderModule(ci);
        };

        vk::ShaderModule vkVertModule;
        try
        {
            vkVertModule = createModule(vertModule->spirv);
        }
        catch (...)
        {
            device->reportError(WGPUErrorType_Internal,
                                pwgpu::ToStringView("createRenderPipeline: failed to create vertex VkShaderModule"));
            return MakeInvalidRenderPipeline(device, labelStr);
        }
        tempModules.push_back(vkVertModule);

        vk::PipelineShaderStageCreateInfo vertStage{};
        vertStage.stage = vk::ShaderStageFlagBits::eVertex;
        vertStage.module = vkVertModule;
        vertStage.pName = vertEntry.c_str();
        stages.push_back(vertStage);

        if (hasFragment)
        {
            vk::ShaderModule vkFragModule;
            try
            {
                vkFragModule = createModule(fragModuleSrc->spirv);
            }
            catch (...)
            {
                for (auto m : tempModules)
                    vkDev.destroyShaderModule(m);
                device->reportError(WGPUErrorType_Internal,
                                    pwgpu::ToStringView("createRenderPipeline: failed to create fragment VkShaderModule"));
                return MakeInvalidRenderPipeline(device, labelStr);
            }
            tempModules.push_back(vkFragModule);

            vk::PipelineShaderStageCreateInfo fragStage{};
            fragStage.stage = vk::ShaderStageFlagBits::eFragment;
            fragStage.module = vkFragModule;
            fragStage.pName = fragEntry.c_str();
            stages.push_back(fragStage);
        }

        std::vector<vk::VertexInputBindingDescription> vertBindings;
        std::vector<vk::VertexInputAttributeDescription> vertAttrs;

        for (uint32_t slot = 0; slot < descriptor->vertex.bufferCount; ++slot)
        {
            const auto &vbuf = descriptor->vertex.buffers[slot];
            if (vbuf.attributeCount == 0 && vbuf.arrayStride == 0)
                continue;

            vk::VertexInputBindingDescription binding{};
            binding.binding = slot;
            binding.stride = static_cast<uint32_t>(vbuf.arrayStride);
            binding.inputRate = (vbuf.stepMode == WGPUVertexStepMode_Instance)
                                    ? vk::VertexInputRate::eInstance
                                    : vk::VertexInputRate::eVertex;
            vertBindings.push_back(binding);

            for (size_t a = 0; a < vbuf.attributeCount; ++a)
            {
                vk::VertexInputAttributeDescription attr{};
                attr.location = vbuf.attributes[a].shaderLocation;
                attr.binding = slot;
                attr.format = static_cast<vk::Format>(pwgpu::VertexFormatToVk(vbuf.attributes[a].format));
                attr.offset = static_cast<uint32_t>(vbuf.attributes[a].offset);
                vertAttrs.push_back(attr);
            }
        }

        vk::PipelineVertexInputStateCreateInfo vertexInputState{};
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertAttrs.size());
        vertexInputState.pVertexAttributeDescriptions = vertAttrs.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = pwgpu::ToVkTopology(primTopology);
        inputAssembly.primitiveRestartEnable = pwgpu::IsStripTopology(primTopology) ? VK_TRUE : VK_FALSE;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        auto frontFace = descriptor->primitive.frontFace;
        if (frontFace == WGPUFrontFace(0))
            frontFace = WGPUFrontFace_CCW;
        auto cullMode = descriptor->primitive.cullMode;
        if (cullMode == WGPUCullMode(0))
            cullMode = WGPUCullMode_None;

        vk::PipelineRasterizationStateCreateInfo rasterState{};
        rasterState.depthClampEnable = VK_FALSE;
        rasterState.rasterizerDiscardEnable = (!hasFragment && !hasDepthStencil) ? VK_TRUE : VK_FALSE;
        rasterState.polygonMode = vk::PolygonMode::eFill;
        rasterState.cullMode = pwgpu::ToVkCullMode(cullMode);
        rasterState.frontFace = pwgpu::ToVkFrontFace(frontFace);
        rasterState.lineWidth = 1.0f;

        if (hasDepthStencil)
        {
            bool hasBias = (descriptor->depthStencil->depthBias != 0 ||
                            descriptor->depthStencil->depthBiasSlopeScale != 0.0f ||
                            descriptor->depthStencil->depthBiasClamp != 0.0f);
            rasterState.depthBiasEnable = hasBias ? VK_TRUE : VK_FALSE;
            rasterState.depthBiasConstantFactor = static_cast<float>(descriptor->depthStencil->depthBias);
            rasterState.depthBiasSlopeFactor = descriptor->depthStencil->depthBiasSlopeScale;
            rasterState.depthBiasClamp = descriptor->depthStencil->depthBiasClamp;
        }

        if (descriptor->primitive.unclippedDepth)
            rasterState.depthClampEnable = VK_TRUE;

        vk::PipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.rasterizationSamples = pwgpu::ToVkSampleCount(msCount);
        uint32_t msMask = descriptor->multisample.mask;
        if (msMask == 0)
            msMask = 0xFFFFFFFF;
        multisampleState.pSampleMask = &msMask;
        multisampleState.alphaToCoverageEnable = descriptor->multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE;

        vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
        if (hasDepthStencil)
        {
            const auto &ds = *descriptor->depthStencil;
            bool hasDepthAspect = pwgpu::HasDepthAspect(ds.format);

            depthStencilState.depthTestEnable = hasDepthAspect ? VK_TRUE : VK_FALSE;
            depthStencilState.depthWriteEnable = (ds.depthWriteEnabled == WGPUOptionalBool_True) ? VK_TRUE : VK_FALSE;

            auto dc = ds.depthCompare;
            if (dc == WGPUCompareFunction_Undefined)
                dc = WGPUCompareFunction_Always;
            depthStencilState.depthCompareOp = pwgpu::ToVkCompareOp(dc);

            bool hasStencilAspect = pwgpu::HasStencilAspect(ds.format);
            depthStencilState.stencilTestEnable = hasStencilAspect ? VK_TRUE : VK_FALSE;

            auto mapFace = [](const WGPUStencilFaceState &face, uint32_t readMask, uint32_t writeMask) -> vk::StencilOpState
            {
                vk::StencilOpState s{};
                auto cmp = face.compare;
                if (cmp == WGPUCompareFunction_Undefined || cmp == WGPUCompareFunction(0))
                    cmp = WGPUCompareFunction_Always;
                s.compareOp = pwgpu::ToVkCompareOp(cmp);

                auto fail = face.failOp;
                if (fail == WGPUStencilOperation(0))
                    fail = WGPUStencilOperation_Keep;
                s.failOp = pwgpu::ToVkStencilOp(fail);

                auto dfail = face.depthFailOp;
                if (dfail == WGPUStencilOperation(0))
                    dfail = WGPUStencilOperation_Keep;
                s.depthFailOp = pwgpu::ToVkStencilOp(dfail);

                auto pass = face.passOp;
                if (pass == WGPUStencilOperation(0))
                    pass = WGPUStencilOperation_Keep;
                s.passOp = pwgpu::ToVkStencilOp(pass);

                s.compareMask = readMask;
                s.writeMask = writeMask;
                s.reference = 0;
                return s;
            };

            depthStencilState.front = mapFace(ds.stencilFront, ds.stencilReadMask, ds.stencilWriteMask);
            depthStencilState.back = mapFace(ds.stencilBack, ds.stencilReadMask, ds.stencilWriteMask);
        }

        vk::PipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.logicOpEnable = VK_FALSE;
        colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        colorBlendState.pAttachments = blendAttachments.data();

        std::vector<vk::DynamicState> dynStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        };
        if (hasDepthStencil && pwgpu::HasStencilAspect(descriptor->depthStencil->format))
            dynStates.push_back(vk::DynamicState::eStencilReference);
        dynStates.push_back(vk::DynamicState::eBlendConstants);

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
        dynamicState.pDynamicStates = dynStates.data();

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorVkFormats.size());
        renderingInfo.pColorAttachmentFormats = colorVkFormats.data();
        if (hasDepthStencil)
        {
            vk::Format dsVkFmt = static_cast<vk::Format>(pwgpu::ToVkFormat(descriptor->depthStencil->format));
            if (pwgpu::HasDepthAspect(descriptor->depthStencil->format))
                renderingInfo.depthAttachmentFormat = dsVkFmt;
            if (pwgpu::HasStencilAspect(descriptor->depthStencil->format))
                renderingInfo.stencilAttachmentFormat = dsVkFmt;
        }

        vk::GraphicsPipelineCreateInfo pipeInfo{};
        pipeInfo.pNext = &renderingInfo;
        pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipeInfo.pStages = stages.data();
        pipeInfo.pVertexInputState = &vertexInputState;
        pipeInfo.pInputAssemblyState = &inputAssembly;
        pipeInfo.pViewportState = &viewportState;
        pipeInfo.pRasterizationState = &rasterState;
        pipeInfo.pMultisampleState = &multisampleState;
        pipeInfo.pDepthStencilState = hasDepthStencil ? &depthStencilState : nullptr;
        pipeInfo.pColorBlendState = &colorBlendState;
        pipeInfo.pDynamicState = &dynamicState;
        pipeInfo.layout = pipeLayout->vkLayout;
        pipeInfo.renderPass = nullptr;
        pipeInfo.subpass = 0;
        pipeInfo.basePipelineHandle = nullptr;
        pipeInfo.basePipelineIndex = -1;

        vk::Pipeline vkPipeline;
        try
        {
            auto result = vkDev.createGraphicsPipeline(nullptr, pipeInfo);
            if (result.result != vk::Result::eSuccess)
            {
                for (auto m : tempModules)
                    vkDev.destroyShaderModule(m);
                device->reportError(WGPUErrorType_Internal,
                                    pwgpu::ToStringView("createRenderPipeline: vkCreateGraphicsPipelines failed"));
                return MakeInvalidRenderPipeline(device, labelStr);
            }
            vkPipeline = result.value;
        }
        catch (const std::exception &e)
        {
            for (auto m : tempModules)
                vkDev.destroyShaderModule(m);
            std::string msg = std::string("createRenderPipeline: vkCreateGraphicsPipelines threw: ") + e.what();
            device->reportError(WGPUErrorType_Internal, pwgpu::ToStringView(msg));
            return MakeInvalidRenderPipeline(device, labelStr);
        }

        for (auto m : tempModules)
            vkDev.destroyShaderModule(m);

        auto *rp = new WGPURenderPipelineImpl();
        rp->device = device;
        if (labelStr)
            rp->label = labelStr;
        rp->vkPipeline = vkPipeline;
        rp->layout = pipeLayout;
        rp->sampleCount = msCount;
        rp->vertexEntryPoint = vertEntry;
        if (hasFragment)
            rp->fragmentEntryPoint = fragEntry;

        if (hasFragment)
        {
            for (size_t i = 0; i < descriptor->fragment->targetCount; ++i)
                rp->colorFormats.push_back(descriptor->fragment->targets[i].format);
        }
        if (hasDepthStencil)
            rp->depthStencilFormat = descriptor->depthStencil->format;

        // Compute writesDepth/writesStencil (§10.3.1)
        if (hasDepthStencil)
        {
            const auto &ds = *descriptor->depthStencil;
            rp->writesDepth = (ds.depthWriteEnabled == WGPUOptionalBool_True);

            if (ds.stencilWriteMask != 0)
            {
                auto primCull = cullMode;

                auto stencilWrites = [](const WGPUStencilFaceState &face)
                {
                    auto p = face.passOp;
                    if (p == WGPUStencilOperation(0))
                        p = WGPUStencilOperation_Keep;
                    auto df = face.depthFailOp;
                    if (df == WGPUStencilOperation(0))
                        df = WGPUStencilOperation_Keep;
                    auto f = face.failOp;
                    if (f == WGPUStencilOperation(0))
                        f = WGPUStencilOperation_Keep;
                    return p != WGPUStencilOperation_Keep || df != WGPUStencilOperation_Keep || f != WGPUStencilOperation_Keep;
                };

                if (primCull != WGPUCullMode_Front && stencilWrites(ds.stencilFront))
                    rp->writesStencil = true;
                if (primCull != WGPUCullMode_Back && stencilWrites(ds.stencilBack))
                    rp->writesStencil = true;
            }
        }

        wgpuDeviceAddRef(device);
        wgpuPipelineLayoutAddRef(pipeLayout);

        return rp;
    }

    WGPUFuture wgpuDeviceCreateRenderPipelineAsync(WGPUDevice device,
                                                   WGPURenderPipelineDescriptor const *descriptor,
                                                   WGPUCreateRenderPipelineAsyncCallbackInfo callbackInfo)
    {
        WGPURenderPipeline rp = wgpuDeviceCreateRenderPipeline(device, descriptor);
        bool valid = rp && !rp->invalid;
        WGPUInstanceImpl *inst = device ? device->instance : nullptr;
        if (!inst || !callbackInfo.callback)
            return inst ? inst->futures.NextId() : WGPUFuture{0};
        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        auto status = valid ? WGPUCreatePipelineAsyncStatus_Success : WGPUCreatePipelineAsyncStatus_ValidationError;
        return inst->futures.TrackEvent(callbackInfo.mode,
                                        [cb, u1, u2, status, rp]()
                                        {
                                            cb(status, rp, {nullptr, 0}, u1, u2);
                                        });
    }

    WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, WGPUCommandEncoderDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateCommandEncoder", false))
            return nullptr;
        auto *enc = new WGPUCommandEncoderImpl();
        enc->device = device;
        if (descriptor && descriptor->label.data)
            enc->label = pwgpu::ToString(descriptor->label);

        // Acquire a pe::CommandBuffer from the device's queue and begin recording.
        pe::CommandBuffer *cmd = device->peQueue->AcquireCommandBuffer();
        cmd->Begin();
        enc->cmd = cmd;
        return enc;
    }

    WGPURenderBundleEncoder wgpuDeviceCreateRenderBundleEncoder(WGPUDevice device, WGPURenderBundleEncoderDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateRenderBundleEncoder", true))
            return nullptr;

        auto makeInvalid = [&]()
        {
            auto *rbe = new WGPURenderBundleEncoderImpl();
            rbe->device = device;
            rbe->invalid = true;
            device->refCount.fetch_add(1, std::memory_order_relaxed);
            return rbe;
        };

        if (descriptor->colorFormatCount > 0 && !descriptor->colorFormats)
        {
            PE_WARN("[WebGPU] createRenderBundleEncoder: colorFormatCount > 0 but colorFormats is null");
            return makeInvalid();
        }

        if (descriptor->colorFormatCount > device->limits.maxColorAttachments)
        {
            PE_WARN("[WebGPU] createRenderBundleEncoder: colorFormatCount %zu > maxColorAttachments %u",
                    descriptor->colorFormatCount, device->limits.maxColorAttachments);
            return makeInvalid();
        }

        bool hasAnyAttachment = false;
        uint32_t colorBytesPerSample = 0;
        for (size_t i = 0; i < descriptor->colorFormatCount; ++i)
        {
            WGPUTextureFormat cf = descriptor->colorFormats[i];
            if (cf == WGPUTextureFormat_Undefined)
                continue;
            hasAnyAttachment = true;

            if (pwgpu::IsDepthStencilFormat(cf) || !pwgpu::IsRenderableFormat(cf))
            {
                PE_WARN("[WebGPU] createRenderBundleEncoder: colorFormats[%zu] is not color-renderable", i);
                return makeInvalid();
            }

            colorBytesPerSample += pwgpu::FormatBytesPerSample(cf);
        }

        if (colorBytesPerSample > device->limits.maxColorAttachmentBytesPerSample)
        {
            PE_WARN("[WebGPU] createRenderBundleEncoder: colorAttachmentBytesPerSample %u > limit %u",
                    colorBytesPerSample, device->limits.maxColorAttachmentBytesPerSample);
            return makeInvalid();
        }

        if (descriptor->depthStencilFormat != WGPUTextureFormat_Undefined)
        {
            hasAnyAttachment = true;
            if (!pwgpu::IsDepthStencilFormat(descriptor->depthStencilFormat))
            {
                PE_WARN("[WebGPU] createRenderBundleEncoder: depthStencilFormat is not a depth-stencil format");
                return makeInvalid();
            }
        }

        if (!hasAnyAttachment)
        {
            PE_WARN("[WebGPU] createRenderBundleEncoder: must have at least one color or depth-stencil attachment");
            return makeInvalid();
        }

        auto *rbe = new WGPURenderBundleEncoderImpl();
        rbe->device = device;
        device->refCount.fetch_add(1, std::memory_order_relaxed);

        if (descriptor->label.data)
            rbe->label = pwgpu::ToString(descriptor->label);

        rbe->sampleCount = descriptor->sampleCount ? descriptor->sampleCount : 1;
        rbe->depthStencilFormat = descriptor->depthStencilFormat;
        rbe->depthReadOnly = descriptor->depthReadOnly;
        rbe->stencilReadOnly = descriptor->stencilReadOnly;

        for (size_t i = 0; i < descriptor->colorFormatCount; ++i)
            rbe->colorFormats.push_back(descriptor->colorFormats[i]);

        return rbe;
    }

    WGPUQuerySet wgpuDeviceCreateQuerySet(WGPUDevice device, WGPUQuerySetDescriptor const *descriptor)
    {
        if (!DeviceCanCreate(device, descriptor, "wgpuDeviceCreateQuerySet", true))
            return nullptr;

        // Validate count > 0
        if (descriptor->count == 0)
        {
            PE_WARN("[WebGPU] wgpuDeviceCreateQuerySet: count must be > 0");
            return nullptr;
        }

        // Map WGPUQueryType → VkQueryType
        vk::QueryType vkType;
        switch (descriptor->type)
        {
        case WGPUQueryType_Occlusion:
            vkType = vk::QueryType::eOcclusion;
            break;
        case WGPUQueryType_Timestamp:
            vkType = vk::QueryType::eTimestamp;
            break;
        default:
            PE_WARN("[WebGPU] wgpuDeviceCreateQuerySet: unsupported query type %d", static_cast<int>(descriptor->type));
            return nullptr;
        }

        vk::QueryPoolCreateInfo ci{};
        ci.queryType = vkType;
        ci.queryCount = descriptor->count;

        vk::QueryPool pool = device->rhi->GetDevice().createQueryPool(ci);
        if (!pool)
        {
            PE_WARN("[WebGPU] wgpuDeviceCreateQuerySet: VkQueryPool creation failed");
            return nullptr;
        }

        // Reset all queries so they are in a known state
        device->rhi->GetDevice().resetQueryPool(pool, 0, descriptor->count);

        auto *qs = new WGPUQuerySetImpl();
        qs->device = device;
        wgpuDeviceAddRef(device);
        qs->type = descriptor->type;
        qs->count = descriptor->count;
        qs->queryPool = static_cast<VkQueryPool>(pool);
        if (descriptor->label.data)
            qs->label = pwgpu::ToString(descriptor->label);
        return qs;
    }

} // extern "C"
