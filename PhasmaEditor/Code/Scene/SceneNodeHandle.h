#pragma once

#include "Scene/SceneNode.h"

namespace pe
{
    class Scene;

    struct SceneNodeHandle
    {
        NodeId *nodeId = nullptr;
        uint32_t generation = 0;
        uint32_t nodeRevision = 0;

        SceneNodeHandle() = default;
        SceneNodeHandle(NodeId *id, uint32_t gen, uint32_t rev) : nodeId(id), generation(gen), nodeRevision(rev) {}

        bool IsValid(const Scene &scene) const;
        bool IsReady(const Scene &scene) const;

        bool operator==(const SceneNodeHandle &other) const
        {
            return nodeId == other.nodeId && generation == other.generation && nodeRevision == other.nodeRevision;
        }
    };
} // namespace pe
