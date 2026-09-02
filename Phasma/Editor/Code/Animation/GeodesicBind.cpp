#include "Animation/GeodesicBind.h"

namespace pe::GeodesicBind
{
    namespace
    {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        constexpr int kRingDirections = 12;
        constexpr float kSampleSpacing = 0.5f; // axis samples every this many mean radii
        constexpr float kReach = 1.25f;        // a seed ray may leave the mesh this many radii from the axis
        constexpr float kBand = 1.5f;          // blend band on each side of a seam, in thinner-bone radii...
        constexpr float kBandOfLength = 0.35f; // ...capped by this share of the shorter bone (girth-fitted torso capsules)
        constexpr int kSmoothPasses = 2;

        struct Graph
        {
            std::vector<uint32_t> offsets, adjacent;
            std::vector<float> length;
        };

        struct Hit
        {
            float t = kInf;
            uint32_t tri = 0;
            int count = 0; // every crossing along the ray: odd = the origin sits inside the mesh
        };

        uint64_t Key(const vec3 &p, float q)
        {
            const auto x = static_cast<uint64_t>(std::llround(p.x / q));
            const auto y = static_cast<uint64_t>(std::llround(p.y / q));
            const auto z = static_cast<uint64_t>(std::llround(p.z / q));
            uint64_t h = x * 0x9E3779B97F4A7C15ull;
            h ^= y * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
            h ^= z * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
            return h;
        }

        // ponytail: every triangle per ray (~0.2 s for a 40k-triangle hero at bake); a triangle grid is the upgrade.
        Hit Cast(std::span<const vec3> P, std::span<const uint32_t> tris, const vec3 &origin, const vec3 &dir)
        {
            Hit hit;
            for (size_t k = 0; k + 2 < tris.size(); k += 3)
            {
                const vec3 &a = P[tris[k]], &b = P[tris[k + 1]], &c = P[tris[k + 2]];
                const vec3 e1 = b - a, e2 = c - a, pv = glm::cross(dir, e2);
                const float det = glm::dot(e1, pv);
                if (std::abs(det) < 1e-12f)
                    continue;
                const float inv = 1.f / det;
                const vec3 tv = origin - a;
                const float u = glm::dot(tv, pv) * inv;
                if (u < 0.f || u > 1.f)
                    continue;
                const vec3 qv = glm::cross(tv, e1);
                const float v = glm::dot(dir, qv) * inv;
                if (v < 0.f || u + v > 1.f)
                    continue;
                const float t = glm::dot(e2, qv) * inv;
                if (t <= 1e-6f)
                    continue;
                hit.count++;
                if (t < hit.t)
                {
                    hit.t = t;
                    hit.tri = static_cast<uint32_t>(k / 3);
                }
            }
            return hit;
        }

        using Queue = std::priority_queue<std::pair<float, uint32_t>, std::vector<std::pair<float, uint32_t>>,
                                          std::greater<>>;

        void Dijkstra(const Graph &g, float *dist, Queue &queue)
        {
            while (!queue.empty())
            {
                const auto [d, v] = queue.top();
                queue.pop();
                if (d > dist[v])
                    continue;
                for (uint32_t e = g.offsets[v]; e < g.offsets[v + 1]; e++)
                {
                    const uint32_t u = g.adjacent[e];
                    const float nd = d + g.length[e];
                    if (nd < dist[u])
                    {
                        dist[u] = nd;
                        queue.emplace(nd, u);
                    }
                }
            }
        }

        // parent, grandparent, child, grandchild or sibling: the bones a seam may blend across
        bool Near(std::span<const Bone> bones, int b, int n)
        {
            const int pb = bones[b].parent, gb = pb >= 0 ? bones[pb].parent : -1;
            const int pn = bones[n].parent, gn = pn >= 0 ? bones[pn].parent : -1;
            return n == pb || (gb >= 0 && n == gb) || pn == b || (gn >= 0 && gn == b) || (pb >= 0 && pn == pb);
        }

        void TopFour(std::vector<std::pair<float, int>> &scratch, VertexWeights &out)
        {
            std::partial_sort(scratch.begin(), scratch.begin() + std::min<size_t>(4, scratch.size()), scratch.end(),
                              [](const auto &a, const auto &b)
                              { return a.first > b.first; });
            float sum = 0.f;
            for (size_t k = 0; k < 4 && k < scratch.size(); k++)
                sum += scratch[k].first;
            out.joints = {};
            out.weights = {};
            for (size_t k = 0; k < 4 && k < scratch.size(); k++)
            {
                out.joints[k] = scratch[k].second;
                out.weights[k] = sum > 0.f ? scratch[k].first / sum : 0.f;
            }
        }
    } // namespace

