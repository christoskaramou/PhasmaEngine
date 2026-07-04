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

    SmoothMeshData SurfaceNetsMesh(const std::vector<float> &field, const vec3 &originW,
                                   int nx, int ny, int nz)
    {
        SmoothMeshData out;
        if (nx < 1 || ny < 1 || nz < 1)
            return out;

        const int cnx = nx + 1, cny = ny + 1, cnz = nz + 1;
        if (field.size() < static_cast<size_t>(cnx) * cny * cnz)
            return out;
        auto CIdx = [cnx, cny](int i, int j, int k)
        { return i + cnx * (j + cny * k); };
        // Field read with edge clamp, so central differences stay valid on the boundary.
        auto F = [&](int i, int j, int k)
        {
            i = i < 0 ? 0 : (i >= cnx ? cnx - 1 : i);
            j = j < 0 ? 0 : (j >= cny ? cny - 1 : j);
            k = k < 0 ? 0 : (k >= cnz ? cnz - 1 : k);
            return field[CIdx(i, j, k)];
        };

        // One vertex per surface cell; -1 = the cell has no surface.
        std::vector<int> cellVert(static_cast<size_t>(nx) * ny * nz, -1);
        auto VIdx = [nx, ny](int i, int j, int k)
        { return i + nx * (j + ny * k); };
        const int stride[3] = {1, nx, nx * ny};

        vec3 bbMin(1e30f), bbMax(-1e30f);

        // Scan i fastest, then j, then k, so a cell's negative-side neighbors are always meshed first.
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    float cd[8];
                    int mask = 0;
                    for (int c = 0; c < 8; ++c)
                    {
                        cd[c] = field[CIdx(i + kCorner[c][0], j + kCorner[c][1], k + kCorner[c][2])];
                        if (cd[c] >= 0.0f)
                            mask |= (1 << c); // solid corner
                    }
                    if (mask == 0 || mask == 0xFF)
                        continue; // wholly air or wholly solid — no surface here

                    // Vertex = mean of the zero-crossings on the cell's edges.
                    vec3 sum(0.0f);
                    int n = 0;
                    for (int e = 0; e < 12; ++e)
                    {
                        const int a = kEdge[e][0], b = kEdge[e][1];
                        if (((mask >> a) & 1) == ((mask >> b) & 1))
                            continue;
                        const float da = cd[a], db = cd[b];
                        const float t = da / (da - db); // crossing where density hits 0
                        const vec3 pa(i + kCorner[a][0], j + kCorner[a][1], k + kCorner[a][2]);
                        const vec3 pb(i + kCorner[b][0], j + kCorner[b][1], k + kCorner[b][2]);
                        sum += pa + (pb - pa) * t;
                        ++n;
                    }
                    const vec3 world = originW + sum / static_cast<float>(n);

                    // Normal = -gradient of the field via central differences (density rises into solid,
                    // so the outward normal is -grad). Reads the edited field, so sculpted craters carry
                    // correct normals with no generator re-query.
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
                    // to the three cells on the negative side of the other two axes.
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
