#include "API/RingBuffer.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Command.h"

namespace pe
{
    RingBuffer::~RingBuffer()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (Chunk *chunk : m_chunks)
        {
            Buffer::Destroy(chunk->buffer);
            delete chunk;
        }
        m_chunks.clear();
    }

    inline void *RingBuffer::Chunk::PtrAt(size_t offset) const
    {
        return static_cast<uint8_t *>(buffer->Data()) + offset;
    }

    RingBuffer::Chunk *RingBuffer::CreateChunk(size_t minCapacity)
    {
        size_t capacity = std::max(kMinCapacity, AlignUp(minCapacity));

        if (!m_chunks.empty())
        {
            size_t lastCap = m_chunks.back()->capacity;
            size_t grown = static_cast<size_t>(static_cast<float>(lastCap) * kGrowthFactor);
            if (grown > capacity)
                capacity = grown;
        }

        Chunk *chunk = new Chunk();
        chunk->buffer = Buffer::Create(
            capacity,
            vk::BufferUsageFlagBits2::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            "RingBuffer_ChunkBuffer");

        chunk->buffer->Map();

        chunk->capacity = capacity;
        chunk->tail = 0;
        chunk->usedBytes = 0;
        chunk->blocks.clear();
        chunk->blocks.reserve(8); // we rarely have more than 8 fragmented blocks

        Block b{};
        b.offset = 0;
        b.size = capacity;
        b.free = true;
        chunk->blocks.push_back(b);

        m_chunks.push_back(chunk);
        return chunk;
    }

    inline int RingBuffer::FindIndex(Chunk *chunk, size_t sizeAligned) const
    {
        int index = -1;
        size_t size = ~0ull;

        const auto &blocks = chunk->blocks;
        for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
        {
            const Block &blk = blocks[i];
            if (blk.free && blk.size >= sizeAligned)
            {
                if (blk.size < size)
                {
                    size = blk.size;
                    index = i;
                }
            }
        }
        return index;
    }

    inline size_t RingBuffer::TryAppend(Chunk *chunk, size_t sizeAligned) const
    {
        size_t alignedTail = AlignUp(chunk->tail);

        if (alignedTail + sizeAligned <= chunk->capacity)
            return alignedTail;

        return ~0ull;
    }

    inline Allocation RingBuffer::AllocateFromChunk(Chunk *chunk, size_t sizeAligned)
    {
        Allocation alloc{};

        // 1. try a free block
        {
            int index = FindIndex(chunk, sizeAligned);
            if (index >= 0)
            {
                Block &block = chunk->blocks[index];
                const size_t allocOffset = block.offset;
                const size_t remaining = block.size - sizeAligned;

                // consume the block
                block.free = false;
                block.size = sizeAligned;

                // split tail remainder back into a free block
                if (remaining > 0)
                {
                    Block rest{};
                    rest.offset = allocOffset + sizeAligned;
                    rest.size = remaining;
                    rest.free = true;

                    chunk->blocks.insert(chunk->blocks.begin() + (index + 1), rest);
                }

                // update chunk tail if we extended the end-of-used range
                size_t newEnd = allocOffset + sizeAligned;
                if (newEnd > chunk->tail)
                    chunk->tail = newEnd;

                chunk->usedBytes += sizeAligned;

                alloc.data = chunk->PtrAt(allocOffset);
                alloc.size = sizeAligned;
                alloc.offset = allocOffset;
                alloc.buffer = chunk->buffer;
                alloc.chunk = chunk;
                return alloc;
            }
        }

        // 2. try append-at-tail
        {
            size_t appendOffset = TryAppend(chunk, sizeAligned);
            if (appendOffset != ~0ull)
            {
                bool consumedLastFree = false;

                // If the last block is a single big free tail that starts exactly
                // at appendOffset, just carve from it.
                if (!chunk->blocks.empty())
                {
                    Block &last = chunk->blocks.back();
                    if (last.free && last.offset == appendOffset)
                    {
                        size_t remaining = last.size - sizeAligned;

                        last.free = false;
                        last.size = sizeAligned;

                        if (remaining > 0)
                        {
                            Block rest{};
                            rest.offset = appendOffset + sizeAligned;
                            rest.size = remaining;
                            rest.free = true;
                            chunk->blocks.push_back(rest);
                        }

                        consumedLastFree = true;
                    }
                }

                if (!consumedLastFree)
                {
                    // otherwise push a new used block
                    Block used{};
                    used.offset = appendOffset;
                    used.size = sizeAligned;
                    used.free = false;
                    chunk->blocks.push_back(used);

                    // and optionally the trailing free block
                    const size_t end = appendOffset + sizeAligned;
                    if (end < chunk->capacity)
                    {
                        Block freeBlk{};
                        freeBlk.offset = end;
                        freeBlk.size = chunk->capacity - end;
                        freeBlk.free = true;
                        chunk->blocks.push_back(freeBlk);
                    }
                }

                size_t newEnd = appendOffset + sizeAligned;
                if (newEnd > chunk->tail)
                    chunk->tail = newEnd;

                chunk->usedBytes += sizeAligned;

                alloc.data = chunk->PtrAt(appendOffset);
                alloc.size = sizeAligned;
                alloc.offset = appendOffset;
                alloc.buffer = chunk->buffer;
                alloc.chunk = chunk;
                return alloc;
            }
        }

        // failure
        return alloc;
    }

    Allocation RingBuffer::Allocate(size_t size)
    {
        PE_ERROR_IF(size == 0, "RingBuffer::Allocate: size == 0");

        std::lock_guard<std::mutex> lock(m_mutex);

        const size_t sizeAligned = AlignUp(size);

        // try newest chunk first
        if (!m_chunks.empty())
        {
            Chunk *latest = m_chunks.back();
            Allocation alloc = AllocateFromChunk(latest, sizeAligned);
            if (alloc.data)
                return alloc;
        }

        // try other existing chunks
        for (int i = static_cast<int>(m_chunks.size()) - 2; i >= 0; --i)
        {
            Chunk *chunk = m_chunks[static_cast<size_t>(i)];
            Allocation alloc = AllocateFromChunk(chunk, sizeAligned);
            if (alloc.data)
                return alloc;
        }

        // create new chunk and allocate there
        {
            Chunk *chunk = CreateChunk(sizeAligned);
            Allocation alloc = AllocateFromChunk(chunk, sizeAligned);
            PE_ERROR_IF(!alloc.data, "RingBuffer::Allocate: allocation failed after new chunk");
            return alloc;
        }
    }

    inline void RingBuffer::MergeFreeBlocks(Chunk *chunk)
    {
        // Compact adjacent free blocks in-place
        auto &blocks = chunk->blocks;
        for (size_t i = 0; i + 1 < blocks.size();)
        {
            Block &curr = blocks[i];
            Block &next = blocks[i + 1];

            if (curr.free && next.free)
            {
                curr.size += next.size;
                blocks.erase(blocks.begin() + (i + 1));
            }
            else
            {
                ++i;
            }
        }
        // Update tail to highest used end
        size_t maxUsedEnd = 0;
        for (const Block &blk : blocks)
        {
            if (!blk.free)
            {
                size_t end = blk.offset + blk.size;
                if (end > maxUsedEnd)
                    maxUsedEnd = end;
            }
        }
        chunk->tail = maxUsedEnd;
    }

    inline void RingBuffer::FreeInChunk(Chunk *chunk, const Allocation &range)
    {
        auto &blocks = chunk->blocks;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            Block &block = blocks[i];
            if (!block.free &&
                block.offset == range.offset &&
                block.size == range.size)
            {
                block.free = true;
                chunk->usedBytes -= block.size;
                break;
            }
        }

        // Merge and update tail.
        MergeFreeBlocks(chunk);
    }

    inline void RingBuffer::DestroyUnsused(Chunk *chunk)
    {
        if (m_chunks.empty())
            return;

        // destroy only if it is not used
        if (chunk->usedBytes != 0)
            return;

        // keep the last chunk
        if (m_chunks.back() == chunk)
            return;

        // remove and destroy
        auto it = std::find(m_chunks.begin(), m_chunks.end(), chunk);
        if (it != m_chunks.end())
        {
            Buffer::Destroy(chunk->buffer);
            delete chunk;
            m_chunks.erase(it);
        }
    }

    void RingBuffer::Free(const Allocation &range)
    {
        if (!range.data || !range.chunk)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);

        Chunk *chunk = reinterpret_cast<Chunk *>(range.chunk);

        FreeInChunk(chunk, range);
        DestroyUnsused(chunk);
    }

    void RingBuffer::Reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_chunks.empty())
            return;

        // Keep the latest chunk and destroy the others
        Chunk *keep = m_chunks.back();

        for (Chunk *chunk : m_chunks)
        {
            if (chunk == keep)
                continue;

            Buffer::Destroy(chunk->buffer);
            delete chunk;
        }

        m_chunks.clear();
        m_chunks.push_back(keep);

        // Rebuild keep as a single free block
        keep->blocks.clear();
        keep->blocks.reserve(1);
        Block b{};
        b.offset = 0;
        b.size = keep->capacity;
        b.free = true;
        keep->blocks.push_back(b);

        keep->tail = 0;
        keep->usedBytes = 0;
    }
}
