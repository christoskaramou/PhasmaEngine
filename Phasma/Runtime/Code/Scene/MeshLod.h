#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>
#include "API/Vertex.h"

namespace pe
{
    inline bool BuildSkinningLodAttributes(std::span<const Vertex> vertices, size_t jointCount,
                                           std::vector<float> &attributes, std::vector<float> &weights)
    {
        attributes.clear();
        weights.clear();
        // ponytail: the linked meshoptimizer supports 16 attributes; larger palettes keep full detail.
        std::array<uint32_t, 16> palette{};
        size_t count = 0;
        for (const Vertex &vertex : vertices)
            for (size_t i = 0; i < 4; ++i)
            {
                const float weight = vertex.weights[i];
                if (!std::isfinite(weight) || weight < 0.f || weight > 1.f)
                    return false;
                if (weight == 0.f)
                    continue;
                if (vertex.joints[i] >= jointCount)
                    return false;
                const auto end = palette.begin() + count;
                if (std::find(palette.begin(), end, vertex.joints[i]) == end)
                {
                    if (count == palette.size())
                        return false;
                    palette[count++] = vertex.joints[i];
                }
            }
        if (!count)
            return false;

        // Dense weights have a stable metric. Bone indices themselves are not interpolatable attributes.
        attributes.assign(vertices.size() * count, 0.f);
        weights.assign(count, 0.1f);
        for (size_t v = 0; v < vertices.size(); ++v)
            for (size_t i = 0; i < 4; ++i)
                if (vertices[v].weights[i] > 0.f)
                {
                    const auto bone = std::find(palette.begin(), palette.begin() + count, vertices[v].joints[i]);
                    attributes[v * count + (bone - palette.begin())] += vertices[v].weights[i];
                }
        return true;
    }
} // namespace pe
