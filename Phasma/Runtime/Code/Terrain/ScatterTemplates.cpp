#include "Terrain/ScatterTemplates.h"

namespace pe::terrain
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        vec3 Pos(const Vertex &v)
        {
            return vec3(v.position[0], v.position[1], v.position[2]);
        }

        void SetPos(Vertex &v, const vec3 &p)
        {
            v.position[0] = p.x;
            v.position[1] = p.y;
            v.position[2] = p.z;
        }

        void SetNormal(Vertex &v, const vec3 &n)
        {
            v.normals[0] = n.x;
            v.normals[1] = n.y;
            v.normals[2] = n.z;
        }

        void SetColor(Vertex &v, const vec3 &c)
        {
            v.color[0] = c.x;
            v.color[1] = c.y;
            v.color[2] = c.z;
            v.color[3] = 1.0f;
        }

        Vertex MakeVertex(const vec3 &p, const vec3 &color, const vec2 &uv)
        {
            Vertex v{};
            SetPos(v, p);
            v.uv[0] = uv.x;
            v.uv[1] = uv.y;
            SetColor(v, color);
            v.tangent[0] = 1.0f;
            v.tangent[3] = 1.0f;
            return v;
        }

        // Appends one flat-shaded triangle; (a, b, c) must already be wound CCW as seen from
        // outside (TerrainWorld's FrontFace=CW + cull FRONT convention — see TerrainWorld.cpp
        // MeshTileGrid), since the face normal is derived from that order.
        void AddTri(ScatterTemplate &t, Vertex a, Vertex b, Vertex c)
        {
            const vec3 n = normalize(cross(Pos(b) - Pos(a), Pos(c) - Pos(a)));
            SetNormal(a, n);
            SetNormal(b, n);
            SetNormal(c, n);

            const uint32_t base = static_cast<uint32_t>(t.vertices.size());
            t.vertices.push_back(a);
            t.vertices.push_back(b);
            t.vertices.push_back(c);
            t.indices.push_back(base);
            t.indices.push_back(base + 1);
            t.indices.push_back(base + 2);
        }

        // A horizontal ring of `segments` points at height `y`, angle increasing CCW as seen from
        // above (matches the winding the side-wall / cap helpers below assume).
        std::vector<vec3> MakeRing(float radius, float y, int segments)
        {
            std::vector<vec3> pts(segments);
            for (int i = 0; i < segments; ++i)
            {
                const float a = 2.0f * kPi * float(i) / float(segments);
                pts[i] = vec3(radius * std::cos(a), y, radius * std::sin(a));
            }
            return pts;
        }

        // A degenerate "ring" that is really a single point (a cone apex), sized to line up with
        // MakeRing's segment count so it can be fed to AddSideWall as either end.
        std::vector<vec3> MakeApexRing(const vec3 &apex, int segments)
        {
            return std::vector<vec3>(static_cast<size_t>(segments), apex);
        }

        // Tube side wall between a lower ring (`bottom`) and a higher ring (`top`), same segment
        // count. Two triangles per segment, wound outward (verified against the engine's
        // CCW-from-outside convention for a plain tapered cylinder).
        void AddSideWall(ScatterTemplate &t, const std::vector<vec3> &bottom, const std::vector<vec3> &top,
                         const vec3 &colorBottom, const vec3 &colorTop, float vBottom, float vTop)
        {
            const int n = static_cast<int>(bottom.size());
            for (int i = 0; i < n; ++i)
            {
                const int j = (i + 1) % n;
                const float u0 = float(i) / float(n);
                const float u1 = float(i + 1) / float(n);
                Vertex b0 = MakeVertex(bottom[i], colorBottom, vec2(u0, vBottom));
                Vertex b1 = MakeVertex(bottom[j], colorBottom, vec2(u1, vBottom));
                Vertex t0 = MakeVertex(top[i], colorTop, vec2(u0, vTop));
                Vertex t1 = MakeVertex(top[j], colorTop, vec2(u1, vTop));
                AddTri(t, b0, t0, b1);
                AddTri(t, b1, t0, t1);
            }
        }

        // Fan cap sealing a ring against its center, facing either up-and-out (`outwardUp`) or
        // down-and-out.
        void AddCapDisc(ScatterTemplate &t, const vec3 &center, const std::vector<vec3> &ring, const vec3 &color,
                        bool outwardUp)
        {
            const int n = static_cast<int>(ring.size());
            for (int i = 0; i < n; ++i)
            {
                const int j = (i + 1) % n;
                Vertex vc = MakeVertex(center, color, vec2(0.5f, 0.5f));
                Vertex vi = MakeVertex(ring[i], color, vec2(0.5f + 0.5f * ring[i].x, 0.5f + 0.5f * ring[i].z));
                Vertex vj = MakeVertex(ring[j], color, vec2(0.5f + 0.5f * ring[j].x, 0.5f + 0.5f * ring[j].z));
                if (outwardUp)
                    AddTri(t, vc, vj, vi);
                else
                    AddTri(t, vc, vi, vj);
            }
        }

        // A point on a unit sphere: phi is the polar angle (0 at the +Y pole, pi at the -Y pole),
        // theta the azimuth (same CCW-from-above convention as MakeRing).
        vec3 SpherePoint(float phi, float theta)
        {
            const float sp = std::sin(phi);
            const float cp = std::cos(phi);
            return vec3(sp * std::cos(theta), cp, sp * std::sin(theta));
        }

        ScatterTemplate BuildTree()
        {
            ScatterTemplate t;
            t.collide = true;

            constexpr int kTrunkSeg = 6;
            const vec3 trunkColor(0.34f, 0.24f, 0.14f);
            const std::vector<vec3> trunkBottom = MakeRing(0.16f, 0.0f, kTrunkSeg);
            const std::vector<vec3> trunkTop = MakeRing(0.10f, 1.6f, kTrunkSeg);
            AddSideWall(t, trunkBottom, trunkTop, trunkColor, trunkColor, 0.0f, 1.0f);
            // No bottom cap (buried) and no top cap: the lower canopy cone fully encloses it.

            // Canopy: two stacked cones (classic layered-conifer silhouette), y 1.2 .. 3.4, the
            // lower one carrying the max radius (~1.1), the upper one slightly lighter.
            constexpr int kCanopySeg = 6;
            const vec3 lowerColor(0.20f, 0.44f, 0.17f);
            const vec3 upperColor(0.24f, 0.50f, 0.20f);

            const std::vector<vec3> lowerBase = MakeRing(1.1f, 1.2f, kCanopySeg);
            const std::vector<vec3> lowerApex = MakeApexRing(vec3(0.0f, 2.4f, 0.0f), kCanopySeg);
            AddSideWall(t, lowerBase, lowerApex, lowerColor, lowerColor, 0.0f, 1.0f);
            AddCapDisc(t, vec3(0.0f, 1.2f, 0.0f), lowerBase, lowerColor, false);

            const std::vector<vec3> upperBase = MakeRing(0.75f, 2.0f, kCanopySeg);
            const std::vector<vec3> upperApex = MakeApexRing(vec3(0.0f, 3.4f, 0.0f), kCanopySeg);
            AddSideWall(t, upperBase, upperApex, upperColor, upperColor, 0.0f, 1.0f);
            AddCapDisc(t, vec3(0.0f, 2.0f, 0.0f), upperBase, upperColor, false);

            return t;
        }

        ScatterTemplate BuildRock()
        {
            ScatterTemplate t;
            t.collide = true;

            constexpr int kSegs = 6;
            constexpr int kStacks = 4;
            const vec3 color(0.44f, 0.42f, 0.39f);
            const float baseRadius = 0.55f;
            const float yScale = 0.65f;

            // Row 0 = top pole, row kStacks = bottom pole; rows in between are latitude rings.
            // Each vertex is displaced radially by a deterministic hash of its flattened index —
            // poles reuse a single hash per row so they stay a closed point (no polar seam).
            std::vector<std::vector<vec3>> rings(kStacks + 1, std::vector<vec3>(kSegs));
            float minY = 0.0f;
            for (int row = 0; row <= kStacks; ++row)
            {
                const float phi = kPi * float(row) / float(kStacks);
                const bool pole = (row == 0 || row == kStacks);
                for (int seg = 0; seg < kSegs; ++seg)
                {
                    const int hashIndex = pole ? row * kSegs : row * kSegs + seg;
                    const uint32_t h = static_cast<uint32_t>(hashIndex) * 2654435761u;
                    const float factor = 0.75f + float(h % 1000u) / 1000.0f * 0.4f;
                    const float theta = 2.0f * kPi * float(seg) / float(kSegs);
                    vec3 p = SpherePoint(phi, theta) * (baseRadius * factor);
                    p.y *= yScale;
                    rings[row][seg] = p;
                    minY = std::min(minY, p.y);
                }
            }
            const float shiftY = -0.08f - minY; // sink the bottom to ~-0.08 so it embeds in the ground
            for (std::vector<vec3> &ring : rings)
                for (vec3 &p : ring)
                    p.y += shiftY;

            for (int row = 0; row < kStacks; ++row)
                AddSideWall(t, rings[row + 1], rings[row], color, color, 0.0f, 1.0f);

            return t;
        }

        // One blade cross-quad: a vertical quad through the Y axis at `angle`, emitted twice with
        // opposite winding/normals so it reads from both sides (like TerrainWorld's skirt quads).
        void AddCrossQuad(ScatterTemplate &t, float angle)
        {
            const vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
            const float halfWidth = 0.35f; // 0.7 wide
            const float height = 0.5f;
            const vec3 p0 = -dir * halfWidth;                            // base, -dir side
            const vec3 p1 = dir * halfWidth;                             // base, +dir side
            const vec3 p2 = dir * halfWidth + vec3(0.0f, height, 0.0f);  // tip, +dir side
            const vec3 p3 = -dir * halfWidth + vec3(0.0f, height, 0.0f); // tip, -dir side
            const vec3 colorBase(0.28f, 0.44f, 0.18f);
            const vec3 colorTip(0.42f, 0.58f, 0.22f);
            const vec3 n(-dir.z, 0.0f, dir.x); // perpendicular to dir in the XZ plane

            auto emit = [&](const vec3 &normal, bool flip)
            {
                Vertex v0 = MakeVertex(p0, colorBase, vec2(0.0f, 0.0f));
                Vertex v1 = MakeVertex(p1, colorBase, vec2(1.0f, 0.0f));
                Vertex v2 = MakeVertex(p2, colorTip, vec2(1.0f, 1.0f));
                Vertex v3 = MakeVertex(p3, colorTip, vec2(0.0f, 1.0f));
                SetNormal(v0, normal);
                SetNormal(v1, normal);
                SetNormal(v2, normal);
                SetNormal(v3, normal);

                const uint32_t base = static_cast<uint32_t>(t.vertices.size());
                t.vertices.push_back(v0);
                t.vertices.push_back(v1);
                t.vertices.push_back(v2);
                t.vertices.push_back(v3);
                if (!flip)
                {
                    const uint32_t tris[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
                    t.indices.insert(t.indices.end(), tris, tris + 6);
                }
                else
                {
                    const uint32_t tris[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
                    t.indices.insert(t.indices.end(), tris, tris + 6);
                }
            };

            emit(n, false);
            emit(-n, true);
        }

        ScatterTemplate BuildGrass()
        {
            ScatterTemplate t;
            t.collide = false;
            for (int i = 0; i < 3; ++i)
                AddCrossQuad(t, kPi * float(i) / 3.0f); // 0, 60, 120 degrees
            return t;
        }
    } // namespace

    ScatterTemplate BuildScatterTemplate(const std::string &name)
    {
        if (name == "tree")
            return BuildTree();
        if (name == "rock")
            return BuildRock();
        if (name == "grass")
            return BuildGrass();
        return ScatterTemplate{};
    }
} // namespace pe::terrain
