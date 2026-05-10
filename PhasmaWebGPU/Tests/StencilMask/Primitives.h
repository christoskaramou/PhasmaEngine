#pragma once

#include <cmath>

namespace pwgpu::test::primitives
{
    // Each vertex is 8 floats: pos3 + normal3 + uv2. Stride = 32 bytes.
    constexpr uint32_t kVertexSize = 8;

    struct VertexData
    {
        std::vector<float> vertices;
        std::vector<uint16_t> indices;
    };

    // ------------------------------------------------------------------
    // Plane: XZ plane centered at origin. Default 1x1, y=0, normal=(0,1,0).
    // ------------------------------------------------------------------
    inline VertexData CreatePlaneVertices(float width = 1.0f,
                                          float depth = 1.0f,
                                          uint32_t subdivisionsWidth = 1,
                                          uint32_t subdivisionsDepth = 1)
    {
        VertexData out;
        const uint32_t numVerts = (subdivisionsWidth + 1) * (subdivisionsDepth + 1);
        out.vertices.resize(numVerts * kVertexSize);

        uint32_t cursor = 0;
        for (uint32_t z = 0; z <= subdivisionsDepth; ++z)
        {
            for (uint32_t x = 0; x <= subdivisionsWidth; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(subdivisionsWidth);
                const float v = static_cast<float>(z) / static_cast<float>(subdivisionsDepth);
                out.vertices[cursor + 0] = width * u - width * 0.5f;
                out.vertices[cursor + 1] = 0.0f;
                out.vertices[cursor + 2] = depth * v - depth * 0.5f;
                out.vertices[cursor + 3] = 0.0f;
                out.vertices[cursor + 4] = 1.0f;
                out.vertices[cursor + 5] = 0.0f;
                out.vertices[cursor + 6] = u;
                out.vertices[cursor + 7] = v;
                cursor += kVertexSize;
            }
        }

        const uint32_t numVertsAcross = subdivisionsWidth + 1;
        out.indices.resize(3 * subdivisionsWidth * subdivisionsDepth * 2);
        cursor = 0;
        for (uint32_t z = 0; z < subdivisionsDepth; ++z)
        {
            for (uint32_t x = 0; x < subdivisionsWidth; ++x)
            {
                out.indices[cursor++] = static_cast<uint16_t>((z + 0) * numVertsAcross + x);
                out.indices[cursor++] = static_cast<uint16_t>((z + 1) * numVertsAcross + x);
                out.indices[cursor++] = static_cast<uint16_t>((z + 0) * numVertsAcross + x + 1);
                out.indices[cursor++] = static_cast<uint16_t>((z + 1) * numVertsAcross + x);
                out.indices[cursor++] = static_cast<uint16_t>((z + 1) * numVertsAcross + x + 1);
                out.indices[cursor++] = static_cast<uint16_t>((z + 0) * numVertsAcross + x + 1);
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Sphere: parametric.
    // ------------------------------------------------------------------
    inline VertexData CreateSphereVertices(float radius = 1.0f,
                                           uint32_t subdivisionsAxis = 24,
                                           uint32_t subdivisionsHeight = 12,
                                           float startLatitudeInRadians = 0.0f,
                                           float endLatitudeInRadians = 3.14159265358979323846f,
                                           float startLongitudeInRadians = 0.0f,
                                           float endLongitudeInRadians = 2.0f * 3.14159265358979323846f)
    {
        VertexData out;
        const float latRange = endLatitudeInRadians - startLatitudeInRadians;
        const float longRange = endLongitudeInRadians - startLongitudeInRadians;

        const uint32_t numVerts = (subdivisionsAxis + 1) * (subdivisionsHeight + 1);
        out.vertices.resize(numVerts * kVertexSize);

        uint32_t cursor = 0;
        for (uint32_t y = 0; y <= subdivisionsHeight; ++y)
        {
            for (uint32_t x = 0; x <= subdivisionsAxis; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(subdivisionsAxis);
                const float v = static_cast<float>(y) / static_cast<float>(subdivisionsHeight);
                const float theta = longRange * u + startLongitudeInRadians;
                const float phi = latRange * v + startLatitudeInRadians;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);
                const float ux = cosTheta * sinPhi;
                const float uy = cosPhi;
                const float uz = sinTheta * sinPhi;
                out.vertices[cursor + 0] = radius * ux;
                out.vertices[cursor + 1] = radius * uy;
                out.vertices[cursor + 2] = radius * uz;
                out.vertices[cursor + 3] = ux;
                out.vertices[cursor + 4] = uy;
                out.vertices[cursor + 5] = uz;
                out.vertices[cursor + 6] = 1.0f - u;
                out.vertices[cursor + 7] = v;
                cursor += kVertexSize;
            }
        }

        const uint32_t numVertsAround = subdivisionsAxis + 1;
        out.indices.resize(3 * subdivisionsAxis * subdivisionsHeight * 2);
        cursor = 0;
        for (uint32_t x = 0; x < subdivisionsAxis; ++x)
        {
            for (uint32_t y = 0; y < subdivisionsHeight; ++y)
            {
                out.indices[cursor++] = static_cast<uint16_t>((y + 0) * numVertsAround + x);
                out.indices[cursor++] = static_cast<uint16_t>((y + 0) * numVertsAround + x + 1);
                out.indices[cursor++] = static_cast<uint16_t>((y + 1) * numVertsAround + x);
                out.indices[cursor++] = static_cast<uint16_t>((y + 1) * numVertsAround + x);
                out.indices[cursor++] = static_cast<uint16_t>((y + 0) * numVertsAround + x + 1);
                out.indices[cursor++] = static_cast<uint16_t>((y + 1) * numVertsAround + x + 1);
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Cube: 24 vertices (each face has own normals/uvs), 36 indices.
    // ------------------------------------------------------------------
    inline VertexData CreateCubeVertices(float size = 1.0f)
    {
        static const int kFaceIndices[6][4] = {
            {3, 7, 5, 1},
            {6, 2, 0, 4},
            {6, 7, 3, 2},
            {0, 1, 5, 4},
            {7, 6, 4, 5},
            {2, 3, 1, 0},
        };

        const float k = size * 0.5f;
        const float corner[8][3] = {{-k, -k, -k}, {+k, -k, -k}, {-k, +k, -k}, {+k, +k, -k}, {-k, -k, +k}, {+k, -k, +k}, {-k, +k, +k}, {+k, +k, +k}};
        const float faceNormal[6][3] = {
            {+1, 0, 0}, {-1, 0, 0}, {0, +1, 0}, {0, -1, 0}, {0, 0, +1}, {0, 0, -1}};
        const float uvCoords[4][2] = {{1, 0}, {0, 0}, {0, 1}, {1, 1}};

        VertexData out;
        out.vertices.resize(6 * 4 * kVertexSize);
        out.indices.resize(3 * 6 * 2);

        uint32_t vCursor = 0;
        uint32_t iCursor = 0;
        for (int f = 0; f < 6; ++f)
        {
            for (int v = 0; v < 4; ++v)
            {
                const float *p = corner[kFaceIndices[f][v]];
                const float *n = faceNormal[f];
                const float *uv = uvCoords[v];
                out.vertices[vCursor + 0] = p[0];
                out.vertices[vCursor + 1] = p[1];
                out.vertices[vCursor + 2] = p[2];
                out.vertices[vCursor + 3] = n[0];
                out.vertices[vCursor + 4] = n[1];
                out.vertices[vCursor + 5] = n[2];
                out.vertices[vCursor + 6] = uv[0];
                out.vertices[vCursor + 7] = uv[1];
                vCursor += kVertexSize;
            }
            const uint16_t offset = static_cast<uint16_t>(4 * f);
            out.indices[iCursor + 0] = offset + 0;
            out.indices[iCursor + 1] = offset + 1;
            out.indices[iCursor + 2] = offset + 2;
            out.indices[iCursor + 3] = offset + 0;
            out.indices[iCursor + 4] = offset + 2;
            out.indices[iCursor + 5] = offset + 3;
            iCursor += 6;
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Truncated cone (also cylinder/cone). y-axis vertical, centered at origin.
    // ------------------------------------------------------------------
    inline VertexData CreateTruncatedConeVertices(float bottomRadius = 1.0f,
                                                  float topRadius = 0.0f,
                                                  float height = 1.0f,
                                                  uint32_t radialSubdivisions = 24,
                                                  uint32_t verticalSubdivisions = 1,
                                                  bool topCap = true,
                                                  bool bottomCap = true)
    {
        const uint32_t extra = (topCap ? 2u : 0u) + (bottomCap ? 2u : 0u);
        const uint32_t numVerts = (radialSubdivisions + 1) * (verticalSubdivisions + 1 + extra);
        VertexData out;
        out.vertices.resize(numVerts * kVertexSize);
        out.indices.resize(3 * radialSubdivisions * (verticalSubdivisions + extra / 2) * 2);

        const uint32_t vertsAroundEdge = radialSubdivisions + 1;

        const float slant = std::atan2(bottomRadius - topRadius, height);
        const float cosSlant = std::cos(slant);
        const float sinSlant = std::sin(slant);

        const int start = topCap ? -2 : 0;
        const int end = static_cast<int>(verticalSubdivisions) + (bottomCap ? 2 : 0);

        uint32_t cursor = 0;
        for (int yy = start; yy <= end; ++yy)
        {
            float v = static_cast<float>(yy) / static_cast<float>(verticalSubdivisions);
            float y = height * v;
            float ringRadius = 0.0f;
            if (yy < 0)
            {
                y = 0.0f;
                v = 1.0f;
                ringRadius = bottomRadius;
            }
            else if (yy > static_cast<int>(verticalSubdivisions))
            {
                y = height;
                v = 1.0f;
                ringRadius = topRadius;
            }
            else
            {
                ringRadius = bottomRadius + (topRadius - bottomRadius) *
                                                (static_cast<float>(yy) /
                                                 static_cast<float>(verticalSubdivisions));
            }
            if (yy == -2 || yy == static_cast<int>(verticalSubdivisions) + 2)
            {
                ringRadius = 0.0f;
                v = 0.0f;
            }
            y -= height * 0.5f;
            for (uint32_t ii = 0; ii < vertsAroundEdge; ++ii)
            {
                const float angle = (static_cast<float>(ii) * 2.0f * 3.14159265358979323846f) /
                                    static_cast<float>(radialSubdivisions);
                const float s = std::sin(angle);
                const float c = std::cos(angle);
                out.vertices[cursor + 0] = s * ringRadius;
                out.vertices[cursor + 1] = y;
                out.vertices[cursor + 2] = c * ringRadius;
                if (yy < 0)
                {
                    out.vertices[cursor + 3] = 0.0f;
                    out.vertices[cursor + 4] = -1.0f;
                    out.vertices[cursor + 5] = 0.0f;
                }
                else if (yy > static_cast<int>(verticalSubdivisions))
                {
                    out.vertices[cursor + 3] = 0.0f;
                    out.vertices[cursor + 4] = 1.0f;
                    out.vertices[cursor + 5] = 0.0f;
                }
                else if (ringRadius == 0.0f)
                {
                    out.vertices[cursor + 3] = 0.0f;
                    out.vertices[cursor + 4] = 0.0f;
                    out.vertices[cursor + 5] = 0.0f;
                }
                else
                {
                    out.vertices[cursor + 3] = s * cosSlant;
                    out.vertices[cursor + 4] = sinSlant;
                    out.vertices[cursor + 5] = c * cosSlant;
                }
                out.vertices[cursor + 6] =
                    static_cast<float>(ii) / static_cast<float>(radialSubdivisions);
                out.vertices[cursor + 7] = 1.0f - v;
                cursor += kVertexSize;
            }
        }

        cursor = 0;
        for (uint32_t yy = 0; yy < verticalSubdivisions + extra; ++yy)
        {
            if ((yy == 1 && topCap) ||
                (yy == verticalSubdivisions + extra - 2 && bottomCap))
            {
                continue;
            }
            for (uint32_t ii = 0; ii < radialSubdivisions; ++ii)
            {
                out.indices[cursor++] =
                    static_cast<uint16_t>(vertsAroundEdge * (yy + 0) + 0 + ii);
                out.indices[cursor++] =
                    static_cast<uint16_t>(vertsAroundEdge * (yy + 0) + 1 + ii);
                out.indices[cursor++] =
                    static_cast<uint16_t>(vertsAroundEdge * (yy + 1) + 1 + ii);
                out.indices[cursor++] =
                    static_cast<uint16_t>(vertsAroundEdge * (yy + 0) + 0 + ii);
                out.indices[cursor++] =
                    static_cast<uint16_t>(vertsAroundEdge * (yy + 1) + 1 + ii);
                out.indices[cursor++] =
                    static_cast<uint16_t>(vertsAroundEdge * (yy + 1) + 0 + ii);
            }
        }
        if (cursor < out.indices.size())
            out.indices.resize(cursor);
        return out;
    }

    inline VertexData CreateCylinderVertices(float radius = 1.0f,
                                             float height = 1.0f,
                                             uint32_t radialSubdivisions = 24,
                                             uint32_t verticalSubdivisions = 1,
                                             bool topCap = true,
                                             bool bottomCap = true)
    {
        return CreateTruncatedConeVertices(radius,
                                           radius,
                                           height,
                                           radialSubdivisions,
                                           verticalSubdivisions,
                                           topCap,
                                           bottomCap);
    }

    // ------------------------------------------------------------------
    // Torus: parametric ring.
    // ------------------------------------------------------------------
    inline VertexData CreateTorusVertices(float radius = 1.0f,
                                          float thickness = 0.24f,
                                          uint32_t radialSubdivisions = 24,
                                          uint32_t bodySubdivisions = 12,
                                          float startAngle = 0.0f,
                                          float endAngle = 2.0f * 3.14159265358979323846f)
    {
        const float range = endAngle - startAngle;

        const uint32_t radialParts = radialSubdivisions + 1;
        const uint32_t bodyParts = bodySubdivisions + 1;
        const uint32_t numVerts = radialParts * bodyParts;

        VertexData out;
        out.vertices.resize(numVerts * kVertexSize);
        out.indices.resize(3 * radialSubdivisions * bodySubdivisions * 2);

        uint32_t cursor = 0;
        for (uint32_t slice = 0; slice < bodyParts; ++slice)
        {
            const float v = static_cast<float>(slice) / static_cast<float>(bodySubdivisions);
            const float sliceAngle = v * 2.0f * 3.14159265358979323846f;
            const float sliceSin = std::sin(sliceAngle);
            const float ringRadius = radius + sliceSin * thickness;
            const float ny = std::cos(sliceAngle);
            const float y = ny * thickness;
            for (uint32_t ring = 0; ring < radialParts; ++ring)
            {
                const float u = static_cast<float>(ring) / static_cast<float>(radialSubdivisions);
                const float ringAngle = startAngle + u * range;
                const float xSin = std::sin(ringAngle);
                const float zCos = std::cos(ringAngle);
                const float x = xSin * ringRadius;
                const float z = zCos * ringRadius;
                const float nx = xSin * sliceSin;
                const float nz = zCos * sliceSin;
                out.vertices[cursor + 0] = x;
                out.vertices[cursor + 1] = y;
                out.vertices[cursor + 2] = z;
                out.vertices[cursor + 3] = nx;
                out.vertices[cursor + 4] = ny;
                out.vertices[cursor + 5] = nz;
                out.vertices[cursor + 6] = u;
                out.vertices[cursor + 7] = 1.0f - v;
                cursor += kVertexSize;
            }
        }

        cursor = 0;
        for (uint32_t slice = 0; slice < bodySubdivisions; ++slice)
        {
            for (uint32_t ring = 0; ring < radialSubdivisions; ++ring)
            {
                const uint32_t nextRingIndex = 1 + ring;
                const uint32_t nextSliceIndex = 1 + slice;
                out.indices[cursor++] = static_cast<uint16_t>(radialParts * slice + ring);
                out.indices[cursor++] =
                    static_cast<uint16_t>(radialParts * nextSliceIndex + ring);
                out.indices[cursor++] = static_cast<uint16_t>(radialParts * slice + nextRingIndex);

                out.indices[cursor++] =
                    static_cast<uint16_t>(radialParts * nextSliceIndex + ring);
                out.indices[cursor++] =
                    static_cast<uint16_t>(radialParts * nextSliceIndex + nextRingIndex);
                out.indices[cursor++] = static_cast<uint16_t>(radialParts * slice + nextRingIndex);
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Deindex: unroll indexed data into a non-shared vertex stream.
    // ------------------------------------------------------------------
    inline VertexData Deindex(const VertexData &src)
    {
        const uint32_t numElements = static_cast<uint32_t>(src.indices.size());
        VertexData out;
        out.vertices.resize(numElements * kVertexSize);
        out.indices.resize(numElements);
        for (uint32_t i = 0; i < numElements; ++i)
        {
            const uint32_t off = static_cast<uint32_t>(src.indices[i]) * kVertexSize;
            for (uint32_t k = 0; k < kVertexSize; ++k)
                out.vertices[i * kVertexSize + k] = src.vertices[off + k];
            out.indices[i] = static_cast<uint16_t>(i);
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Generate flat triangle normals for a deindexed vertex stream.
    // Matches webgpu-samples formula: n = cross(normalize(p0-p1), normalize(p0-p2)).
    // ------------------------------------------------------------------
    inline void GenerateTriangleNormalsInPlace(VertexData &data)
    {
        const uint32_t triStride = 3u * kVertexSize;
        for (uint32_t ii = 0; ii + triStride <= data.vertices.size(); ii += triStride)
        {
            const float *p0 = &data.vertices[ii + kVertexSize * 0];
            const float *p1 = &data.vertices[ii + kVertexSize * 1];
            const float *p2 = &data.vertices[ii + kVertexSize * 2];

            float n0x = p0[0] - p1[0];
            float n0y = p0[1] - p1[1];
            float n0z = p0[2] - p1[2];
            float n1x = p0[0] - p2[0];
            float n1y = p0[1] - p2[1];
            float n1z = p0[2] - p2[2];

            const float l0 =
                std::sqrt(n0x * n0x + n0y * n0y + n0z * n0z);
            if (l0 > 0.0f)
            {
                n0x /= l0;
                n0y /= l0;
                n0z /= l0;
            }
            const float l1 =
                std::sqrt(n1x * n1x + n1y * n1y + n1z * n1z);
            if (l1 > 0.0f)
            {
                n1x /= l1;
                n1y /= l1;
                n1z /= l1;
            }
            const float nx = n0y * n1z - n0z * n1y;
            const float ny = n0z * n1x - n0x * n1z;
            const float nz = n0x * n1y - n0y * n1x;

            for (uint32_t k = 0; k < 3; ++k)
            {
                data.vertices[ii + kVertexSize * k + 3] = nx;
                data.vertices[ii + kVertexSize * k + 4] = ny;
                data.vertices[ii + kVertexSize * k + 5] = nz;
            }
        }
    }

    inline VertexData Facet(const VertexData &src)
    {
        VertexData d = Deindex(src);
        GenerateTriangleNormalsInPlace(d);
        return d;
    }

    // ------------------------------------------------------------------
    // Reorient: transform positions by m, normals by upper3x3 of m.
    // Matrix is column-major 4x4 (m[col*4+row]), matching wgpu-matrix layout.
    // ------------------------------------------------------------------
    inline void ReorientInPlace(VertexData &data, const float m[16])
    {
        for (uint32_t i = 0; i < data.vertices.size(); i += kVertexSize)
        {
            const float px = data.vertices[i + 0];
            const float py = data.vertices[i + 1];
            const float pz = data.vertices[i + 2];
            // transformMat4: p' = M * [p, 1]. Column-major: M[col*4+row].
            const float npx = m[0] * px + m[4] * py + m[8] * pz + m[12];
            const float npy = m[1] * px + m[5] * py + m[9] * pz + m[13];
            const float npz = m[2] * px + m[6] * py + m[10] * pz + m[14];
            data.vertices[i + 0] = npx;
            data.vertices[i + 1] = npy;
            data.vertices[i + 2] = npz;

            const float nx = data.vertices[i + 3];
            const float ny = data.vertices[i + 4];
            const float nz = data.vertices[i + 5];
            // transformMat4Upper3x3: n' = upper3x3(M) * n (no translation).
            const float nnx = m[0] * nx + m[4] * ny + m[8] * nz;
            const float nny = m[1] * nx + m[5] * ny + m[9] * nz;
            const float nnz = m[2] * nx + m[6] * ny + m[10] * nz;
            data.vertices[i + 3] = nnx;
            data.vertices[i + 4] = nny;
            data.vertices[i + 5] = nnz;
        }
    }

    inline void MakeTranslation(float m[16], float x, float y, float z)
    {
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 0;
        m[4] = 0;
        m[5] = 1;
        m[6] = 0;
        m[7] = 0;
        m[8] = 0;
        m[9] = 0;
        m[10] = 1;
        m[11] = 0;
        m[12] = x;
        m[13] = y;
        m[14] = z;
        m[15] = 1;
    }
} // namespace pwgpu::test::primitives
