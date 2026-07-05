#include "Voxel/SurfaceNets.h"

namespace pe::voxel
{
    namespace
    {
        // Cell corner offsets, indexed bit0=x, bit1=y, bit2=z (corner 0 = the cell's minimum corner).
        constexpr int kCorner[8][3] = {
            {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
        // The 12 cell edges as (cornerA, cornerB).
        constexpr int kEdge[12][2] = {
            {0, 1}, {2, 3}, {4, 5}, {6, 7}, // along x
            {0, 2},
            {1, 3},
            {4, 6},
            {5, 7}, // along y
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7} // along z
        };
        // Corner one step along +x / +y / +z from corner 0 — the three edges that gate quad emission.
        constexpr int kAxisCorner[3] = {1, 2, 4};
    } // namespace

    SmoothMeshData SurfaceNetsTile(const std::vector<float> &field, const ivec3 &gridMin,
                                   const ivec3 &cells, const ivec3 &apron, float cellSize)
    {
        SmoothMeshData out;
        if (cells.x < 1 || cells.y < 1 || cells.z < 1 || cellSize <= 0.0f)
            return out;

        // Corner lattice with the one-corner guard ring: field (i,j,k) = global corner
        // gridMin + (i,j,k) - 1. Cell c reads corners c..c+1 -> field c+1..c+2; central differences
        // reach field c..c+3 — all in range.
        const int fnx = cells.x + 3, fny = cells.y + 3, fnz = cells.z + 3;
        if (field.size() < static_cast<size_t>(fnx) * fny * fnz)
            return out;
        auto FIdx = [fnx, fny](int i, int j, int k)
        { return i + fnx * (j + fny * k); };

        // One vertex per surface cell; -1 = the cell has no surface.
        std::vector<int> cellVert(static_cast<size_t>(cells.x) * cells.y * cells.z, -1);
        auto VIdx = [&](int i, int j, int k)
        { return i + cells.x * (j + cells.y * k); };
        const int stride[3] = {1, cells.x, cells.x * cells.y};

        vec3 bbMin(1e30f), bbMax(-1e30f);

        // Scan i fastest, then j, then k, so a cell's negative-side neighbors are always meshed first.
        for (int k = 0; k < cells.z; ++k)
            for (int j = 0; j < cells.y; ++j)
                for (int i = 0; i < cells.x; ++i)
                {
                    float cd[8];
                    int mask = 0;
                    for (int c = 0; c < 8; ++c)
                    {
                        cd[c] = field[FIdx(i + 1 + kCorner[c][0], j + 1 + kCorner[c][1], k + 1 + kCorner[c][2])];
                        if (cd[c] >= 0.0f)
                            mask |= (1 << c); // solid corner
                    }
                    if (mask == 0 || mask == 0xFF)
                        continue; // wholly air or wholly solid — no surface here

                    // Vertex = mean of the zero-crossings on the cell's edges, in GLOBAL lattice space
                    // (float(globalCorner) * cellSize), so a neighbouring tile duplicating this apron
                    // cell lands on the bit-identical position.
                    vec3 sum(0.0f);
                    int n = 0;
                    for (int e = 0; e < 12; ++e)
                    {
                        const int a = kEdge[e][0], b = kEdge[e][1];
                        if (((mask >> a) & 1) == ((mask >> b) & 1))
                            continue;
                        const float da = cd[a], db = cd[b];
                        const float t = da / (da - db); // crossing where density hits 0
                        const vec3 pa(static_cast<float>(gridMin.x + i + kCorner[a][0]),
                                      static_cast<float>(gridMin.y + j + kCorner[a][1]),
                                      static_cast<float>(gridMin.z + k + kCorner[a][2]));
                        const vec3 pb(static_cast<float>(gridMin.x + i + kCorner[b][0]),
                                      static_cast<float>(gridMin.y + j + kCorner[b][1]),
                                      static_cast<float>(gridMin.z + k + kCorner[b][2]));
                        sum += pa + (pb - pa) * t;
                        ++n;
                    }
                    const vec3 world = (sum / static_cast<float>(n)) * cellSize;

                    // Normal = -gradient of the field via central differences (density rises into solid,
                    // so the outward normal is -grad). The guard ring keeps the stencil full even for
                    // apron cells, so duplicated seam vertices shade identically in both tiles.
                    auto F = [&](int ci, int cj, int ck)
                    { return field[FIdx(ci + 1, cj + 1, ck + 1)]; };
                    vec3 nrm(-(F(i + 1, j, k) - F(i - 1, j, k)), -(F(i, j + 1, k) - F(i, j - 1, k)),
                             -(F(i, j, k + 1) - F(i, j, k - 1)));
                    const float len = glm::length(nrm);
                    nrm = len > 1e-6f ? nrm / len : vec3(0.0f, 1.0f, 0.0f);

                    Vertex v{};
                    v.position[0] = world.x;
                    v.position[1] = world.y;
                    v.position[2] = world.z;
                    v.normals[0] = nrm.x;
                    v.normals[1] = nrm.y;
                    v.normals[2] = nrm.z;
                    v.uv[0] = world.x; // planar UV; a smooth surface has no natural UV (triplanar later)
                    v.uv[1] = world.z;
                    v.tangent[0] = 1.0f;
                    v.tangent[3] = 1.0f;
                    v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;

                    const int m = VIdx(i, j, k);
                    cellVert[m] = static_cast<int>(out.vertices.size());
                    out.vertices.push_back(v);
                    bbMin = glm::min(bbMin, world);
                    bbMax = glm::max(bbMax, world);

                    // Dual quads: for each of the 3 min-corner edges that cross, join this cell's vertex
                    // to the three cells on the negative side of the other two axes. Apron cells emit
                    // nothing — the edge belongs to the neighbouring tile, which emits the quad with its
                    // own duplicates of these vertices.
                    if (i < apron.x || j < apron.y || k < apron.z)
                        continue;
                    const bool s0 = (mask & 1) != 0;
                    const int cellX[3] = {i, j, k};
                    for (int ax = 0; ax < 3; ++ax)
                    {
                        if (s0 == (((mask >> kAxisCorner[ax]) & 1) != 0))
                            continue; // no sign change along this axis's corner-0 edge → no quad
                        const int iu = (ax + 1) % 3, iv = (ax + 2) % 3;
                        if (cellX[iu] == 0 || cellX[iv] == 0)
                            continue; // negative-side neighbors would be off the grid
                        const int du = stride[iu], dv = stride[iv];
                        const int v1 = cellVert[m - du];
                        const int v2 = cellVert[m - du - dv];
                        const int v3 = cellVert[m - dv];
                        if (v1 < 0 || v2 < 0 || v3 < 0)
                            continue;
                        const int v0 = cellVert[m];
                        if (s0) // corner 0 solid → wind so the front face points toward the air side
                        {
                            out.indices.push_back(v0);
                            out.indices.push_back(v1);
                            out.indices.push_back(v2);
                            out.indices.push_back(v0);
                            out.indices.push_back(v2);
                            out.indices.push_back(v3);
                        }
                        else
                        {
                            out.indices.push_back(v0);
                            out.indices.push_back(v3);
                            out.indices.push_back(v2);
                            out.indices.push_back(v0);
                            out.indices.push_back(v2);
                            out.indices.push_back(v1);
                        }
                    }
                }

        if (!out.vertices.empty())
        {
            out.aabbMin = bbMin;
            out.aabbMax = bbMax;
        }
        return out;
    }
} // namespace pe::voxel
