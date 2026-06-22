#include "Voxel/GreedyMesher.h"

namespace pe::voxel
{
    MeshData GreedyMesher::Mesh(const BlockSampler &sample, const BlockRegistry &reg, int lod)
    {
        int stride = 1 << lod;
        MeshData result;
        int32_t mask[kSectionDim * kSectionDim];
        for (int d = 0; d < 3; d++)
        {
            int u = (d + 1) % 3, v = (d + 2) % 3;
            for (int dir = 1; dir >= -1; dir -= 2)
            {
                for (int p = 0; p <= kSectionDim; p++)
                {
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
                            BlockId bA = sample(cA[0], cA[1], cA[2]);
                            BlockId bB = sample(cB[0], cB[1], cB[2]);
                            if (dir == 1)
                            {
                                bool vis = reg.IsOpaque(bA) && !reg.IsOpaque(bB);
                                mask[pu * kSectionDim + pv] = vis ? (int)reg.FaceTile(bA, d * 2) : -1;
                            }
                            else
                            {
                                bool vis = reg.IsOpaque(bB) && !reg.IsOpaque(bA);
                                mask[pu * kSectionDim + pv] = vis ? (int)reg.FaceTile(bB, d * 2 + 1) : -1;
                            }
                        }
                    }
                    // Greedy merge
                    for (int qu = 0; qu < kSectionDim; qu++)
                    {
                        for (int qv = 0; qv < kSectionDim;)
                        {
                            int tile = mask[qu * kSectionDim + qv];
                            if (tile < 0)
                            {
                                qv++;
                                continue;
                            }
                            int w = 1;
                            while (qv + w < kSectionDim && mask[qu * kSectionDim + qv + w] == tile)
                                w++;
                            int h = 1;
                            while (qu + h < kSectionDim)
                            {
                                bool ok = true;
                                for (int i = 0; i < w; i++)
                                    if (mask[(qu + h) * kSectionDim + qv + i] != tile)
                                    {
                                        ok = false;
                                        break;
                                    }
                                if (!ok)
                                    break;
                                h++;
                            }
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
                            float norm[3] = {};
                            norm[d] = (float)dir;
                            float tang[3] = {};
                            tang[u] = 1.0f;
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
                            uint32_t base = (uint32_t)result.vertices.size();
                            for (int ci = 0; ci < 4; ci++)
                            {
                                Vertex vt = {};
                                vt.position[0] = pos[ci][0] * (float)stride;
                                vt.position[1] = pos[ci][1] * (float)stride;
                                vt.position[2] = pos[ci][2] * (float)stride;
                                vt.normals[0] = norm[0];
                                vt.normals[1] = norm[1];
                                vt.normals[2] = norm[2];
                                vt.tangent[0] = tang[0];
                                vt.tangent[1] = tang[1];
                                vt.tangent[2] = tang[2];
                                vt.tangent[3] = 1.0f;
                                vt.color[0] = 1.0f;
                                vt.color[1] = 1.0f;
                                vt.color[2] = 1.0f;
                                vt.color[3] = 1.0f;
                                vt.joints[0] = (uint32_t)tile;
                                vt.joints[1] = 0;
                                vt.joints[2] = 0;
                                vt.joints[3] = 0;
                                vt.uv[0] = uvCoords[ci][0];
                                vt.uv[1] = uvCoords[ci][1];
                                result.vertices.push_back(vt);
                            }
                            result.indices.push_back(base + 0);
                            result.indices.push_back(base + 1);
                            result.indices.push_back(base + 2);
                            result.indices.push_back(base + 0);
                            result.indices.push_back(base + 2);
                            result.indices.push_back(base + 3);
                            for (int du = 0; du < h; du++)
                                for (int dv = 0; dv < w; dv++)
                                    mask[(qu + du) * kSectionDim + qv + dv] = -1;
                            qv += w;
                        }
                    }
                }
            }
        }
        return result;
    }
} // namespace pe::voxel
