#pragma once

#include "API/Buffer.h"

namespace pe
{
    class CommandBuffer;

    struct Buffer::Impl : public NoCopy
    {
        virtual ~Impl() = default;
        virtual void *Map() = 0;
        virtual void Unmap() = 0;
        virtual void Flush(size_t size, size_t offset) const = 0;
        virtual uint64_t GetDeviceAddress() const = 0;
        virtual void CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset) = 0;
    };

    // Phase 0 backend factory + free-function dispatch. In Phase 0 these route
    // unconditionally to the Vulkan implementation; Phase 1 will dispatch on the
    // active RHI backend selected at RHI::Init(). Defined in the active backend's
    // translation unit (currently Vulkan/VulkanBufferImpl.cpp).
    Buffer::Impl *CreateBufferImpl(Buffer *owner, const BufferDesc &desc);
    void Buffer_Barrier_Backend(CommandBuffer *cmd, const BufferBarrierInfo &info);
    void Buffer_Barriers_Backend(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos);
} // namespace pe
