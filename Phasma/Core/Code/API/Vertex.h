#pragma once

namespace pe
{
    // gbuffer vertex structure
    struct Vertex
    {
        float position[3];
        float uv[2];
        float normals[3];
        float tangent[4];
        float color[4];
        uint32_t joints[4];
        float weights[4];
    };

    // aabb vertex structure
    struct AabbVertex
    {
        float position[3];
    };

    // position and uv vertex structure for depth and shadows
    struct PositionUvVertex
    {
        float position[3];
        float uv[2];
        uint32_t joints[4];
        float weights[4];
    };

    // Packed voxel vertex (8 B). Reflection derives the input layout (2x R32_UINT, stride 8) from the
    // voxel VS input struct, so both the voxel GBuffer VS and the voxel shadow VS bind this directly.
    // Bit layout — MUST match VoxelGBufferVS.hlsl / VoxelShadowVS.hlsl unpack:
    //   w0: posX[0:5] posY[5:10] posZ[10:15] normalId[15:18] ao[18:20]   (pos is section-local 0..16)
    //   w1: tile[0:16] u[16:24] v[24:32]
    // World position = constants[id].aabbMin (== section origin) + section-local pos. Section-local
    // positions fit 5 bits because kSectionDim==16 and voxel meshing is lod 0 (stride 1).
    struct VoxelVertex
    {
        uint32_t w0;
        uint32_t w1;
    };

    inline void FillVertexPosition(Vertex &vertex, float x, float y, float z)
    {
        vertex.position[0] = x;
        vertex.position[1] = y;
        vertex.position[2] = z;
    }

    inline void FillVertexPosition(PositionUvVertex &vertex, float x, float y, float z)
    {
        vertex.position[0] = x;
        vertex.position[1] = y;
        vertex.position[2] = z;
    }

    inline void FillVertexUV(Vertex &vertex, float u, float v)
    {
        vertex.uv[0] = u;
        vertex.uv[1] = v;
    }

    inline void FillVertexUV(PositionUvVertex &vertex, float u, float v)
    {
        vertex.uv[0] = u;
        vertex.uv[1] = v;
    }

    inline void FillVertexNormal(Vertex &vertex, float x, float y, float z)
    {
        vertex.normals[0] = x;
        vertex.normals[1] = y;
        vertex.normals[2] = z;
    }

    inline void FillVertexTangent(Vertex &vertex, float x, float y, float z, float w)
    {
        vertex.tangent[0] = x;
        vertex.tangent[1] = y;
        vertex.tangent[2] = z;
        vertex.tangent[3] = w;
    }

    inline void FillVertexColor(Vertex &vertex, float r, float g, float b, float a)
    {
        vertex.color[0] = r;
        vertex.color[1] = g;
        vertex.color[2] = b;
        vertex.color[3] = a;
    }

    inline void FillVertexJointsWeights(Vertex &vertex, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                        float w0, float w1, float w2, float w3)
    {
        vertex.joints[0] = j0;
        vertex.joints[1] = j1;
        vertex.joints[2] = j2;
        vertex.joints[3] = j3;
        vertex.weights[0] = w0;
        vertex.weights[1] = w1;
        vertex.weights[2] = w2;
        vertex.weights[3] = w3;
    }

    inline void FillVertexJointsWeights(PositionUvVertex &vertex, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                        float w0, float w1, float w2, float w3)
    {
        vertex.joints[0] = j0;
        vertex.joints[1] = j1;
        vertex.joints[2] = j2;
        vertex.joints[3] = j3;
        vertex.weights[0] = w0;
        vertex.weights[1] = w1;
        vertex.weights[2] = w2;
        vertex.weights[3] = w3;
    }
} // namespace pe
