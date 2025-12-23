#include "StagingManager.h"
#include "Buffer.h"
#include "Queue.h"
#include "RHI.h"

namespace pe
{
    StagingManager::~StagingManager()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &alloc : m_allocations)
        {
            if (alloc.buffer)
            {
                alloc.buffer->Unmap();
                Buffer::Destroy(alloc.buffer);
            }
        }
        m_allocations.clear();
    }

    StagingAllocation StagingManager::Allocate(size_t size)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto &allocation : m_allocations)
        {
            if (!allocation.used && allocation.size > size * kOversizeCapMultiplier)
            {
                allocation.used = size;
                return allocation;
            }
        }

        StagingAllocation allocation{};
        allocation.size = size;
        allocation.used = size;

        allocation.buffer = Buffer::Create(
            size,
            vk::BufferUsageFlagBits2::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            "StagingBuffer");
        PE_ERROR_IF(!allocation.buffer, "StagingManager::Allocate(): failed to create staging buffer.");

        allocation.buffer->Map();
        allocation.data = allocation.buffer->Data();

        m_allocations.push_back(allocation);
        return allocation;
    }

    void StagingManager::RemoveUnused()
    {
        size_t serial = RHII.GetMainQueue()->GetSubmissionCount();
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_allocations.begin(); it != m_allocations.end();)
        {
            if (serial <= it->delaySerial || it->used)
            {
                ++it;
                continue;
            }

            if (it->buffer)
            {
                it->buffer->Unmap();
                Buffer::Destroy(it->buffer);
            }

            it = m_allocations.erase(it);
        }
    }

    void StagingManager::SetUnused(const StagingAllocation &allocation)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &alloc : m_allocations)
        {
            if (alloc.buffer == allocation.buffer)
            {
                alloc.used = 0;
                alloc.delaySerial = RHII.GetMainQueue()->GetSubmissionCount() + kDeleteDelay;
                return;
            }
        }
    }
} // namespace pe