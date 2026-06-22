#include "Voxel/FreeListAllocator.h"

namespace pe::voxel
{
    FreeListAllocator::FreeListAllocator(uint32_t capacityBytes)
        : m_capacity(capacityBytes)
    {
        m_free.push_back({0, capacityBytes});
    }

    uint32_t FreeListAllocator::Alloc(uint32_t bytes)
    {
        // Zero-byte allocations reserve nothing; report the first reusable offset if one exists.
        if (bytes == 0)
        {
            return m_free.empty() ? kInvalid : m_free[0].offset;
        }

        for (uint32_t i = 0; i < m_free.size(); ++i)
        {
            Span &span = m_free[i];
            if (span.size < bytes)
            {
                continue;
            }

            const uint32_t offset = span.offset;
            span.offset += bytes;
            span.size -= bytes;
            if (span.size == 0)
            {
                m_free.erase(m_free.begin() + i);
            }

            m_used += bytes;
            return offset;
        }

        return kInvalid;
    }

    void FreeListAllocator::Free(uint32_t offset, uint32_t bytes)
    {
        if (bytes == 0)
        {
            return;
        }

        uint32_t insertIndex = 0;
        while (insertIndex < m_free.size() && m_free[insertIndex].offset < offset)
        {
            ++insertIndex;
        }

        m_free.insert(m_free.begin() + insertIndex, {offset, bytes});

        if (insertIndex > 0)
        {
            Span &previous = m_free[insertIndex - 1];
            Span &current = m_free[insertIndex];
            if (previous.offset + previous.size == current.offset)
            {
                previous.size += current.size;
                m_free.erase(m_free.begin() + insertIndex);
                --insertIndex;
            }
        }

        if (insertIndex + 1 < m_free.size())
        {
            Span &current = m_free[insertIndex];
            Span &next = m_free[insertIndex + 1];
            if (current.offset + current.size == next.offset)
            {
                current.size += next.size;
                m_free.erase(m_free.begin() + insertIndex + 1);
            }
        }

        m_used -= bytes;
    }

    uint32_t FreeListAllocator::Used() const
    {
        return m_used;
    }
} // namespace pe::voxel
