#include "Scene/HingeField.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t kInvalidSlot = 0xFFFFFFFFu;
        constexpr uint32_t kHiddenBit = 1u << 31;
        constexpr uint32_t kBackwardBit = 1u << 30;
        constexpr uint32_t kGenerationMask = 0x3FFFu;
        constexpr uint32_t kGenerationShift = 16;
        constexpr float kQuarterTurn = 1.57079632679f;
        constexpr float kCellSize = 32.f;           // a side of a culling cell, in world units
        constexpr uint64_t kMaxCells = 1u << 20;    // the cells grow past this rather than the grid
        constexpr uint32_t kTailLimit = 1u << 14;   // unsorted appends drawn uncull before a re-sort
        constexpr float kReach = 1.2f;              // how far past its base a box's fall reaches
        constexpr size_t kMinBufferBytes = 1u << 20;
        constexpr size_t kMergeGapBytes = 1u << 16; // uploads this close together travel as one

        uint32_t PackAngle(float angle)
        {
            return static_cast<uint32_t>(std::clamp(angle / kQuarterTurn, 0.f, 1.f) * 255.f + 0.5f);
        }
    }

    HingeField::~HingeField()
    {
        if (m_buffer)
            RHII.AddToDeletionQueue([b = m_buffer]() mutable { Buffer::Destroy(b); });
    }

    void HingeField::Clear()
    {
        m_instances.clear();
        m_slotOfId.clear();
        m_idOfSlot.clear();
        m_cells.clear();
        m_cellDirty.clear();
        m_dirtyCells.clear();
        m_sortedCount = 0;
        m_gpuCount = 0;
        m_tailDirty = false;
        m_layoutDirty = false;
        m_fullUpload = false;
    }

    HingeField::Instance *HingeField::Append(uint32_t count, uint32_t &firstId)
    {
        firstId = static_cast<uint32_t>(m_slotOfId.size());
        const uint32_t firstSlot = static_cast<uint32_t>(m_instances.size());
        m_instances.resize(firstSlot + count);
        m_slotOfId.resize(firstId + count);
        m_idOfSlot.resize(firstSlot + count);
        for (uint32_t i = 0; i < count; i++)
        {
            m_slotOfId[firstId + i] = firstSlot + i;
            m_idOfSlot[firstSlot + i] = firstId + i;
        }

        m_tailDirty = true;
        if (m_instances.size() - m_sortedCount > kTailLimit)
            m_layoutDirty = true;
        return m_instances.data() + firstSlot;
    }

    void HingeField::TouchSlot(uint32_t slot)
    {
        if (slot >= m_sortedCount)
        {
            m_tailDirty = true;
            return;
        }

        const auto after = std::upper_bound(m_cells.begin(), m_cells.end(), slot,
                                            [](uint32_t s, const Cell &cell) { return s < cell.first; });
        const uint32_t index = static_cast<uint32_t>(after - m_cells.begin()) - 1u;
        if (!m_cellDirty[index])
        {
            m_cellDirty[index] = 1;
            m_dirtyCells.push_back(index);
        }
    }

    void HingeField::Hide(uint32_t id)
    {
        if (id >= m_slotOfId.size() || m_slotOfId[id] == kInvalidSlot)
            return;

        const uint32_t slot = m_slotOfId[id];
        m_instances[slot].state |= kHiddenBit;
        TouchSlot(slot);
    }

    void HingeField::Fall(uint32_t id, float tStart, float tContact, float contact, float final, bool backward)
    {
        if (id >= m_slotOfId.size() || m_slotOfId[id] == kInvalidSlot)
            return;

        const uint32_t slot = m_slotOfId[id];
        Instance &instance = m_instances[slot];
        const uint32_t hidden = instance.state & kHiddenBit;
        instance.tStart = tStart;
        instance.tContact = tContact;
        instance.state = PackAngle(contact) | (PackAngle(final) << 8) | (m_generation << kGenerationShift) |
                         (backward ? kBackwardBit : 0u) | hidden;
        TouchSlot(slot);
    }

    void HingeField::StandAll()
    {
        m_previousClock = m_clock = 0.f;
        if (++m_generation <= kGenerationMask)
            return;

        // The counter wrapped: forget every old generation so none can match again.
        m_generation = 1;
        for (Instance &instance : m_instances)
            instance.state &= ~(kGenerationMask << kGenerationShift);
        m_fullUpload = true;
    }

    void HingeField::SetClock(float time)
    {
        m_previousClock = m_clock;
        m_clock = time;
    }

    std::array<vec4, 6> HingeField::PlanesFromMatrix(const mat4 &m)
    {
        const vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
        return {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2};
    }

    void HingeField::RebuildLayout()
    {
        m_layoutDirty = false;
        m_tailDirty = false;
        m_fullUpload = true;
        m_cells.clear();
        m_dirtyCells.clear();

        vec2 low(std::numeric_limits<float>::max());
        vec2 high(std::numeric_limits<float>::lowest());
        uint32_t live = 0;
        for (const Instance &instance : m_instances)
        {
            if (instance.state & kHiddenBit)
                continue;

            live++;
            low = glm::min(low, vec2(instance.x, instance.z));
            high = glm::max(high, vec2(instance.x, instance.z));
        }

        float cellSize = kCellSize;
        uint32_t columns = 1, rows = 1;
        if (live > 0)
        {
            const vec2 extent = high - low;
            do
            {
                columns = static_cast<uint32_t>(extent.x / cellSize) + 1u;
                rows = static_cast<uint32_t>(extent.y / cellSize) + 1u;
                if (static_cast<uint64_t>(columns) * rows <= kMaxCells)
                    break;
                cellSize *= 2.f;
            } while (true);
        }

        // Counting sort: instances stay in the order they came within a cell, and hidden ones are dropped for good.
        const size_t cellCount = static_cast<size_t>(columns) * rows;
        std::vector<uint32_t> start(cellCount + 1, 0u);
        const auto cellOf = [&](const Instance &instance)
        {
            const uint32_t column = std::min(static_cast<uint32_t>((instance.x - low.x) / cellSize), columns - 1u);
            const uint32_t row = std::min(static_cast<uint32_t>((instance.z - low.y) / cellSize), rows - 1u);
            return static_cast<size_t>(row) * columns + column;
        };

        for (const Instance &instance : m_instances)
        {
            if (!(instance.state & kHiddenBit))
                start[cellOf(instance) + 1]++;
        }
        for (size_t i = 1; i <= cellCount; i++)
            start[i] += start[i - 1];

        std::vector<Instance> sorted(live);
        std::vector<uint32_t> idOfSlot(live);
        std::vector<uint32_t> next(start.begin(), start.end() - 1);
        for (size_t i = 0; i < m_instances.size(); i++)
        {
            const Instance &instance = m_instances[i];
            const uint32_t id = m_idOfSlot[i];
            if (instance.state & kHiddenBit)
            {
                m_slotOfId[id] = kInvalidSlot;
                continue;
            }

            const uint32_t slot = next[cellOf(instance)]++;
            sorted[slot] = instance;
            idOfSlot[slot] = id;
            m_slotOfId[id] = slot;
        }
        m_instances = std::move(sorted);
        m_idOfSlot = std::move(idOfSlot);
        m_sortedCount = live;

        for (size_t i = 0; i < cellCount; i++)
        {
            if (start[i + 1] == start[i])
                continue;

            Cell cell;
            cell.first = start[i];
            cell.count = start[i + 1] - start[i];
            cell.min = vec3(std::numeric_limits<float>::max());
            cell.max = vec3(std::numeric_limits<float>::lowest());
            for (uint32_t slot = cell.first; slot < cell.first + cell.count; slot++)
            {
                const vec3 p(m_instances[slot].x, m_instances[slot].y, m_instances[slot].z);
                cell.min = glm::min(cell.min, p);
                cell.max = glm::max(cell.max, p);
            }
            cell.min -= vec3(kReach, 0.f, kReach);
            cell.max += vec3(kReach, kReach, kReach);
            m_cells.push_back(cell);
        }
        m_cellDirty.assign(m_cells.size(), 0);
    }

    void HingeField::EnsureCapacity(size_t bytes)
    {
        if (m_buffer && m_buffer->Size() >= bytes)
            return;

        if (m_buffer)
            RHII.AddToDeletionQueue([b = m_buffer]() mutable { Buffer::Destroy(b); });

        BufferDesc desc{};
        desc.size = std::max(bytes + bytes / 2, kMinBufferBytes);
        desc.usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST;
        desc.memoryUsage = PE_MEMORY_USAGE_GPU_ONLY;
        desc.name = "HingeField_Instances";
        m_buffer = Buffer::Create(desc);
        m_fullUpload = true;
    }

    void HingeField::RecordUploads(CommandBuffer *cmd)
    {
        if (!cmd)
            return;

        if (m_layoutDirty)
            RebuildLayout();
        if (!m_fullUpload && !m_tailDirty && m_dirtyCells.empty())
            return;

        const size_t bytes = m_instances.size() * sizeof(Instance);
        m_gpuCount = static_cast<uint32_t>(m_instances.size());
        if (bytes == 0)
        {
            m_fullUpload = false;
            return;
        }

        EnsureCapacity(bytes);

        // The previous frame's draws may still be reading these ranges on this queue.
        MemoryBarrierInfo before{};
        before.srcStageMask = PE_STAGE_ALL_COMMANDS;
        before.srcAccessMask = PE_ACCESS_MEMORY_READ | PE_ACCESS_MEMORY_WRITE;
        before.dstStageMask = PE_STAGE_TRANSFER;
        before.dstAccessMask = PE_ACCESS_TRANSFER_WRITE;
        cmd->MemoryBarrier(before);

        if (m_fullUpload)
        {
            cmd->CopyBufferStaged(m_buffer, m_instances.data(), bytes, 0);
        }
        else
        {
            std::sort(m_dirtyCells.begin(), m_dirtyCells.end());
            const auto upload = [&](size_t begin, size_t end)
            {
                auto *source = reinterpret_cast<uint8_t *>(m_instances.data()) + begin;
                cmd->CopyBufferStaged(m_buffer, source, end - begin, begin);
            };

            size_t begin = 0, end = 0;
            for (uint32_t index : m_dirtyCells)
            {
                const Cell &cell = m_cells[index];
                const size_t cellBegin = cell.first * sizeof(Instance);
                const size_t cellEnd = cellBegin + cell.count * sizeof(Instance);
                if (end > begin && cellBegin <= end + kMergeGapBytes)
                {
                    end = cellEnd;
                    continue;
                }
                if (end > begin)
                    upload(begin, end);
                begin = cellBegin;
                end = cellEnd;
            }
            if (end > begin)
                upload(begin, end);
            if (m_tailDirty)
                upload(m_sortedCount * sizeof(Instance), bytes);
        }

        BufferBarrierInfo after{};
        after.buffer = m_buffer;
        after.stageMask = PE_STAGE_VERTEX_SHADER;
        after.accessMask = PE_ACCESS_SHADER_READ;
        after.size = m_buffer->Size();
        cmd->BufferBarrier(after);

        for (uint32_t index : m_dirtyCells)
            m_cellDirty[index] = 0;
        m_dirtyCells.clear();
        m_tailDirty = false;
        m_fullUpload = false;
    }
} // namespace pe
