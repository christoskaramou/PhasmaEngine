#include "API/WebGPU/WebGpuRhiImpl.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/WebGPU/WebGpuBlit.h"
#include "API/WebGPU/WebGpuDescriptorImpl.h"

namespace pe
{
    namespace
    {
        constexpr uint64_t kStartupWaitNs = 10'000'000'000ull;

        bool WaitFor(WGPUInstance instance, WGPUFuture future, const char *what)
        {
            WGPUFutureWaitInfo info{};
            info.future = future;
            const WGPUWaitStatus status = wgpuInstanceWaitAny(instance, 1, &info, kStartupWaitNs);
            if (status != WGPUWaitStatus_Success || !info.completed)
            {
                PE_WARN("WebGPU: waiting for the %s failed (status %d)", what, static_cast<int>(status));
                return false;
            }

            return true;
        }

        std::string ToString(WGPUStringView text)
        {
            if (!text.data)
                return {};

            return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
        }
    } // namespace

    WebGpuRhiImpl *WebGpuRhiImpl::Get()
    {
        return static_cast<WebGpuRhiImpl *>(RHII.GetImpl());
    }

    bool WebGpuRhiImpl::Init(SDL_Window *)
    {
        WGPUInstanceFeatureName instanceFeatures[] = {WGPUInstanceFeatureName_TimedWaitAny};
        WGPUInstanceDescriptor instanceDesc{};
        instanceDesc.requiredFeatureCount = 1;
        instanceDesc.requiredFeatures = instanceFeatures;
        m_instance = wgpuCreateInstance(&instanceDesc);
        if (!m_instance)
            return false;

        WGPURequestAdapterOptions adapterOptions{};
        adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo adapterCb{};
        adapterCb.mode = WGPUCallbackMode_WaitAnyOnly;
        adapterCb.userdata1 = this;
        adapterCb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *user, void *)
        {
            if (status != WGPURequestAdapterStatus_Success)
                PE_WARN("WebGPU: no adapter: %s", ToString(message).c_str());

            static_cast<WebGpuRhiImpl *>(user)->m_adapter = adapter;
        };

        if (!WaitFor(m_instance, wgpuInstanceRequestAdapter(m_instance, &adapterOptions, adapterCb), "adapter") || !m_adapter)
            return false;

