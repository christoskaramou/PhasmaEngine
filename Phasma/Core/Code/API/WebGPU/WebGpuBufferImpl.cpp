#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/Command.h"
#include "API/WebGPU/WebGpuCommandBufferImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        bool IsHostVisibleUsage(PeMemoryUsage usage)
        {
            return usage == PE_MEMORY_USAGE_CPU_TO_GPU ||
                   usage == PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT ||
                   usage == PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT_DEVICE;
        }

        bool IsReadbackUsage(PeMemoryUsage usage)
        {
            return usage == PE_MEMORY_USAGE_GPU_TO_CPU || usage == PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT;
        }

        WGPUBufferUsage ToWgpuUsage(PeBufferUsageFlags usage)
        {
            // Every buffer can be copied both ways: uploads (writeBuffer) and in-stream copies need it.
            WGPUBufferUsage out = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
            if (usage & PE_BUFFER_USAGE_UNIFORM_BUFFER)
                out |= WGPUBufferUsage_Uniform;

            if (usage & PE_BUFFER_USAGE_STORAGE_BUFFER)
                out |= WGPUBufferUsage_Storage;

            if (usage & PE_BUFFER_USAGE_INDEX_BUFFER)
                out |= WGPUBufferUsage_Index;

            if (usage & PE_BUFFER_USAGE_VERTEX_BUFFER)
                out |= WGPUBufferUsage_Vertex;

            if (usage & PE_BUFFER_USAGE_INDIRECT_BUFFER)
                out |= WGPUBufferUsage_Indirect;

            return out;
        }

        size_t AlignUp4(size_t v)
        {
            return (v + 3) & ~size_t(3);
        }
    } // namespace

    WebGpuBufferImpl::WebGpuBufferImpl(Buffer *owner, const BufferDesc &desc)
        : m_owner{owner},
          m_paddedSize{AlignUp4(desc.size)},
          m_hostVisible{IsHostVisibleUsage(desc.memoryUsage)}
    {
        WGPUBufferDescriptor bufferDesc{};
        bufferDesc.label = {desc.name.c_str(), desc.name.size()};
        bufferDesc.size = m_paddedSize;
        bufferDesc.usage = ToWgpuUsage(desc.usage);
        m_buffer = wgpuDeviceCreateBuffer(WebGpuRhiImpl::Get()->m_device, &bufferDesc);
        PE_ERROR_IF(!m_buffer, "WebGpuBufferImpl: failed to create buffer '%s'", desc.name.c_str());

        if (m_hostVisible || IsReadbackUsage(desc.memoryUsage))
            m_shadow.resize(m_paddedSize);
    }

    WebGpuBufferImpl::~WebGpuBufferImpl()
    {
        if (m_buffer)
        {
            wgpuBufferDestroy(m_buffer);
            wgpuBufferRelease(m_buffer);
        }
    }

    void *WebGpuBufferImpl::Map()
    {
        if (!m_hostVisible)
        {
            // Readback is off in the first version (decision 5): callers read zeros.
            PE_ERROR_IF(m_shadow.empty(), "WebGpuBufferImpl::Map: '%s' is GPU-only", m_owner->GetName().c_str());
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                PE_WARN("WebGPU: GPU readback is not available on the web yet; '%s' reads zeros",
                        m_owner->GetName().c_str());
            }
        }

        m_flushed.clear();
        m_mapped = true;
        return m_shadow.data();
    }

    void WebGpuBufferImpl::Unmap()
    {
        m_mapped = false;
        if (!m_hostVisible)
            return;

        // Rule 1: upload the gaps between the ranges Flush already uploaded since Map.
        // ponytail: a range written again after its Flush is not re-uploaded here; to lift it, track dirty ranges in
        // Map/Data()/CopyDataRaw (the same upgrade as rule 2's).
        size_t cursor = 0;
        for (const auto &[begin, end] : m_flushed)
        {
            if (begin > cursor)
                Upload(cursor, begin);

            cursor = std::max(cursor, end);
        }

        if (cursor < m_owner->Size())
            Upload(cursor, m_owner->Size());

        m_flushed.clear();
    }

    void WebGpuBufferImpl::Flush(size_t size, size_t offset) const
    {
        if (!m_hostVisible)
            return;

        const size_t begin = offset;
        const size_t end = offset + size;
        Upload(begin, end);

        // Insert [begin, end) and merge every range it overlaps or touches.
        auto it = std::lower_bound(m_flushed.begin(), m_flushed.end(), std::make_pair(begin, end));
        it = m_flushed.insert(it, {begin, end});
        if (it != m_flushed.begin() && std::prev(it)->second >= it->first)
            --it;

        auto next = std::next(it);
        while (next != m_flushed.end() && next->first <= it->second)
        {
            it->second = std::max(it->second, next->second);
            next = m_flushed.erase(next);
        }
    }

    uint64_t WebGpuBufferImpl::GetDeviceAddress() const
    {
        PE_ERROR("WebGpuBufferImpl::GetDeviceAddress: WebGPU has no buffer device addresses");
        return 0;
    }

    void WebGpuBufferImpl::CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset)
    {
        PE_ERROR_IF(!cmd, "WebGpuBufferImpl::CopyBuffer: no command buffer specified");
        WebGpuCommandBufferImpl::From(cmd)->CopyBuffer(src, m_owner, size, srcOffset, dstOffset);
    }

    void WebGpuBufferImpl::UploadShadow() const
    {
        Upload(0, m_owner->Size());
    }

    void WebGpuBufferImpl::Upload(size_t begin, size_t end) const
    {
        // writeBuffer offsets and sizes must be multiples of 4; the padded size keeps the rounded range in bounds.
        begin &= ~size_t(3);
        end = std::min(AlignUp4(end), m_paddedSize);
        if (end <= begin)
            return;

        wgpuQueueWriteBuffer(WebGpuRhiImpl::Get()->m_queue, m_buffer, begin, m_shadow.data() + begin, end - begin);
        s_uploadedBytes += end - begin;
    }
} // namespace pe
