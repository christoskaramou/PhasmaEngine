#pragma once

namespace pe
{
    class Buffer;
    class CommandBuffer;

    // Up to millions of identical small boxes standing on the ground, drawn as instances from one GPU buffer with no
    // scene node each. A box stands until it is told when to topple over one of its bottom edges; the vertex shader
    // then animates the fall from the clock, so a standing or falling box costs the CPU nothing per frame and a chain
    // reaction pays only for the boxes it starts. Instances are kept sorted into spatial cells for culling.
    class HingeField
    {
    public:
        // Matches HingeInstance in Shaders/Field/HingeField.hlsl.
        struct Instance
        {
            float x, y, z, yaw;
            float tStart, tContact;
            uint32_t color; // RGBA8
            uint32_t state; // contact 8 | final 8 | generation 14 | backward 1 | hidden 1
        };

        static constexpr uint32_t kNearVertices = 36; // the whole box
        static constexpr uint32_t kFarVertices = 18;  // the three faces a box can show

        ~HingeField();

        void Clear();
        // Adds `count` standing boxes for the caller to fill through the returned pointer; firstId is the first's id.
        Instance *Append(uint32_t count, uint32_t &firstId);
        void Hide(uint32_t id);
        void Fall(uint32_t id, float tStart, float tContact, float contact, float final, bool backward);
        void StandAll();
        void SetClock(float time);
        void SetNearDistance(float distance) { m_nearDistance = distance; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        void SetCastsShadows(bool casts) { m_castsShadows = casts; }
        void RequestRebuild() { m_layoutDirty = true; }

        // At the front of the frame's command buffer: lays out and uploads what changed.
        void RecordUploads(CommandBuffer *cmd);

        bool Ready() const { return m_enabled && m_buffer && m_gpuCount > 0; }
        bool CastsShadows() const { return m_castsShadows; }
        float GetNearDistance() const { return m_nearDistance; }
        Buffer *GetBuffer() const { return m_buffer; }
        uint32_t GetCount() const { return static_cast<uint32_t>(m_instances.size()); }
        uint32_t GetGeneration() const { return m_generation; }
        float GetClock() const { return m_clock; }
        float GetPreviousClock() const { return m_previousClock; }

        static std::array<vec4, 6> PlanesFromMatrix(const mat4 &m);

        // draw(first, count, vertices) for every run of instances inside these planes.
        template <class Fn>
        void ForEachVisible(const std::array<vec4, 6> &planes, const vec3 &eye, Fn &&draw) const
        {
            uint32_t first = 0, count = 0, vertices = kNearVertices;
            const auto flush = [&]()
            {
                if (count)
                    draw(first, count, vertices);
                count = 0;
            };

            const float nearSq = m_nearDistance * m_nearDistance;
            for (const Cell &cell : m_cells)
            {
                if (!CellVisible(planes, cell))
                {
                    flush();
                    continue;
                }

                const vec3 closest = glm::clamp(eye, cell.min, cell.max);
                const uint32_t want = glm::dot(eye - closest, eye - closest) <= nearSq ? kNearVertices : kFarVertices;
                if (count && want == vertices && first + count == cell.first)
                {
                    count += cell.count;
                    continue;
                }

                flush();
                first = cell.first;
                count = cell.count;
                vertices = want;
            }
            flush();

            if (m_gpuCount > m_sortedCount)
                draw(m_sortedCount, m_gpuCount - m_sortedCount, kNearVertices);
        }

    private:
        struct Cell
        {
            uint32_t first = 0;
            uint32_t count = 0;
            vec3 min = vec3(0.f);
            vec3 max = vec3(0.f);
        };

        static bool CellVisible(const std::array<vec4, 6> &planes, const Cell &cell)
        {
            for (const vec4 &plane : planes)
            {
                const vec3 corner(plane.x >= 0.f ? cell.max.x : cell.min.x,
                                  plane.y >= 0.f ? cell.max.y : cell.min.y,
                                  plane.z >= 0.f ? cell.max.z : cell.min.z);
                if (glm::dot(vec3(plane), corner) + plane.w < 0.f)
                    return false;
            }
            return true;
        }

        void TouchSlot(uint32_t slot);
        void RebuildLayout();
        void EnsureCapacity(size_t bytes);

        std::vector<Instance> m_instances; // draw order: [0, m_sortedCount) by cell, then the unsorted tail
        std::vector<uint32_t> m_slotOfId;
        std::vector<uint32_t> m_idOfSlot;
        std::vector<Cell> m_cells;
        std::vector<uint8_t> m_cellDirty;
        std::vector<uint32_t> m_dirtyCells;
        Buffer *m_buffer = nullptr;
        uint32_t m_sortedCount = 0;
        uint32_t m_gpuCount = 0;
        uint32_t m_generation = 1;
        float m_clock = 0.f;
        float m_previousClock = 0.f;
        float m_nearDistance = 150.f;
        bool m_tailDirty = false;
        bool m_layoutDirty = false;
        bool m_fullUpload = false;
        bool m_enabled = true;
        bool m_castsShadows = true;
    };
} // namespace pe