        // An indirect draw with a non-zero firstInstance is silently skipped without this feature (spec risk 9).
        if (!wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_IndirectFirstInstance))
        {
            PE_WARN("WebGPU: the adapter lacks indirect-first-instance, which the renderer requires");
            return false;
        }

        std::vector<WGPUFeatureName> features{WGPUFeatureName_IndirectFirstInstance};
        m_float32Filterable = wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_Float32Filterable);
        if (m_float32Filterable)
            features.push_back(WGPUFeatureName_Float32Filterable);

        m_textureCompressionBC = wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_TextureCompressionBC);
        if (m_textureCompressionBC)
            features.push_back(WGPUFeatureName_TextureCompressionBC);

        // The defaults are below what the always-on passes need (G-buffer bytes per sample, sampled textures and
        // storage buffers per stage; P0 census), so request everything the adapter offers.
        WGPULimits adapterLimits = WGPU_LIMITS_INIT;
        if (wgpuAdapterGetLimits(m_adapter, &adapterLimits) != WGPUStatus_Success)
            return false;

        WGPUDeviceDescriptor deviceDesc{};
        deviceDesc.requiredFeatureCount = features.size();
        deviceDesc.requiredFeatures = features.data();
        deviceDesc.requiredLimits = &adapterLimits;
        deviceDesc.uncapturedErrorCallbackInfo.userdata1 = this;
        deviceDesc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const *, WGPUErrorType type, WGPUStringView message, void *user, void *)
        {
            static_cast<WebGpuRhiImpl *>(user)->m_errorCount++;
            PE_WARN("WebGPU error %d: %s", static_cast<int>(type), ToString(message).c_str());
        };
        deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceDesc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const *, WGPUDeviceLostReason reason, WGPUStringView message, void *, void *)
        {
            if (reason != WGPUDeviceLostReason_Destroyed)
                PE_WARN("WebGPU device lost (%d): %s", static_cast<int>(reason), ToString(message).c_str());
        };

        WGPURequestDeviceCallbackInfo deviceCb{};
        deviceCb.mode = WGPUCallbackMode_WaitAnyOnly;
        deviceCb.userdata1 = this;
        deviceCb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *user, void *)
        {
            if (status != WGPURequestDeviceStatus_Success)
                PE_WARN("WebGPU: no device: %s", ToString(message).c_str());

            static_cast<WebGpuRhiImpl *>(user)->m_device = device;
        };

        if (!WaitFor(m_instance, wgpuAdapterRequestDevice(m_adapter, &deviceDesc, deviceCb), "device") || !m_device)
            return false;

        m_queue = wgpuDeviceGetQueue(m_device);
        if (wgpuDeviceGetLimits(m_device, &m_limits) != WGPUStatus_Success)
            return false;

        PE_INFO("WebGPU RHI: device ready (float32-filterable %d, texture-compression-bc %d, %u color bytes per sample, "
                "%u sampled textures and %u storage buffers per stage)",
                m_float32Filterable ? 1 : 0,
                m_textureCompressionBC ? 1 : 0,
                m_limits.maxColorAttachmentBytesPerSample,
                m_limits.maxSampledTexturesPerShaderStage,
                m_limits.maxStorageBuffersPerShaderStage);
        return true;
    }

    WGPUBuffer WebGpuRhiImpl::DummyBuffer()
    {
        if (!m_dummyBuffer)
        {
            WGPUBufferDescriptor desc{};
            desc.label = {"WebGPU_dummy_buffer", WGPU_STRLEN};
            desc.size = 256;
            desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage;
            m_dummyBuffer = wgpuDeviceCreateBuffer(m_device, &desc);
        }

        return m_dummyBuffer;
    }

    WGPUTextureView WebGpuRhiImpl::CachedDummyView(WGPUTextureViewDimension dimension,
                                                   WGPUTextureFormat format,
                                                   bool storage,
                                                   bool multisampled)
    {
        const uint64_t key = (static_cast<uint64_t>(format) << 16) | (static_cast<uint64_t>(dimension) << 2) |
                             (storage ? 2u : 0u) | (multisampled ? 1u : 0u);
        DummyTexture &dummy = m_dummyTextures[key];
        if (dummy.view)
            return dummy.view;

        const bool cube = dimension == WGPUTextureViewDimension_Cube || dimension == WGPUTextureViewDimension_CubeArray;
        const uint32_t layers = cube ? 6 : 1;

        WGPUTextureDescriptor desc{};
        desc.label = {"WebGPU_dummy_texture", WGPU_STRLEN};
        desc.usage = storage ? WGPUTextureUsage_StorageBinding : WGPUTextureUsage_TextureBinding;
        if (multisampled)
            desc.usage |= WGPUTextureUsage_RenderAttachment; // WebGPU allows multisampling only on render targets

        desc.dimension = WGPUTextureDimension_2D;
        if (dimension == WGPUTextureViewDimension_1D)
            desc.dimension = WGPUTextureDimension_1D;
        else if (dimension == WGPUTextureViewDimension_3D)
            desc.dimension = WGPUTextureDimension_3D;

        desc.size = {1, 1, layers};
        desc.format = format;
        desc.mipLevelCount = 1;
        desc.sampleCount = multisampled ? 4 : 1;
        dummy.texture = wgpuDeviceCreateTexture(m_device, &desc);

        WGPUTextureViewDescriptor view{};
        view.format = format;
        view.dimension = dimension;
        view.mipLevelCount = 1;
        view.arrayLayerCount = layers;
        view.aspect = WGPUTextureAspect_All;
        dummy.view = wgpuTextureCreateView(dummy.texture, &view);
        return dummy.view;
    }

    WGPUTextureView WebGpuRhiImpl::DummyView(WGPUTextureViewDimension dimension,
                                             WGPUTextureSampleType sampleType,
                                             bool multisampled)
    {
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm; // serves float and unfilterable-float entries
        if (sampleType == WGPUTextureSampleType_Depth)
            format = WGPUTextureFormat_Depth32Float;
        else if (sampleType == WGPUTextureSampleType_Uint)
            format = WGPUTextureFormat_R32Uint;
        else if (sampleType == WGPUTextureSampleType_Sint)
            format = WGPUTextureFormat_R32Sint;

        return CachedDummyView(dimension, format, false, multisampled);
    }

    WGPUTextureView WebGpuRhiImpl::DummyStorageView(WGPUTextureViewDimension dimension, WGPUTextureFormat format)
    {
        return CachedDummyView(dimension, format, true, false);
    }

    WGPUSampler WebGpuRhiImpl::DummySampler(WGPUSamplerBindingType type)
    {
        int i = 0;
        if (type == WGPUSamplerBindingType_NonFiltering)
            i = 1;
        else if (type == WGPUSamplerBindingType_Comparison)
            i = 2;

        if (!m_dummySamplers[i])
        {
            const bool filtering = type != WGPUSamplerBindingType_NonFiltering;
            WGPUSamplerDescriptor desc{};
            desc.addressModeU = WGPUAddressMode_ClampToEdge;
            desc.addressModeV = WGPUAddressMode_ClampToEdge;
            desc.addressModeW = WGPUAddressMode_ClampToEdge;
            desc.magFilter = filtering ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
            desc.minFilter = desc.magFilter;
            desc.mipmapFilter = filtering ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
            desc.lodMaxClamp = 32.0f;
            desc.maxAnisotropy = 1;
            if (type == WGPUSamplerBindingType_Comparison)
                desc.compare = WGPUCompareFunction_LessEqual;

            m_dummySamplers[i] = wgpuDeviceCreateSampler(m_device, &desc);
        }

        return m_dummySamplers[i];
    }

    WGPUBindGroup WebGpuRhiImpl::EmptyGroup()
    {
        if (!m_emptyGroup)
        {
            // The same empty layout pipelines put at a set index the pass has no descriptor for.
            DescriptorLayout *empty = DescriptorLayout::GetOrCreate({}, PE_SHADER_STAGE_ALL);
            WGPUBindGroupDescriptor desc{};
            desc.layout = WebGpuDescriptorLayoutImpl::From(empty)->m_layout;
            m_emptyGroup = wgpuDeviceCreateBindGroup(m_device, &desc);
        }

        return m_emptyGroup;
    }

    WGPUBindGroupLayout WebGpuRhiImpl::PushLayout()
    {
        if (!m_pushLayout)
        {
            WGPUBindGroupLayoutEntry entry{};
            entry.binding = 0;
            entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
            entry.buffer.type = WGPUBufferBindingType_Uniform;
            entry.buffer.hasDynamicOffset = true;

            WGPUBindGroupLayoutDescriptor desc{};
            desc.label = {"WebGPU_push_constants", WGPU_STRLEN};
            desc.entryCount = 1;
            desc.entries = &entry;
            m_pushLayout = wgpuDeviceCreateBindGroupLayout(m_device, &desc);
        }

        return m_pushLayout;
    }

    WGPUBindGroup WebGpuRhiImpl::PushGroup()
    {
        if (m_pushGroup)
            return m_pushGroup;

        // ponytail: a fixed ring of 4096 slots per frame (1 MiB at Chrome's 256-byte alignment); WritePush fails past
        // it. If a frame needs more, chain a second ring buffer and bind group instead of growing this one.
        constexpr uint32_t kSlots = 4096;
        const uint32_t align = std::max<uint32_t>(m_limits.minUniformBufferOffsetAlignment, 1u);
        m_pushStride = (kPushBytes + align - 1) / align * align;
        m_pushShadow.assign(static_cast<size_t>(m_pushStride) * kSlots, 0);

        WGPUBufferDescriptor bufferDesc{};
        bufferDesc.label = {"WebGPU_push_ring", WGPU_STRLEN};
        bufferDesc.size = m_pushShadow.size();
        bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        m_pushBuffer = wgpuDeviceCreateBuffer(m_device, &bufferDesc);

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = m_pushBuffer;
        entry.size = kPushBytes;

        WGPUBindGroupDescriptor desc{};
        desc.label = {"WebGPU_push_constants", WGPU_STRLEN};
        desc.layout = PushLayout();
        desc.entryCount = 1;
        desc.entries = &entry;
        m_pushGroup = wgpuDeviceCreateBindGroup(m_device, &desc);
        return m_pushGroup;
    }

    uint32_t WebGpuRhiImpl::WritePush(const void *data)
    {
        PushGroup();
        if (m_pushFrame != RHII.GetFrameCounter())
        {
            m_pushFrame = RHII.GetFrameCounter();
            m_pushUsed = 0;
            m_pushUploaded = 0;
        }

        PE_ERROR_IF(m_pushUsed + m_pushStride > m_pushShadow.size(),
                    "WebGPU: more than %zu push-constant writes in one frame",
                    m_pushShadow.size() / m_pushStride);

        const uint32_t offset = m_pushUsed;
        std::memcpy(m_pushShadow.data() + offset, data, kPushBytes);
        m_pushUsed += m_pushStride;
        return offset;
    }

    void WebGpuRhiImpl::UploadPush()
    {
        if (m_pushUsed == m_pushUploaded)
            return;

        const uint32_t size = m_pushUsed - m_pushUploaded;
        wgpuQueueWriteBuffer(m_queue, m_pushBuffer, m_pushUploaded, m_pushShadow.data() + m_pushUploaded, size);
        m_pushUploaded = m_pushUsed;
    }

    void WebGpuRhiImpl::Shutdown()
    {
        WebGpuBlitShutdown();
        for (WGPUSampler sampler : m_dummySamplers)
        {
            if (sampler)
                wgpuSamplerRelease(sampler);
        }

        for (auto &[key, dummy] : m_dummyTextures)
        {
            wgpuTextureViewRelease(dummy.view);
            wgpuTextureRelease(dummy.texture);
        }

        m_dummyTextures.clear();
        if (m_dummyBuffer)
            wgpuBufferRelease(m_dummyBuffer);

        if (m_emptyGroup)
            wgpuBindGroupRelease(m_emptyGroup);

        if (m_pushGroup)
            wgpuBindGroupRelease(m_pushGroup);

        if (m_pushBuffer)
            wgpuBufferRelease(m_pushBuffer);

        if (m_pushLayout)
            wgpuBindGroupLayoutRelease(m_pushLayout);

        if (m_queue)
            wgpuQueueRelease(m_queue);

        if (m_device)
        {
            wgpuDeviceDestroy(m_device);
            wgpuDeviceRelease(m_device);
        }

        if (m_adapter)
            wgpuAdapterRelease(m_adapter);

        if (m_instance)
            wgpuInstanceRelease(m_instance);

        m_queue = nullptr;
        m_device = nullptr;
        m_adapter = nullptr;
        m_instance = nullptr;
    }

    void WebGpuRhiImpl::FillLimits(GpuLimits &out) const
    {
        const auto clamp32 = [](uint64_t v)
        {
            return static_cast<uint32_t>(std::min<uint64_t>(v, UINT32_MAX));
        };

        out.maxTextureDimension1D = m_limits.maxTextureDimension1D;
        out.maxTextureDimension2D = m_limits.maxTextureDimension2D;
        out.maxTextureDimension3D = m_limits.maxTextureDimension3D;
        out.maxTextureArrayLayers = m_limits.maxTextureArrayLayers;
        out.maxBindGroups = m_limits.maxBindGroups;
        out.maxDynamicUniformBuffersPerPipelineLayout = m_limits.maxDynamicUniformBuffersPerPipelineLayout;
        out.maxDynamicStorageBuffersPerPipelineLayout = m_limits.maxDynamicStorageBuffersPerPipelineLayout;
        out.maxSampledTexturesPerShaderStage = m_limits.maxSampledTexturesPerShaderStage;
        out.maxSamplersPerShaderStage = m_limits.maxSamplersPerShaderStage;
        out.maxStorageBuffersPerShaderStage = m_limits.maxStorageBuffersPerShaderStage;
        out.maxStorageTexturesPerShaderStage = m_limits.maxStorageTexturesPerShaderStage;
        out.maxUniformBuffersPerShaderStage = m_limits.maxUniformBuffersPerShaderStage;
        out.maxUniformBufferBindingSize = clamp32(m_limits.maxUniformBufferBindingSize);
        out.maxStorageBufferBindingSize = clamp32(m_limits.maxStorageBufferBindingSize);
        out.minUniformBufferOffsetAlignment = m_limits.minUniformBufferOffsetAlignment;
        out.minStorageBufferOffsetAlignment = m_limits.minStorageBufferOffsetAlignment;
        out.maxVertexBuffers = m_limits.maxVertexBuffers;
        out.maxBufferSize = m_limits.maxBufferSize;
        out.maxVertexAttributes = m_limits.maxVertexAttributes;
        out.maxVertexBufferArrayStride = m_limits.maxVertexBufferArrayStride;
        out.maxInterStageShaderVariables = m_limits.maxInterStageShaderVariables;
        out.maxColorAttachments = m_limits.maxColorAttachments;
        out.maxComputeWorkgroupStorageSize = m_limits.maxComputeWorkgroupStorageSize;
        out.maxComputeInvocationsPerWorkgroup = m_limits.maxComputeInvocationsPerWorkgroup;
        out.maxComputeWorkgroupSizeX = m_limits.maxComputeWorkgroupSizeX;
        out.maxComputeWorkgroupSizeY = m_limits.maxComputeWorkgroupSizeY;
        out.maxComputeWorkgroupSizeZ = m_limits.maxComputeWorkgroupSizeZ;
        out.maxComputeWorkgroupsPerDimension = m_limits.maxComputeWorkgroupsPerDimension;
    }
} // namespace pe
