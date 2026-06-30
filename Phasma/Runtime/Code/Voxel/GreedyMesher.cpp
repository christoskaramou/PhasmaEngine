#include "Voxel/GreedyMesher.h"

namespace pe::voxel
{
    namespace
    {
        // Classic 3-sample per-vertex ambient occlusion (Mikola Lysenko / "0fps" scheme).
        // Each face corner is darkened by the air-side neighbours that touch it: the two
        // edge neighbours (side1/side2) and the diagonal corner. Two touching sides fully
        // occlude (level 0); otherwise level = 3 - (side1+side2+corner). Higher = brighter.
        int VertexAO(bool side1, bool side2, bool corner)
        {
            if (side1 && side2)
                return 0;
            return 3 - ((int)side1 + (int)side2 + (int)corner);
        }

        // A merged-quad candidate cell: the tile to draw plus the 4 corner AO levels in
        // pos[]-emit order. Greedy merge requires BOTH tile and all 4 AO to match, so AO
        // never bleeds across a merged quad (conservative — never merges across an AO change).
        struct MaskCell
        {
            int tile; // -1 = no visible face here
            uint8_t ao[4];
        };

        bool CellEq(const MaskCell &a, const MaskCell &b)
        {
            return a.tile == b.tile && a.ao[0] == b.ao[0] && a.ao[1] == b.ao[1] && a.ao[2] == b.ao[2] &&
                   a.ao[3] == b.ao[3];
        }

