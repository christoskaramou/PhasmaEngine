#pragma once

namespace pe
{
    class VertexInputBindingDescription
    {
    public:
        uint32_t binding;
        uint32_t stride;
        VertexInputRate inputRate;
    };

    class VertexInputAttributeDescription
    {
    public:
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
        int bonesIDs[4];
        float weights[4];
    };

    struct ShadowVertex
    {
        float position[3];
        int bonesIDs[4];
        float weights[4];
    };
}