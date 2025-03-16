#pragma once

namespace pe
{
    struct VertexInputBindingDescription
    {
        uint32_t binding;
        uint32_t stride;
        VertexInputRate inputRate;
    };

    struct VertexInputAttributeDescription
    {
        uint32_t location;
        uint32_t binding;
        Format format;
        uint32_t offset;
    };

    struct Vertex
    {
        float position[3];
        float uv[2];
        float normals[3];
        float color[4];
        uint32_t joints[4];
        float weights[4];
    };

    struct AabbVertex
    {
        float position[3];
    };

    struct PositionUvVertex
    {
        float position[3];
        float uv[2];
        uint32_t joints[4];
        float weights[4];
    };
}
