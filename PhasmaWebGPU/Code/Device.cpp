#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "FormatMap.h"
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
        if (descriptor->viewFormatCount > 0 && descriptor->viewFormats)
            ci.flags |= vk::ImageCreateFlagBits::eMutableFormat;

        const std::string peName = tex->label.empty() ? std::string("wgpu_texture") : tex->label;
        try
        {
            tex->image = pe::Image::Create(ci, peName);
        }
        catch (...)
        {
            tex->image = nullptr;
        }

        if (!tex->image)
        {
            std::string msg = "wgpuDeviceCreateTexture: backing allocation failed";
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
