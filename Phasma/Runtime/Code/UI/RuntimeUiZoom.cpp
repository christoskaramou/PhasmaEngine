#include "UI/RuntimeUiZoom.h"

namespace pe
{
    namespace
    {
        constexpr size_t kMaxQuads = 800;     // grouping compares every pair
        constexpr float kBackdropArea = 0.6f; // of the surface: larger quads are backdrops
        constexpr float kGroupGap = 0.012f;   // of the surface height: closer quads belong together
        constexpr float kEdgeMargin = 24.0f;  // a group keeps at most this much of its distance to the edges
        constexpr float kZoomStep = 0.93f;
        constexpr int kMaxPasses = 24;
        constexpr float kMaxTextBoost = 1.3f; // given to every quad; beyond it text grows only while it fits its box

        struct Group
        {
            float minX = FLT_MAX;
            float minY = FLT_MAX;
            float maxX = -FLT_MAX;
            float maxY = -FLT_MAX;
            bool content = false; // false: only moving decorations, which stay as the game drew them
            RuntimeUiZoomPlacement placement{};
        };

        int Root(std::vector<int> &parent, int index)
        {
            while (parent[index] != index)
            {
                parent[index] = parent[parent[index]];
                index = parent[index];
            }

            return index;
        }

        bool Near(const RuntimeUiZoomRect &a, const RuntimeUiZoomRect &b, float gap)
        {
            const bool overlapX = a.x < b.x + b.width + gap && b.x < a.x + a.width + gap;
            const bool overlapY = a.y < b.y + b.height + gap && b.y < a.y + a.height + gap;
            return overlapX && overlapY;
        }

        bool Contains(const RuntimeUiZoomRect &outer, const RuntimeUiZoomRect &inner)
        {
            const bool insideX = inner.x >= outer.x - 1.0f && inner.x + inner.width <= outer.x + outer.width + 1.0f;
            const bool insideY = inner.y >= outer.y - 1.0f && inner.y + inner.height <= outer.y + outer.height + 1.0f;
            return insideX && insideY;
        }

        // Quads belong together when they are close. A moving decoration (a health bar following a unit) only goes
        // with a quad it lies inside or encloses, so it never changes a group's bounds as it drifts across the HUD.
        bool Linked(const RuntimeUiZoomRect &a, const RuntimeUiZoomRect &b, float gap)
        {
            if (a.content && b.content)
                return Near(a, b, gap);

            return Contains(a, b) || Contains(b, a);
        }

        // The group of each rect (-1: not zoomed) and the groups' bounds.
        std::vector<int> BuildGroups(const std::vector<RuntimeUiZoomRect> &rects, float gap, std::vector<Group> &groups)
        {
            const int count = static_cast<int>(rects.size());
            std::vector<int> parent(count);
            std::iota(parent.begin(), parent.end(), 0);
            for (int i = 0; i < count; ++i)
            {
                for (int j = i + 1; rects[i].eligible && j < count; ++j)
                {
                    if (rects[j].eligible && Linked(rects[i], rects[j], gap))
                        parent[Root(parent, j)] = Root(parent, i);
                }
            }

            std::vector<int> groupOf(count, -1);
            std::unordered_map<int, int> groupOfRoot;
            for (int i = 0; i < count; ++i)
            {
                if (!rects[i].eligible)
                    continue;

                const auto [it, added] = groupOfRoot.try_emplace(Root(parent, i), static_cast<int>(groups.size()));
                if (added)
                    groups.emplace_back();

                Group &group = groups[it->second];
                group.minX = std::min(group.minX, rects[i].x);
                group.minY = std::min(group.minY, rects[i].y);
                group.maxX = std::max(group.maxX, rects[i].x + rects[i].width);
                group.maxY = std::max(group.maxY, rects[i].y + rects[i].height);
                group.content = group.content || rects[i].content;
                groupOf[i] = it->second;
            }

            return groupOf;
        }

        // One axis: the scaled span stays centred where it was, then moves inside [margin, size - margin].
        float PlaceAxis(float min, float max, float scale, float margin, float size)
        {
            const float span = (max - min) * scale;
            const float centred = (min + max) * 0.5f - span * 0.5f;
            const float placed = std::clamp(centred, margin, std::max(margin, size - margin - span));
            return placed - min * scale;
        }

        void Place(Group &group, float zoom, float width, float height)
        {
            const float spanX = std::max(group.maxX - group.minX, 1.0f);
            const float spanY = std::max(group.maxY - group.minY, 1.0f);
            const float toEdge = std::min({group.minX, group.minY, width - group.maxX, height - group.maxY});
            const float margin = std::clamp(toEdge, 0.0f, kEdgeMargin);
            const float fit = std::min((width - 2.0f * margin) / spanX, (height - 2.0f * margin) / spanY);
            group.placement.scale = std::max(1.0f, std::min(zoom, fit));
            group.placement.offsetX = PlaceAxis(group.minX, group.maxX, group.placement.scale, margin, width);
            group.placement.offsetY = PlaceAxis(group.minY, group.maxY, group.placement.scale, margin, height);
        }

