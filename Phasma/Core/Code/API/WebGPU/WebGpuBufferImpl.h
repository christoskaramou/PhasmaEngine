#pragma once

#include "API/Buffer_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // WebGPU has no persistently mapped writable buffers, so a host-visible buffer is a WGPUBuffer plus a CPU shadow
    // that Map and Data() return. Spec decision 5, host-visible row:
    // (1) Flush uploads its range; Unmap uploads only what no Flush uploaded since the last Map.
    // (2) Before each submit, the whole shadow of every mapped, non-TRANSFER_SRC buffer that submit references uploads
    //     (WebGpuCommandBufferImpl records the references; WebGpuQueueImpl uploads).
    // (3) A host-visible buffer is never GPU-written, or its shadow would overwrite the GPU's result.
    struct WebGpuBufferImpl final : public Buffer::Impl
    {
        WebGpuBufferImpl(Buffer *owner, const BufferDesc &desc);
        ~WebGpuBufferImpl() override;

        void *Map() override;
        void Unmap() override;
        void Flush(size_t size, size_t offset) const override;
        uint64_t GetDeviceAddress() const override;
        void CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset) override;

        static WebGpuBufferImpl *From(Buffer *buf) { return static_cast<WebGpuBufferImpl *>(buf->m_impl); }

        bool IsHostVisible() const { return m_hostVisible; }
        // Rule 2 candidate: host-visible and not staging. Staging (TRANSFER_SRC) never uploads whole: up to 128 MiB
        // of it stays mapped.
        bool UploadsWhenReferenced() const { return m_hostVisible && !(m_owner->Usage() & PE_BUFFER_USAGE_TRANSFER_SRC); }
        bool IsMapped() const { return m_mapped; }
        void UploadShadow() const;

        // Bytes sent with wgpuQueueWriteBuffer since startup, for the per-frame upload measurement (spec risk 6).
        static uint64_t UploadedBytes() { return s_uploadedBytes; }

        Buffer *m_owner = nullptr;
        WGPUBuffer m_buffer = nullptr;
        size_t m_paddedSize = 0; // the buffer size rounded up to 4 bytes, so 4-byte-rounded writes never overrun

    private:
        void Upload(size_t begin, size_t end) const;

        std::vector<uint8_t> m_shadow;
        // Ranges [begin, end) uploaded by Flush since the last Map, sorted and merged.
        mutable std::vector<std::pair<size_t, size_t>> m_flushed;
        bool m_hostVisible = false;
        bool m_mapped = false;

        static inline uint64_t s_uploadedBytes = 0;
    };
} // namespace pe