        // Sweep-and-merge the faces of ONE render class into outVerts/outIdx, re-basing packed
        // positions to the tight local min (returned in mn/mx). transparentPass selects the
        // visibility rule: opaque emits a solid face against a non-opaque neighbour (with AO);
        // transparent emits a water-class face against AIR only (water-water faces cull, water-vs-
        // solid is hidden), unshaded (AO = full bright) — water needs no corner darkening.
        void GreedySweep(BlockSampleFn sample, void *sampleCtx, const BlockRegistry &reg, int lod,
                         bool transparentPass, std::vector<VoxelVertex> &outVerts,
                         std::vector<uint16_t> &outIdx, int mn[3], int mx[3])
        {
            int stride = 1 << lod;
            MaskCell mask[kSectionDim * kSectionDim];
            for (int d = 0; d < 3; d++)
            {
                int u = (d + 1) % 3, v = (d + 2) % 3;

                // Air-side occupancy at axis-d layer `dd`, in-plane (u,v) = (uu,vv). Used for AO.
                auto occ = [&](int dd, int uu, int vv) -> bool
                {
                    int c[3];
                    c[d] = dd;
                    c[u] = uu;
                    c[v] = vv;
                    return reg.IsOpaque(sample(sampleCtx, c[0], c[1], c[2]));
                };

                for (int dir = 1; dir >= -1; dir -= 2)
                {
                    for (int p = 0; p <= kSectionDim; p++)
                    {
                        // Air-side layer along d for AO occluder sampling: +d face sits between solid
                        // at p-1 and air at p; -d face between solid at p and air at p-1.
                        int airD = (dir == 1) ? p : p - 1;

                        // Build mask: check face between block at p-1 and block at p along axis d
                        for (int pu = 0; pu < kSectionDim; pu++)
                        {
                            for (int pv = 0; pv < kSectionDim; pv++)
                            {
                                int cA[3], cB[3];
                                cA[d] = p - 1;
                                cA[u] = pu;
                                cA[v] = pv;
                                cB[d] = p;
                                cB[u] = pu;
                                cB[v] = pv;
                                BlockId bA = sample(sampleCtx, cA[0], cA[1], cA[2]);
                                BlockId bB = sample(sampleCtx, cB[0], cB[1], cB[2]);

                                MaskCell &cell = mask[pu * kSectionDim + pv];
                                bool vis;
                                if (!transparentPass)
                                {
                                    if (dir == 1)
                                    {
                                        vis = reg.IsOpaque(bA) && !reg.IsOpaque(bB);
                                        cell.tile = vis ? (int)reg.FaceTile(bA, d * 2) : -1;
                                    }
                                    else
                                    {
                                        vis = reg.IsOpaque(bB) && !reg.IsOpaque(bA);
                                        cell.tile = vis ? (int)reg.FaceTile(bB, d * 2 + 1) : -1;
                                    }
                                }
                                else
                                {
                                    // Water surface: emit only where a transparent block faces AIR.
                                    if (dir == 1)
                                    {
                                        vis = reg.IsTransparent(bA) && bB == kAir;
                                        cell.tile = vis ? (int)reg.FaceTile(bA, d * 2) : -1;
                                    }
                                    else
                                    {
                                        vis = reg.IsTransparent(bB) && bA == kAir;
                                        cell.tile = vis ? (int)reg.FaceTile(bB, d * 2 + 1) : -1;
                                    }
                                }
                                if (!vis)
                                    continue;

                                if (transparentPass)
                                {
                                    // Water is unshaded — full-bright corners (also lets co-planar water
                                    // merge into one big quad since AO never differs).
                                    cell.ao[0] = cell.ao[1] = cell.ao[2] = cell.ao[3] = 3;
                                    continue;
                                }

                                // AO for the cell's 4 grid corners. Canonical (du,dv) corners
                                // (0,0),(1,0),(1,1),(0,1); each samples its two in-plane neighbours +
                                // the diagonal on the air side. Store in pos[]-emit order per dir.
                                int aoCanon[4];
                                const int duv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                                for (int k = 0; k < 4; k++)
                                {
                                    int ou = duv[k][0] ? +1 : -1;
                                    int ov = duv[k][1] ? +1 : -1;
                                    bool s1 = occ(airD, pu + ou, pv);
                                    bool s2 = occ(airD, pu, pv + ov);
                                    bool cr = occ(airD, pu + ou, pv + ov);
                                    aoCanon[k] = VertexAO(s1, s2, cr);
                                }
                                if (dir == 1)
                                {
                                    // pos = c0,c1,c2,c3
                                    cell.ao[0] = (uint8_t)aoCanon[0];
                                    cell.ao[1] = (uint8_t)aoCanon[1];
                                    cell.ao[2] = (uint8_t)aoCanon[2];
                                    cell.ao[3] = (uint8_t)aoCanon[3];
                                }
                                else
                                {
                                    // reversed winding: pos = c0,c3,c2,c1
                                    cell.ao[0] = (uint8_t)aoCanon[0];
                                    cell.ao[1] = (uint8_t)aoCanon[3];
                                    cell.ao[2] = (uint8_t)aoCanon[2];
                                    cell.ao[3] = (uint8_t)aoCanon[1];
                                }
                            }
                        }
                        // Greedy merge
                        for (int qu = 0; qu < kSectionDim; qu++)
                        {
                            for (int qv = 0; qv < kSectionDim;)
                            {
                                MaskCell seed = mask[qu * kSectionDim + qv];
                                if (seed.tile < 0)
                                {
                                    qv++;
                                    continue;
                                }
                                int w = 1;
                                while (qv + w < kSectionDim && CellEq(mask[qu * kSectionDim + qv + w], seed))
                                    w++;
                                int h = 1;
                                while (qu + h < kSectionDim)
                                {
                                    bool ok = true;
                                    for (int i = 0; i < w; i++)
                                        if (!CellEq(mask[(qu + h) * kSectionDim + qv + i], seed))
                                        {
                                            ok = false;
                                            break;
                                        }
                                    if (!ok)
                                        break;
                                    h++;
                                }
                                int tile = seed.tile;
                                float fd = (float)p, fqu = (float)qu, fqv = (float)qv, fh = (float)h, fw = (float)w;
                                float pos[4][3] = {};
                                if (dir == 1)
                                {
                                    // +d face CCW from outside: c0(qu,qv),c1(qu+h,qv),c2(qu+h,qv+w),c3(qu,qv+w)
                                    pos[0][d] = fd;
                                    pos[0][u] = fqu;
                                    pos[0][v] = fqv;
                                    pos[1][d] = fd;
                                    pos[1][u] = fqu + fh;
                                    pos[1][v] = fqv;
                                    pos[2][d] = fd;
                                    pos[2][u] = fqu + fh;
                                    pos[2][v] = fqv + fw;
                                    pos[3][d] = fd;
                                    pos[3][u] = fqu;
                                    pos[3][v] = fqv + fw;
                                }
                                else
                                {
                                    // -d face CCW from outside: reverse winding
                                    pos[0][d] = fd;
                                    pos[0][u] = fqu;
                                    pos[0][v] = fqv;
                                    pos[1][d] = fd;
                                    pos[1][u] = fqu;
                                    pos[1][v] = fqv + fw;
                                    pos[2][d] = fd;
                                    pos[2][u] = fqu + fh;
                                    pos[2][v] = fqv + fw;
                                    pos[3][d] = fd;
                                    pos[3][u] = fqu + fh;
                                    pos[3][v] = fqv;
                                }
                                float uvCoords[4][2];
                                if (dir == 1)
                                {
                                    uvCoords[0][0] = 0;
                                    uvCoords[0][1] = 0;
                                    uvCoords[1][0] = fh;
                                    uvCoords[1][1] = 0;
                                    uvCoords[2][0] = fh;
                                    uvCoords[2][1] = fw;
                                    uvCoords[3][0] = 0;
                                    uvCoords[3][1] = fw;
                                }
                                else
                                {
                                    uvCoords[0][0] = 0;
                                    uvCoords[0][1] = 0;
                                    uvCoords[1][0] = 0;
                                    uvCoords[1][1] = fw;
                                    uvCoords[2][0] = fh;
                                    uvCoords[2][1] = fw;
                                    uvCoords[3][0] = fh;
                                    uvCoords[3][1] = 0;
                                }
                                // Face id 0..5: axis d in 0..2, +dir even / -dir odd. The VS rebuilds the
                                // face normal + a perpendicular tangent from this; the packed vert encodes only the id.
                                const uint32_t normalId = (uint32_t)(d * 2 + (dir == 1 ? 0 : 1));
                                uint32_t base = (uint32_t)outVerts.size();
                                for (int ci = 0; ci < 4; ci++)
                                {
                                    // Section-local pos (0..16, lod 0 => stride 1); world pos = aabbMin + this.
                                    // All values are exact non-negative integers, so +0.5 cast just guards fp fuzz.
                                    const uint32_t px = (uint32_t)(pos[ci][0] * (float)stride + 0.5f);
                                    const uint32_t py = (uint32_t)(pos[ci][1] * (float)stride + 0.5f);
                                    const uint32_t pz = (uint32_t)(pos[ci][2] * (float)stride + 0.5f);
                                    const uint32_t ao = seed.ao[ci];
                                    const uint32_t uu = (uint32_t)(uvCoords[ci][0] + 0.5f);
                                    const uint32_t vv = (uint32_t)(uvCoords[ci][1] + 0.5f);
                                    if ((int)px < mn[0])
                                        mn[0] = (int)px;
                                    if ((int)px > mx[0])
                                        mx[0] = (int)px;
                                    if ((int)py < mn[1])
                                        mn[1] = (int)py;
                                    if ((int)py > mx[1])
                                        mx[1] = (int)py;
                                    if ((int)pz < mn[2])
                                        mn[2] = (int)pz;
                                    if ((int)pz > mx[2])
                                        mx[2] = (int)pz;
                                    VoxelVertex vt;
                                    vt.w0 = (px & 31u) | ((py & 31u) << 5) | ((pz & 31u) << 10) |
                                            ((normalId & 7u) << 15) | ((ao & 3u) << 18);
                                    vt.w1 = ((uint32_t)tile & 0xFFFFu) | ((uu & 0xFFu) << 16) | ((vv & 0xFFu) << 24);
                                    outVerts.push_back(vt);
                                }
                                // Anti-artifact: split the quad along the diagonal between the two
                                // brighter corners so the dark AO gradient doesn't interpolate across
                                // the bright pair (the classic flipped-quad fix). Both choices keep
                                // the CCW winding.
                                // uint16 indices: base (section-local vert count) stays < 65536 (a 16^3
                                // section maxes at ~49k verts even checkerboard, fewer after greedy merge).
                                auto idx = [&](uint32_t vtx)
                                { outIdx.push_back(static_cast<uint16_t>(vtx)); };
                                if (seed.ao[0] + seed.ao[2] > seed.ao[1] + seed.ao[3])
                                {
                                    idx(base + 0);
                                    idx(base + 1);
                                    idx(base + 3);
                                    idx(base + 1);
                                    idx(base + 2);
                                    idx(base + 3);
                                }
                                else
                                {
                                    idx(base + 0);
                                    idx(base + 1);
                                    idx(base + 2);
                                    idx(base + 0);
                                    idx(base + 2);
                                    idx(base + 3);
                                }
                                for (int du = 0; du < h; du++)
                                    for (int dv = 0; dv < w; dv++)
                                        mask[(qu + du) * kSectionDim + qv + dv].tile = -1;
                                qv += w;
                            }
                        }
                    }
                }
            }
            // Re-base packed positions to the tight local min so the AABB (set in GeometryArena from
            // localMin/localMax) hugs the actual surfaces instead of the full 16^3 cube. aabbMin
            // (= sectionOrigin + localMin) stays the VS origin: world = aabbMin + (pos - localMin) ==
            // sectionOrigin + pos, unchanged — but the tighter box lets Hi-Z occlusion cull sparse
            // (cave/overhang) sections whose loose-cube nearest corner used to sit in empty air.
            if (!outVerts.empty())
            {
                for (VoxelVertex &vt : outVerts)
                {
                    uint32_t px = (vt.w0 & 31u) - (uint32_t)mn[0];
                    uint32_t py = ((vt.w0 >> 5) & 31u) - (uint32_t)mn[1];
                    uint32_t pz = ((vt.w0 >> 10) & 31u) - (uint32_t)mn[2];
                    vt.w0 = (vt.w0 & ~0x7FFFu) | (px & 31u) | ((py & 31u) << 5) | ((pz & 31u) << 10);
                }
            }
        }
    } // namespace

