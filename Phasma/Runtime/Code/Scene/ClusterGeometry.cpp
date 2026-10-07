#include "Scene/ClusterGeometry.h"
#include <meshoptimizer.h>
#define CLUSTERLOD_IMPLEMENTATION
#include <clusterlod.h>

namespace pe
{
    bool ValidateClusterGeometry(const ClusterGeometry &geometry, size_t vertexCount)
    {
        if (geometry.clusters.empty())
            return geometry.groups.empty() && geometry.indices.empty();
        if (geometry.groups.empty() || geometry.indices.size() > UINT32_MAX || geometry.groups.size() > INT32_MAX || geometry.clusters.size() > UINT32_MAX)
            return false;
        for (const ClusterGroup &group : geometry.groups)
            if (!std::isfinite(group.center[0]) || !std::isfinite(group.center[1]) ||
                !std::isfinite(group.center[2]) || !std::isfinite(group.radius) || group.radius < 0.f ||
                !std::isfinite(group.error) || group.error < 0.f)
                return false;
        for (const GeometryCluster &cluster : geometry.clusters)
        {
            if (!std::isfinite(cluster.center[0]) || !std::isfinite(cluster.center[1]) ||
                !std::isfinite(cluster.center[2]) || !std::isfinite(cluster.radius) || cluster.radius < 0.f ||
                cluster.group >= geometry.groups.size() || cluster.refinedGroup < -1 ||
                (cluster.refinedGroup >= 0 && static_cast<size_t>(cluster.refinedGroup) >= cluster.group) ||
                !cluster.indexCount || cluster.indexCount % 3 || cluster.indexCount > 128 * 3 ||
                cluster.indexOffset > geometry.indices.size() ||
                cluster.indexCount > geometry.indices.size() - cluster.indexOffset)
                return false;
        }
        return std::all_of(geometry.indices.begin(), geometry.indices.end(),
                           [vertexCount](uint32_t index)
                           { return index < vertexCount; });
    }

    ClusterGeometry BuildClusterGeometry(std::span<const Vertex> vertices, std::span<const uint32_t> indices)
    {
        ClusterGeometry result;
        if (vertices.empty() || indices.empty() || indices.size() % 3 ||
            indices.size() > UINT32_MAX || vertices.size() > UINT32_MAX)
            return result;
        for (uint32_t index : indices)
            if (index >= vertices.size())
                return result;
        std::vector<float> attributes(vertices.size() * 9);
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const Vertex &vertex = vertices[i];
            for (float position : vertex.position)
                if (!std::isfinite(position))
                    return {};
            std::copy_n(vertex.normals, 3, attributes.data() + i * 9);
            std::copy_n(vertex.uv, 2, attributes.data() + i * 9 + 3);
            std::copy_n(vertex.color, 4, attributes.data() + i * 9 + 5);
        }
        for (float attribute : attributes)
            if (!std::isfinite(attribute))
                return {};
        const float weights[] = {0.1f, 0.1f, 0.1f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
        clodConfig config = clodDefaultConfig(128);
        config.simplify_permissive = false;
        config.simplify_fallback_permissive = false;
        config.simplify_fallback_sloppy = false;
        clodMesh mesh{};
        mesh.indices = indices.data();
        mesh.index_count = indices.size();
        mesh.vertex_count = vertices.size();
        mesh.vertex_positions = vertices[0].position;
        mesh.vertex_positions_stride = sizeof(Vertex);
        mesh.vertex_attributes = attributes.data();
        mesh.vertex_attributes_stride = 9 * sizeof(float);
        mesh.attribute_weights = weights;
        mesh.attribute_count = 9;
        clodBuild(config, mesh, [&](clodGroup group, const clodCluster *clusters, size_t count)
                  {
            const uint32_t groupIndex = static_cast<uint32_t>(result.groups.size());
            result.groups.push_back({{group.simplified.center[0], group.simplified.center[1], group.simplified.center[2]},
                                     group.simplified.radius, group.simplified.error});
            for (size_t i = 0; i < count; ++i)
            {
                const clodCluster &source = clusters[i];
                GeometryCluster cluster{{source.bounds.center[0], source.bounds.center[1], source.bounds.center[2]},
                                        source.bounds.radius, static_cast<uint32_t>(result.indices.size()),
                                        static_cast<uint32_t>(source.index_count), groupIndex, source.refined};
                result.clusters.push_back(cluster);
                result.indices.insert(result.indices.end(), source.indices, source.indices + source.index_count);
            }
            return static_cast<int>(groupIndex); });
        return ValidateClusterGeometry(result, vertices.size()) ? std::move(result) : ClusterGeometry{};
    }
} // namespace pe
