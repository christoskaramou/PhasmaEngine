#include "API/RingBuffer.h"
#include "API/Buffer.h"

namespace pe
{
    void *RingBuffer::Chunk::PtrAt(size_t offset) const
    {
        return static_cast<uint8_t *>(buffer->Data()) + offset;
    }

    RingBuffer::Chunk::~Chunk()
    {
        if (buffer)
            Buffer::Destroy(buffer);
    }

    RingBuffer::~RingBuffer()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_chunks.clear();
    }

    // -------------------------------------------------------
    // Static helpers
    // -------------------------------------------------------

    std::vector<RingBuffer::Block>::iterator
    RingBuffer::FindBestFitBlock(std::vector<Block> &blocks, size_t sizeAligned)
    {
        auto best = blocks.end();
        size_t bestSize = ~size_t(0);

        for (auto it = blocks.begin(); it != blocks.end(); ++it)
        {
            if (it->free && it->size >= sizeAligned && it->size < bestSize)
            {
                best = it;
                bestSize = it->size;
            }
        }
        return best;
    }

    size_t RingBuffer::TryAppendAtTail(const Chunk *chunk, size_t sizeAligned)
    {
        // NOTE:
        // We assume MergeFreeBlocks() is called on every Free(),
        // so the memory after 'tail' is always one coalesced free region.
        // tail is recomputed to max end of USED blocks, so [tail, capacity)
        // should be free space.
        //
        // This is what keeps this cheap and valid.
        const size_t alignedTail = AlignUp(chunk->tail);
        return (alignedTail + sizeAligned <= chunk->capacity) ? alignedTail : ~0ull;
    }

    Allocation RingBuffer::MakeAllocation(Chunk *chunk, size_t sizeAligned, size_t offset)
    {
        Allocation alloc{};
        alloc.data = chunk->PtrAt(offset);
        alloc.size = sizeAligned;
        alloc.offset = offset;
        alloc.buffer = chunk->buffer;
        alloc.chunk = chunk;
        return alloc;
    }

    Allocation RingBuffer::CarveFromBlock(Chunk *chunk,
                                          std::vector<Block>::iterator it,
                                          size_t sizeAligned)
    {
        const size_t allocOffset = it->offset;
        const size_t remaining = it->size - sizeAligned;

        // consume the head of this free block
        it->free = false;
        it->size = sizeAligned;

        // push the leftover tail (still free) right after it
        if (remaining > 0)
        {
            Block rest{
                allocOffset + sizeAligned,
                remaining,
                true};
            chunk->blocks.insert(it + 1, rest);
        }

        const size_t newEnd = allocOffset + sizeAligned;
        chunk->tail = std::max(chunk->tail, newEnd);
        chunk->usedBytes += sizeAligned;

        Allocation a = MakeAllocation(chunk, sizeAligned, allocOffset);
        DebugValidateChunk(chunk);
        return a;
    }

    Allocation RingBuffer::AppendNewBlock(Chunk *chunk,
                                          size_t appendOffset,
                                          size_t sizeAligned)
    {
        // fast path: reuse the last block if it is a matching free tail
        if (!chunk->blocks.empty())
        {
            Block &last = chunk->blocks.back();
            if (last.free && last.offset == appendOffset)
            {
                const size_t remaining = last.size - sizeAligned;

                last.free = false;
                last.size = sizeAligned;

                if (remaining > 0)
                {
                    chunk->blocks.push_back(Block{
                        appendOffset + sizeAligned,
                        remaining,
                        true});
                }

                const size_t newEnd = appendOffset + sizeAligned;
                chunk->tail = std::max(chunk->tail, newEnd);
                chunk->usedBytes += sizeAligned;

                Allocation a = MakeAllocation(chunk, sizeAligned, appendOffset);
                DebugValidateChunk(chunk);
                return a;
            }
        }

        // otherwise: append a new used block
        chunk->blocks.push_back(Block{
            appendOffset,
            sizeAligned,
            false});

        const size_t end = appendOffset + sizeAligned;
        if (end < chunk->capacity)
        {
            // trailing free block after the new used block
            chunk->blocks.push_back(Block{
                end,
                chunk->capacity - end,
                true});
        }

        chunk->tail = std::max(chunk->tail, end);
        chunk->usedBytes += sizeAligned;

        Allocation a = MakeAllocation(chunk, sizeAligned, appendOffset);
        DebugValidateChunk(chunk);
        return a;
    }

    void RingBuffer::MergeFreeBlocks(Chunk *chunk)
    {
        auto &blocks = chunk->blocks;

        // merge adjacent free blocks in-place
        for (size_t i = 0; i + 1 < blocks.size();)
        {
            Block &curr = blocks[i];
            Block &next = blocks[i + 1];

            if (curr.free &&
                next.free &&
                (curr.offset + curr.size == next.offset))
            {
                curr.size += next.size;
                blocks.erase(blocks.begin() + (i + 1));
            }
            else
            {
                ++i;
            }
        }

        // recompute tail, max end of any USED block
        size_t maxUsedEnd = 0;
        for (const Block &blk : blocks)
        {
            if (!blk.free)
            {
                const size_t end = blk.offset + blk.size;
                if (end > maxUsedEnd)
                    maxUsedEnd = end;
            }
        }
        chunk->tail = maxUsedEnd;
    }