        bool Overlap(const Group &a, const Group &b)
        {
            const RuntimeUiZoomPlacement &p = a.placement;
            const RuntimeUiZoomPlacement &q = b.placement;
            const bool overlapX = a.minX * p.scale + p.offsetX < b.maxX * q.scale + q.offsetX &&
                                  b.minX * q.scale + q.offsetX < a.maxX * p.scale + p.offsetX;
            const bool overlapY = a.minY * p.scale + p.offsetY < b.maxY * q.scale + q.offsetY &&
                                  b.minY * q.scale + q.offsetY < a.maxY * p.scale + p.offsetY;
            return overlapX && overlapY;
        }

        // Lowers the zoom of every group that overlaps another; true when none does.
        bool ShrinkOverlapping(const std::vector<Group> &groups, std::vector<float> &zooms)
        {
            std::vector<bool> shrink(groups.size(), false);
            bool clear = true;
            for (size_t i = 0; i < groups.size(); ++i)
            {
                for (size_t j = i + 1; j < groups.size(); ++j)
                {
                    const bool both = groups[i].content && groups[j].content;
                    if (!both || !Overlap(groups[i], groups[j]))
                        continue;

                    shrink[i] = shrink[j] = true;
                    clear = false;
                }
            }

            for (size_t i = 0; i < groups.size(); ++i)
            {
                if (shrink[i])
                    zooms[i] = std::max(1.0f, zooms[i] * kZoomStep);
            }

            return clear;
        }

        // The part of the zoom the quad did not get as size goes to its text.
        void BoostText(RuntimeUiZoomPlacement &placement, float zoom)
        {
            const float missing = std::max(1.0f, zoom / placement.scale);
            placement.textScale = std::min(missing, kMaxTextBoost);
            placement.textFit = missing / placement.textScale;
        }
    } // namespace

    std::vector<RuntimeUiZoomPlacement> ZoomRuntimeUiQuads(const std::vector<RuntimeUiZoomRect> &source,
                                                           float width,
                                                           float height,
                                                           float zoom)
    {
        std::vector<RuntimeUiZoomPlacement> placements(source.size());
        if (zoom <= 1.0f)
            return placements;

        // Quads that stay where they are still get larger text, as far as it fits them.
        for (RuntimeUiZoomPlacement &placement : placements)
            placement.textFit = zoom;

        if (width <= 0.0f || height <= 0.0f || source.size() > kMaxQuads)
            return placements;

        std::vector<RuntimeUiZoomRect> rects = source;
        for (RuntimeUiZoomRect &rect : rects)
        {
            const bool sized = rect.width > 0.0f && rect.height > 0.0f;
            const bool backdrop = rect.width * rect.height >= kBackdropArea * width * height;
            rect.eligible = rect.eligible && sized && !backdrop;
            rect.holds = rect.holds && sized && !backdrop;
        }

        // A quad centred on a panel that stays put stays with it (text quads are often wider than their panel).
        for (RuntimeUiZoomRect &rect : rects)
        {
            const float centreX = rect.x + rect.width * 0.5f;
            const float centreY = rect.y + rect.height * 0.5f;
            const auto holdsIt = [centreX, centreY](const RuntimeUiZoomRect &other)
            {
                const bool insideX = centreX >= other.x && centreX <= other.x + other.width;
                const bool insideY = centreY >= other.y && centreY <= other.y + other.height;
                return other.holds && insideX && insideY;
            };
            rect.eligible = rect.eligible && std::none_of(rects.begin(), rects.end(), holdsIt);
        }

        std::vector<Group> groups;
        const std::vector<int> groupOf = BuildGroups(rects, kGroupGap * height, groups);

        // Every group starts at the full zoom; the two groups of an overlapping pair shrink until they clear.
        std::vector<float> zooms(groups.size(), zoom);
        bool placed = false;
        for (int pass = 0; pass < kMaxPasses && !placed; ++pass)
        {
            for (size_t i = 0; i < groups.size(); ++i)
            {
                if (groups[i].content)
                    Place(groups[i], zooms[i], width, height);
            }

            placed = ShrinkOverlapping(groups, zooms);
        }

        if (!placed)
            return placements;

        for (size_t i = 0; i < rects.size(); ++i)
        {
            if (groupOf[i] < 0 || !groups[groupOf[i]].content)
                continue;

            placements[i] = groups[groupOf[i]].placement;
            BoostText(placements[i], zoom);
        }

        return placements;
    }
} // namespace pe
