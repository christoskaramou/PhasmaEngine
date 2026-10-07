#pragma once
#include "API/Vertex.h"

namespace pe
{
    struct ClusterGroup
    {
        float center[3];
        float radius;
        float error;
    };

    struct GeometryCluster
    {
        float center[3];
        float radius;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t group;
        int32_t refinedGroup;
    };

    static_assert(sizeof(ClusterGroup) == 20 && sizeof(GeometryCluster) == 32);

    struct ClusterGeometry
    {
        std::vector<ClusterGroup> groups;
        std::vector<GeometryCluster> clusters;
        std::vector<uint32_t> indices;
    };

    struct GeometryClusterGPU
    {
        float bounds[4];
        float coarserBounds[4];
        float finerBounds[4];
        float coarserError;
        float finerError;
        uint32_t firstIndex;
        uint32_t indexCount;
    };
    static_assert(sizeof(GeometryClusterGPU) == 64);

    bool ValidateClusterGeometry(const ClusterGeometry &geometry, size_t vertexCount);
    ClusterGeometry BuildClusterGeometry(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
} // namespace pe