    const VertexWeights *Result::At(size_t vertex) const
    {
        if (vertex >= weld.size() || weld[vertex] >= welded.size() || welded[weld[vertex]].label < 0)
            return nullptr;
        return &welded[weld[vertex]];
    }

    const VertexWeights *Result::Find(const vec3 &p) const
    {
        if (quantum <= 0.f)
            return nullptr;
        // a position that came through another matrix path may round into the next cell: probe the 27 around it
        constexpr float offset[3] = {0.f, -1.f, 1.f};
        for (int dz = 0; dz < 3; dz++)
            for (int dy = 0; dy < 3; dy++)
                for (int dx = 0; dx < 3; dx++)
                {
                    const auto it = byKey.find(Key(p + vec3(offset[dx], offset[dy], offset[dz]) * quantum, quantum));
                    if (it != byKey.end() && welded[it->second].label >= 0)
                        return &welded[it->second];
                    if (dx == 0 && dy == 0 && dz == 0 && it != byKey.end())
                        return nullptr; // the exact cell exists but no bone reaches it
                }
        return nullptr;
    }

    Result Solve(std::span<const vec3> positions, std::span<const uint32_t> triangles, std::span<const Bone> bones,
                 float modelHeight)
    {
        Result r;
        r.quantum = std::max(modelHeight, 1e-3f) * 1e-5f;
        r.seedCount.assign(bones.size(), 0);
        if (positions.empty() || triangles.size() < 3 || bones.empty())
            return r;

        // weld coincident vertices (UV and normal seams split them) so distance can cross every seam
        std::vector<vec3> P;
        r.weld.assign(positions.size(), 0);
        r.byKey.reserve(positions.size());
        for (size_t v = 0; v < positions.size(); v++)
        {
            const auto [it, inserted] = r.byKey.try_emplace(Key(positions[v], r.quantum), static_cast<uint32_t>(P.size()));
            if (inserted)
                P.push_back(positions[v]);
            r.weld[v] = it->second;
        }
        const size_t V = P.size(), B = bones.size();

        std::vector<uint32_t> tris;
        tris.reserve(triangles.size());
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        edges.reserve(triangles.size());
        for (size_t k = 0; k + 2 < triangles.size(); k += 3)
        {
            if (triangles[k] >= positions.size() || triangles[k + 1] >= positions.size() || triangles[k + 2] >= positions.size())
                continue;
            const uint32_t w[3] = {r.weld[triangles[k]], r.weld[triangles[k + 1]], r.weld[triangles[k + 2]]};
            if (w[0] == w[1] || w[1] == w[2] || w[0] == w[2])
                continue;
            for (int c = 0; c < 3; c++)
            {
                tris.push_back(w[c]);
                edges.emplace_back(std::min(w[c], w[(c + 1) % 3]), std::max(w[c], w[(c + 1) % 3]));
            }
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        Graph g;
        g.offsets.assign(V + 1, 0);
        for (const auto &[a, b] : edges)
        {
            g.offsets[a + 1]++;
            g.offsets[b + 1]++;
        }
        for (size_t v = 0; v < V; v++)
            g.offsets[v + 1] += g.offsets[v];
        g.adjacent.resize(edges.size() * 2);
        g.length.resize(edges.size() * 2);
        std::vector<uint32_t> cursor(g.offsets.begin(), g.offsets.end() - 1);
        for (const auto &[a, b] : edges)
        {
            const float len = glm::length(P[a] - P[b]);
            g.adjacent[cursor[a]] = b;
            g.length[cursor[a]++] = len;
            g.adjacent[cursor[b]] = a;
            g.length[cursor[b]++] = len;
        }

        // seeds: from samples along each axis, rays around the bone that leave the mesh within the capsule
        std::vector<float> dist(B * V, kInf);
        std::vector<float> meanRadius(B), boneLength(B);
        for (size_t i = 0; i < B; i++)
        {
            const Bone &b = bones[i];
            float *d = dist.data() + i * V;
            Queue queue;
            const vec3 axis = b.tail - b.head;
            const float L = glm::length(axis);
            boneLength[i] = L;
            const vec3 y = L > 1e-6f ? axis / L : vec3(0.f, 1.f, 0.f);
            vec3 u = std::abs(y.x) < 0.9f ? glm::cross(y, vec3(1.f, 0.f, 0.f)) : glm::cross(y, vec3(0.f, 0.f, 1.f));
            u = glm::normalize(u);
            const vec3 w = glm::cross(y, u);
            meanRadius[i] = 0.5f * (b.headRadius + b.tailRadius);
            const int samples = std::clamp(static_cast<int>(std::ceil(L / std::max(kSampleSpacing * meanRadius[i], 1e-4f))) + 1, 2, 24);
            for (int s = 0; s < samples; s++)
            {
                const float t = static_cast<float>(s) / static_cast<float>(samples - 1);
                const vec3 origin = b.head + axis * t;
                const float reach = kReach * glm::mix(b.headRadius, b.tailRadius, t) + r.quantum;
                auto shoot = [&](const vec3 &dir)
                {
                    const Hit hit = Cast(P, tris, origin, dir);
                    if ((hit.count & 1) == 0 || hit.t > reach)
                        return; // outside the mesh, or the mesh is thicker here than the capsule says
                    const vec3 at = origin + dir * hit.t;
                    for (int c = 0; c < 3; c++)
                    {
                        const uint32_t v = tris[hit.tri * 3 + c];
                        const float d0 = glm::length(P[v] - at);
                        if (d0 < d[v])
                        {
                            d[v] = d0;
                            queue.emplace(d0, v);
                        }
                    }
                    r.seedCount[i]++;
                };
                for (int k = 0; k < kRingDirections; k++)
                {
                    const float a = glm::two_pi<float>() * static_cast<float>(k) / kRingDirections;
                    shoot(u * std::cos(a) + w * std::sin(a));
                }
                if (s == 0)
                    shoot(-y);
                if (s == samples - 1)
                    shoot(y);
            }
            Dijkstra(g, d, queue);
        }

        // label = nearest bone along the surface; blend with near bones inside the seam band
        r.welded.assign(V, {});
        std::vector<std::pair<float, int>> scratch;
        for (size_t v = 0; v < V; v++)
        {
            int best = -1;
            float bestDist = kInf;
            for (size_t i = 0; i < B; i++)
                if (dist[i * V + v] < bestDist)
                {
                    bestDist = dist[i * V + v];
                    best = static_cast<int>(i);
                }
            VertexWeights &out = r.welded[v];
            out.label = best;
            if (best < 0)
            {
                r.unreached++;
                continue;
            }
            scratch.clear();
            scratch.emplace_back(1.f, best);
            if (!bones[best].rigid && !bones[best].spline)
                for (size_t n = 0; n < B; n++)
                {
                    const float dn = dist[n * V + v];
                    if (static_cast<int>(n) == best || dn == kInf || !Near(bones, best, static_cast<int>(n)))
                        continue;
                    const float band = std::max(std::min(kBand * std::min(meanRadius[best], meanRadius[n]),
                                                         kBandOfLength * std::min(boneLength[best], boneLength[n])),
                                                1e-6f);
                    const float x = (dn - bestDist) / band;
                    if (x < 1.f)
                        scratch.emplace_back(1.f - x * x * (3.f - 2.f * x), static_cast<int>(n));
                }
            TopFour(scratch, out);
        }

        // a light relax evens out the seed lattice; rigid and chain vertices stay, far bones stay out
        std::vector<VertexWeights> next(V);
        std::vector<float> accumulate(B, 0.f);
        for (int pass = 0; pass < kSmoothPasses; pass++)
        {
            for (size_t v = 0; v < V; v++)
            {
                const VertexWeights &self = r.welded[v];
                next[v] = self;
                if (self.label < 0 || bones[self.label].rigid || bones[self.label].spline)
                    continue;
                int neighbours = 0;
                for (uint32_t e = g.offsets[v]; e < g.offsets[v + 1]; e++)
                    neighbours += r.welded[g.adjacent[e]].label >= 0;
                if (neighbours == 0)
                    continue;
                std::fill(accumulate.begin(), accumulate.end(), 0.f);
                for (int k = 0; k < 4; k++)
                    accumulate[self.joints[k]] += 0.5f * self.weights[k];
                for (uint32_t e = g.offsets[v]; e < g.offsets[v + 1]; e++)
                {
                    const VertexWeights &other = r.welded[g.adjacent[e]];
                    if (other.label < 0)
                        continue;
                    for (int k = 0; k < 4; k++)
                        if (other.weights[k] > 0.f && (other.joints[k] == self.label || Near(bones, self.label, other.joints[k])))
                            accumulate[other.joints[k]] += 0.5f * other.weights[k] / static_cast<float>(neighbours);
                }
                scratch.clear();
                for (size_t i = 0; i < B; i++)
                    if (accumulate[i] > 0.f)
                        scratch.emplace_back(accumulate[i], static_cast<int>(i));
                TopFour(scratch, next[v]);
            }
            r.welded.swap(next);
        }
        return r;
    }
} // namespace pe::GeodesicBind
