#pragma once

#include "Animation/AnimationTypes.h"

namespace pe
{
    struct NodeId;
    class Scene;

    struct AnimationNodeState
    {
        NodeId *nodeId = nullptr;
        uint32_t nodeRevision = 0;
        int clipIndex = -1;
        float time = 0.0f;
        float speed = 1.0f;
        bool playing = false;
        bool loop = true;
    };

    class AnimationSystem : public ISystem
    {
    public:
        ~AnimationSystem() override;

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        void PlayAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop = true);
        void PlayAnimation(Scene &scene, NodeId *node, const std::string &clipName, bool loop = true);
        void StopAnimation(NodeId *node);
        void RemoveAnimation(NodeId *node);
        void SetSpeed(NodeId *node, float speed);
        bool IsPlaying(const NodeId *node) const;
        int GetCurrentClip(const NodeId *node) const;
        float GetCurrentTime(const NodeId *node) const;

        void ClearAllAnimations();

    private:
        std::unordered_map<const NodeId *, size_t> m_nodeToIndex;
        std::vector<AnimationNodeState> m_states;
    };
} // namespace pe
