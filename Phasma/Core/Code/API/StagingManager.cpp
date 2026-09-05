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

        // Bucket the request up to a power-of-two size class (256 B floor) so the many slightly-different
        // voxel upload sizes (8.7 KB, 11.9 KB, …) collapse onto a few shared classes. Without this the
        // free list fragments — best-fit can't reuse a buffer that's even one byte too small, so each
        // streaming burst created fresh buffers (Buffer::Create == a DX12 CreateCommittedResource, slow),
        // spiking the frame. Bucketing makes reuse converge after warmup; the slack capacity is never
        // touched (CopyBufferStaged only ever copies the real request size).
        size_t bucket = 256;
        while (bucket < size)
            bucket <<= 1;

        StagingAllocation *best = nullptr;
        for (auto &allocation : m_allocations)
        {
            if (allocation.used || allocation.size < bucket)
                continue;
            if (!best || allocation.size < best->size)
                best = &allocation;
        }
        if (best)
        {
            best->used = bucket;
            return *best;
        }

        StagingAllocation allocation{};
        allocation.size = bucket;
        allocation.used = bucket;

        allocation.buffer = Buffer::Create({
            .size = bucket,
            .usage = PE_BUFFER_USAGE_TRANSFER_SRC,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT,
            .name = "StagingBuffer",
        });
        PE_ERROR_IF(!allocation.buffer, "StagingManager::Allocate(): failed to create staging buffer.");

        allocation.buffer->Map();
        allocation.data = allocation.buffer->Data();

        m_allocations.push_back(allocation);
        return allocation;
    }

    void StagingManager::RemoveUnused()
    {
        // Retain freed staging buffers under a byte budget instead of destroying them every frame.
        // On DX12 Buffer::Create == CreateCommittedResource (an expensive heap/kernel allocation); the
        // old per-frame teardown meant every voxel-streaming burst re-created its whole working set of
        // staging buffers (3 per section), spiking the frame. Vulkan hid this (cheap VMA sub-alloc).
        // Keeping the pool alive lets the next burst reuse buffers (zero creates); the budget caps VRAM
        // so one-off large uploads (textures) still get reclaimed.
        constexpr size_t kRetainBudget = 128ull * 1024 * 1024;

        std::lock_guard<std::mutex> lock(m_mutex);

        size_t total = 0;
        for (auto &alloc : m_allocations)
            total += alloc.size;
        if (total <= kRetainBudget)
            return;

        size_t serial = RHII.GetMainQueue()->GetSubmissionCount();
        for (auto it = m_allocations.begin(); it != m_allocations.end() && total > kRetainBudget;)
        {
            if (serial <= it->delaySerial || it->used)
            {
                ++it;
                continue;
            }

            total -= it->size;
            if (it->buffer)
            {
                it->buffer->Unmap();
                Buffer::Destroy(it->buffer);
            }

            it = m_allocations.erase(it);
        }
    }

    // Call only once the GPU is done with the allocation (today: exclusively from CommandBuffer after-wait
    // callbacks). Allocate() hands an unused buffer out immediately; delaySerial only paces RemoveUnused().
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
