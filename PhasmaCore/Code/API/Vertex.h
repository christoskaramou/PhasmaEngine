#pragma once

namespace pe
{
    // gbuffer vertex structure
    struct Vertex
    {
        float position[3];
        float uv[2];
        float normals[3];
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
}