#if defined(_DEBUG)
    void RingBuffer::DebugValidateChunk(const Chunk *c)
    {
        const auto &b = c->blocks;
        for (size_t i = 1; i < b.size(); ++i)
        {
            // 1) strictly increasing offsets
            // 2) contiguous tiling: prev.offset + prev.size == this.offset
            PE_ERROR_IF(!(b[i - 1].offset < b[i].offset), "RingBuffer: blocks not strictly ordered by offset");
            PE_ERROR_IF(b[i - 1].offset + b[i - 1].size != b[i].offset, "RingBuffer: block list not contiguous / sorted");
        }
    }
#endif

    // -------------------------------------------------------
    // Chunk management
    // -------------------------------------------------------

    RingBuffer::Chunk *RingBuffer::CreateChunk(size_t minCapacity)
    {
        size_t capacity = std::max(kMinCapacity, AlignUp(minCapacity));

        // grow from last capacity using kGrowthFactor
        if (!m_chunks.empty())
        {
            const size_t lastCap = m_chunks.back()->capacity;
            const size_t grown =
                static_cast<size_t>(static_cast<float>(lastCap) * kGrowthFactor);

            if (grown > capacity)
                capacity = grown;
        }

        auto chunkHolder = std::make_unique<Chunk>();
        Chunk *chunk = chunkHolder.get();

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
        chunk->blocks.reserve(8);
        chunk->blocks.push_back(Block{0, capacity, true});

        m_chunks.push_back(std::move(chunkHolder));

        DebugValidateChunk(chunk);
        return chunk;
    }

    // Try to suballocate from one chunk
    Allocation RingBuffer::AllocateFromChunk(Chunk *chunk, size_t sizeAligned)
    {
        // best fit free block
        auto it = FindBestFitBlock(chunk->blocks, sizeAligned);
        if (it != chunk->blocks.end())
        {
            return CarveFromBlock(chunk, it, sizeAligned);
        }

        // try append at tail
        const size_t appendOffset = TryAppendAtTail(chunk, sizeAligned);
        if (appendOffset != ~0ull)
        {
            return AppendNewBlock(chunk, appendOffset, sizeAligned);
        }

        // fail
        return {};
    }

    Allocation RingBuffer::Allocate(size_t size)
    {
        PE_ERROR_IF(size == 0, "RingBuffer::Allocate: size == 0");

        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t sizeAligned = AlignUp(size);

        // newest -> oldest
        for (auto it = m_chunks.rbegin(); it != m_chunks.rend(); ++it)
        {
            if (Allocation a = AllocateFromChunk(it->get(), sizeAligned); a.Valid())
                return a;
        }

        // no existing chunk fits, create a new one
        Chunk *fresh = CreateChunk(sizeAligned);
        Allocation a = AllocateFromChunk(fresh, sizeAligned);
        PE_ERROR_IF(!a.Valid(), "RingBuffer::Allocate: allocation failed after new chunk");

        return a;
    }

    void RingBuffer::FreeInChunk(Chunk *chunk, const Allocation &range)
    {
        auto &blocks = chunk->blocks;

        auto it = std::find_if(
            blocks.begin(), blocks.end(),
            [&](Block &b)
            {
                return !b.free &&
                       b.offset == range.offset &&
                       b.size == range.size;
            });

        PE_ERROR_IF(it == blocks.end(), "RingBuffer::FreeInChunk: allocation not found in chunk");
        if (it == blocks.end())
            return;

        it->free = true;
        chunk->usedBytes -= it->size;

        // we merge eagerly -> keeps contiguous free tail
        MergeFreeBlocks(chunk);
        DebugValidateChunk(chunk);
    }

    void RingBuffer::DestroyUnused(Chunk *chunk)
    {
        if (m_chunks.empty())
            return;

        // if chunk still has live allocations, keep it
        if (chunk->usedBytes != 0)
            return;

        // we keep chunks that are empty only if they are the newest,
        // to avoid thrash, older empty chunks are destroyed.
        if (m_chunks.back().get() == chunk)
            return;

        std::erase_if(m_chunks,
                      [chunk](const std::unique_ptr<Chunk> &c)
                      { return c.get() == chunk; });
    }

    void RingBuffer::Free(const Allocation &range)
    {
        if (!range.Valid() || !range.chunk)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);

        Chunk *chunk = static_cast<Chunk *>(range.chunk);
        PE_ERROR_IF(!OwnsChunk(chunk),
                    "RingBuffer::Free: allocation does not belong to this buffer");

        FreeInChunk(chunk, range);
        DestroyUnused(chunk);
    }

    void RingBuffer::Reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_chunks.empty())
        {
            CreateChunk(kMinCapacity);
            return;
        }

        // keep the SMALLEST chunk
        auto smallest = std::min_element(
            m_chunks.begin(), m_chunks.end(),
            [](const std::unique_ptr<Chunk> &a,
               const std::unique_ptr<Chunk> &b)
            {
                return a->capacity < b->capacity;
            });

        std::unique_ptr<Chunk> keep = std::move(*smallest);
        m_chunks.clear();

        // rebuild as a single big free block
        keep->blocks.clear();
        keep->blocks.push_back(Block{0, keep->capacity, true});
        keep->tail = 0;
        keep->usedBytes = 0;

        m_chunks.push_back(std::move(keep));

        DebugValidateChunk(m_chunks.back().get());
    }

    bool RingBuffer::OwnsChunk(const Chunk *chunk) const
    {
        return std::any_of(
            m_chunks.begin(), m_chunks.end(),
            [chunk](const std::unique_ptr<Chunk> &c)
            {
                return c.get() == chunk;
            });
    }

} // namespace pe
