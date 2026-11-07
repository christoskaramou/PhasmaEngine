#pragma once

#include "API/RHI_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // Browser WebGPU device (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md, decisions 4 and 5).
    // Startup waits are synchronous through wgpuInstanceWaitAny, which needs the player's -sJSPI link.
    struct WebGpuRhiImpl final : public RHI::Impl
    {
        bool Init(SDL_Window *window) override;
        void Shutdown() override;
        void WaitDeviceIdle() override {} // one ordered queue; destroy after submit is safe (WebGPU 5.1.4)
        void NextFrame() override {}
        GpuMemorySnapshot GetGpuMemorySnapshot() override { return {}; }

        static WebGpuRhiImpl *Get();
        void FillLimits(GpuLimits &out) const;

        // Resources for bind group slots nothing was bound to: every layout entry must be bound (decision 5). Each view
        // matches its entry's view dimension and sample type, or storage format, as the binding sidecar sets them.
        WGPUBuffer DummyBuffer();
        WGPUTextureView DummyView(WGPUTextureViewDimension dimension, WGPUTextureSampleType sampleType, bool multisampled);
        WGPUTextureView DummyStorageView(WGPUTextureViewDimension dimension, WGPUTextureFormat format);
        WGPUSampler DummySampler(WGPUSamplerBindingType type);

        // A bind group for a set index the pipeline layout fills with an empty layout: WebGPU draws need every group.
        WGPUBindGroup EmptyGroup();

        // Push constants (decision 8): a uniform ring with one minUniformBufferOffsetAlignment slot per PushConstants
        // call, bound at group 3 with a dynamic offset. The queue uploads new slots before each submit, and the ring
        // restarts each frame: writeBuffer runs on the queue timeline after the earlier submits that read old slots.
        static constexpr uint32_t kPushGroup = 3;
        static constexpr uint32_t kPushBytes = 128;
        WGPUBindGroupLayout PushLayout();
        WGPUBindGroup PushGroup();
        uint32_t WritePush(const void *data); // returns the slot's dynamic offset
        void UploadPush();

        WGPUInstance m_instance = nullptr;
        WGPUAdapter m_adapter = nullptr;
        WGPUDevice m_device = nullptr;
        WGPUQueue m_queue = nullptr;
        WGPULimits m_limits = WGPU_LIMITS_INIT;
        bool m_float32Filterable = false;
        bool m_textureCompressionBC = false;
        uint32_t m_errorCount = 0; // uncaptured WebGPU errors, logged as they arrive

        struct DummyTexture
        {
            WGPUTexture texture = nullptr;
            WGPUTextureView view = nullptr;
        };

        WGPUBuffer m_dummyBuffer = nullptr;
        std::unordered_map<uint64_t, DummyTexture> m_dummyTextures; // by format, dimension, storage, multisampled
        WGPUSampler m_dummySamplers[3] = {};                        // filtering, non-filtering, comparison
        WGPUBindGroup m_emptyGroup = nullptr;

        WGPUBindGroupLayout m_pushLayout = nullptr;
        WGPUBuffer m_pushBuffer = nullptr;
        WGPUBindGroup m_pushGroup = nullptr;
        std::vector<uint8_t> m_pushShadow;
        uint32_t m_pushStride = 0;
        uint32_t m_pushUsed = 0;     // bytes of the ring written this frame
        uint32_t m_pushUploaded = 0; // bytes of those already uploaded
        uint32_t m_pushFrame = UINT32_MAX;

    private:
        WGPUTextureView CachedDummyView(WGPUTextureViewDimension dimension,
                                        WGPUTextureFormat format,
                                        bool storage,
                                        bool multisampled);
    };
} // namespace pe
