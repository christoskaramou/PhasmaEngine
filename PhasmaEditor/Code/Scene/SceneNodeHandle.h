#pragma once

#include "Scene/SceneNode.h"

namespace pe
{
    class Scene;

    struct SceneNodeHandle
    {
        NodeId *nodeId = nullptr;
        uint32_t generation = 0;

        SceneNodeHandle() = default;
        SceneNodeHandle(NodeId *id, uint32_t gen) : nodeId(id), generation(gen) {}

        bool IsValid(const Scene &scene) const;
        bool IsReady(const Scene &scene) const;

        bool operator==(const SceneNodeHandle &other) const
        {
            return nodeId == other.nodeId && generation == other.generation;
        }
    };
} // namespace pe
