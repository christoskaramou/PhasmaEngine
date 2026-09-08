#pragma once

#include <span>
#include "API/Vertex.h"

namespace pe
{
    // Bind-space bounds of the vertices influenced by each joint, shared by mesh instances.
    struct SkinnedBounds
    {
        static AABB Empty()
        {
            return {vec3(std::numeric_limits<float>::max()), vec3(-std::numeric_limits<float>::max())};
        }

        std::vector<AABB> joints;
        AABB unweighted = Empty();
        float negativeWeight = 0.f;

        void Build(std::span<const Vertex> vertices, size_t jointCount)
        {
            joints.assign(jointCount, Empty());
            unweighted = Empty();
            negativeWeight = 0.f;
            for (const Vertex &v : vertices)
            {
                const vec3 p(v.position[0], v.position[1], v.position[2]);
                const float sum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
                if (sum <= 0.f || v.joints[3] == 0x53505254u) // same identity / sprite bypass as the vertex shaders
                {
                    unweighted.min = min(unweighted.min, p);
                    unweighted.max = max(unweighted.max, p);
                    continue;
                }
                float negative = 0.f;
                for (int k = 0; k < 4; ++k)
                {
                    if (v.weights[k] == 0.f || v.joints[k] >= jointCount)
                        continue;
                    AABB &box = joints[v.joints[k]];
                    box.min = min(box.min, p);
                    box.max = max(box.max, p);
                    negative += std::max(-v.weights[k] / sum, 0.f);
                }
                negativeWeight = std::max(negativeWeight, negative);
            }
        }

        AABB Pose(std::span<const mat4> matrices, const mat4 &basis, const mat4 &world) const
        {
            AABB result = Empty();
            auto include = [&](const AABB &box, const mat4 &m)
            {
                if (box.min.x > box.max.x)
                    return;
                const vec3 center = vec3(m * vec4(box.GetCenter(), 1.f));
                const vec3 half = box.GetSize() * .5f;
                const vec3 extent = abs(vec3(m[0])) * half.x + abs(vec3(m[1])) * half.y + abs(vec3(m[2])) * half.z;
                result.min = min(result.min, center - extent);
                result.max = max(result.max, center + extent);
            };
            include(unweighted, world);
            for (size_t j = 0; j < joints.size() && j < matrices.size(); ++j)
                include(joints[j], basis * matrices[j]);
            if (result.min.x > result.max.x)
                return result;
            // Signed Catmull-Rom weights can leave the joint-box union by at most N * its extent.
            const vec3 margin = (result.max - result.min) * negativeWeight + vec3(1e-5f);
            result.min -= margin;
            result.max += margin;
            return result;
        }
    };
} // namespace pe