    MeshData GreedyMesher::Mesh(BlockSampleFn sample, void *sampleCtx, const BlockRegistry &reg, int lod)
    {
        MeshData result;

        int mn[3] = {255, 255, 255};
        int mx[3] = {0, 0, 0};
        GreedySweep(sample, sampleCtx, reg, lod, false, result.vertices, result.indices, mn, mx);
        if (!result.vertices.empty())
            for (int k = 0; k < 3; ++k)
            {
                result.localMin[k] = (uint32_t)mn[k];
                result.localMax[k] = (uint32_t)mx[k];
            }

        // Second pass only when the section actually has a transparent (water) block — the cheap scan
        // keeps the common water-free section single-sweep. kSectionDim^3 sample() calls << a full sweep.
        bool hasTransparent = false;
        for (int z = 0; z < kSectionDim && !hasTransparent; ++z)
            for (int y = 0; y < kSectionDim && !hasTransparent; ++y)
                for (int x = 0; x < kSectionDim; ++x)
                    if (reg.IsTransparent(sample(sampleCtx, x, y, z)))
                    {
                        hasTransparent = true;
                        break;
                    }
        if (hasTransparent)
        {
            int tmn[3] = {255, 255, 255};
            int tmx[3] = {0, 0, 0};
            GreedySweep(sample, sampleCtx, reg, lod, true, result.transparentVertices,
                        result.transparentIndices, tmn, tmx);
            if (!result.transparentVertices.empty())
                for (int k = 0; k < 3; ++k)
                {
                    result.transparentLocalMin[k] = (uint32_t)tmn[k];
                    result.transparentLocalMax[k] = (uint32_t)tmx[k];
                }
        }
        return result;
    }
} // namespace pe::voxel
