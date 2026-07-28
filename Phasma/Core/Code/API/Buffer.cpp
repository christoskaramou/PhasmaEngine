#include "API/Buffer_Internal.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/StagingManager.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#endif

namespace pe
{
    Buffer::Impl *CreateBufferImpl(Buffer *owner, const BufferDesc &desc)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanBufferImpl(owner, desc);
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            return new Dx12BufferImpl(owner, desc);
#endif
        default:
            PE_ERROR("CreateBufferImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }

    void Buffer_Barrier_Backend(CommandBuffer *cmd, const BufferBarrierInfo &info)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!cmd, "Buffer::Barrier: no command buffer specified");
            Dx12CommandBufferImpl::From(cmd)->BufferBarrier(info);
            return;
        }
#endif
        Buffer_Barrier_Vulkan(cmd, info);
    }

    void Buffer_Barriers_Backend(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!cmd, "Buffer::Barriers: no command buffer specified");
            Dx12CommandBufferImpl::From(cmd)->BufferBarriers(infos);
            return;
        }
#endif
        Buffer_Barriers_Vulkan(cmd, infos);
    }

    Buffer *Buffer::Create(const BufferDesc &desc)
    {
        Buffer *buf = new Buffer(desc);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Buffer), reinterpret_cast<void *>(buf));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Buffer created (Handle: %p)", reinterpret_cast<void *>(buf));
#endif
#endif
        return buf;
    }

    void Buffer::Destroy(Buffer *&buf)
    {
        if (buf)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Buffer destroyed (Handle: %p)", reinterpret_cast<void *>(buf));
#endif
            delete buf;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Buffer), reinterpret_cast<void *>(buf));
#endif
            buf = nullptr;
        }
    }

    std::vector<Buffer *> Buffer::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Buffer));
        std::vector<Buffer *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Buffer *>(p));
        return out;
#else
        return {};
#endif
    }

    Buffer::Buffer(const BufferDesc &desc)
        : m_size{desc.size},
          m_data{nullptr},
          m_usage{desc.usage},
          m_memoryUsage{desc.memoryUsage},
          m_name{desc.name}
    {
        PE_ERROR_IF((m_usage & PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS) &&
                        !RHII.GetCaps().bufferDeviceAddress,
                    "Buffer '%s' requests a device address on unsupported hardware",
                    m_name.c_str());

        if (m_usage & PE_BUFFER_USAGE_UNIFORM_BUFFER)
        {
            m_size = RHII.AlignUniform(m_size);
            PE_ERROR_IF(m_size > RHII.GetMaxUniformBufferSize(), "Uniform buffer size is too big");
        }
        if (m_usage & PE_BUFFER_USAGE_STORAGE_BUFFER)
        {
            m_size = RHII.AlignStorage(m_size);
            PE_ERROR_IF(m_size > RHII.GetMaxStorageBufferSize(), "Storage buffer size is too big");
        }
        if (m_usage & PE_BUFFER_USAGE_INDIRECT_BUFFER)
        {
            PE_ERROR_IF(m_size > RHII.GetMaxDrawIndirectCount() * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                        "Indirect command buffer size is too big");
        }
        if (!m_size)
            m_size = 16;

        m_impl = CreateBufferImpl(this, BufferDesc{m_size, m_usage, m_memoryUsage, m_name});
    }

    Buffer::~Buffer()
    {
        if (m_data)
            Unmap();
        delete m_impl;
    }

    void Buffer::Map()
    {
        if (m_data)
            return;
        m_data = m_impl->Map();
    }

    void Buffer::Unmap()
    {
        if (!m_data)
            return;
        m_impl->Unmap();
        m_data = nullptr;
    }

    void Buffer::Flush(size_t size, size_t offset) const
    {
        if (!m_data)
            return;
        size = size ? size : m_size;
        PE_ERROR_IF(offset + size > m_size, "Buffer::Flush: range overflow");
        m_impl->Flush(size, offset);
    }

    void Buffer::Zero() const
    {
        PE_ERROR_IF(!m_data, "Buffer::Zero: Buffer is not mapped");
        memset(m_data, 0, m_size);
    }

    void Buffer::CopyDataRaw(const void *data, size_t size, size_t offset)
    {
        if (!size)
            return;
        PE_ERROR_IF(!m_data, "Buffer::CopyDataRaw: Buffer is not mapped");
        PE_ERROR_IF(offset + size > m_size, "Buffer::CopyDataRaw: Source data size is too large");
        std::memcpy(static_cast<uint8_t *>(m_data) + offset, data, size);
    }

    void Buffer::Copy(uint32_t count, BufferRange *ranges, bool keepMapped)
    {
        Map();

        for (uint32_t i = 0; i < count; i++)
            CopyDataRaw(ranges[i].data, ranges[i].size, ranges[i].offset);

        if (!keepMapped)
        {
            for (uint32_t i = 0; i < count; i++)
                Flush(ranges[i].size, ranges[i].offset);

            Unmap();
        }
    }

    void Buffer::CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset)
    {
        if (!size)
            return;
        m_impl->CopyBuffer(cmd, src, size, srcOffset, dstOffset);
    }

    void Buffer::CopyBufferStaged(CommandBuffer *cmd, const void *data, size_t size, size_t dstOffset)
    {
        if (!size)
            return;
        PE_ERROR_IF(dstOffset + size > m_size, "CopyBufferStaged: dst range overflow");

        StagingAllocation alloc = RHII.GetStagingManager()->Allocate(size);
        std::memcpy(alloc.data, data, size);
        alloc.buffer->Flush(size, 0);

        CopyBuffer(cmd, alloc.buffer, size, 0, dstOffset);

        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]()
                                  { RHII.GetStagingManager()->SetUnused(alloc); });
    }

    uint64_t Buffer::GetDeviceAddress() const
    {
        PE_ERROR_IF(!(m_usage & PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS),
                    "Buffer '%s' was not created for device-address access",
                    m_name.c_str());
        PE_ERROR_IF(!RHII.GetCaps().bufferDeviceAddress,
                    "Buffer device addresses are not supported on this device");
        return m_impl->GetDeviceAddress();
    }

    void Buffer::Barrier(CommandBuffer *cmd, const BufferBarrierInfo &info)
    {
        Buffer_Barrier_Backend(cmd, info);
    }

    void Buffer::Barriers(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos)
    {
        Buffer_Barriers_Backend(cmd, infos);
    }
} // namespace pe
