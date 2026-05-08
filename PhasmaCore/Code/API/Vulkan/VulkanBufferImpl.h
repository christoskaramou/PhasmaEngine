#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Buffer_Internal.h"

namespace pe
{
    class CommandBuffer;

    struct VulkanBufferImpl final : public Buffer::Impl
    {
        VulkanBufferImpl(Buffer *owner, const BufferDesc &desc);
        ~VulkanBufferImpl() override;

        void *Map() override;
        void Unmap() override;
        void Flush(size_t size, size_t offset) const override;
        uint64_t GetDeviceAddress() const override;
        void CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset) override;

        // Friend Buffer in Buffer.h grants access to Buffer's private m_impl.
        static VulkanBufferImpl *From(Buffer *buf) { return static_cast<VulkanBufferImpl *>(buf->m_impl); }
        static const VulkanBufferImpl *From(const Buffer *buf) { return static_cast<const VulkanBufferImpl *>(buf->m_impl); }

        Buffer *m_owner;
        vk::Buffer m_buffer;
        VmaAllocation m_allocation{};
        VmaAllocationInfo m_allocationInfo{};
    };

    // Engine-private Vulkan seam accessor for PhasmaWebGPU. Will move to
    // API/Vulkan/RHI_Vulkan.h when Phase 0 step 13 lands. Returns null vk::Buffer
    // for null input so callers can pass through unchecked pointers.
    inline vk::Buffer GetVulkanBuffer(Buffer *buffer)
    {
        return buffer ? VulkanBufferImpl::From(buffer)->m_buffer : vk::Buffer{};
    }
} // namespace pe
