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
        float GetPlaybackTime(const NodeId *node) const;

        // Timeline scrubbing: sets time in ticks, evaluates pose immediately, pauses playback.
        void SetPlaybackTime(Scene &scene, NodeId *node, float timeTicks);

        // Pause/resume without resetting time.
        void SetPaused(NodeId *node, bool paused);

        // Change loop flag without resetting time.
        void SetLoop(NodeId *node, bool loop);

        // Read-only access to animation state for UI. Returns nullptr if node has no state.
        const AnimationNodeState *GetAnimationState(const NodeId *node) const;

        // Procedural pose helper for generated skinned_strip_2d nodes.
        bool SetJointLocalRotationsZ(Scene &scene,
                                     NodeId *node,
                                     const std::vector<float> &rotationsRadians,
                                     float stretchScale = 1.0f);
        bool SolveStripIk2D(Scene &scene,
                            NodeId *node,
                            const vec2 &targetLocal,
                            int iterations = 8,
                            std::vector<float> *outRotationsRadians = nullptr,
                            float maxBendRadians = 1.04719755f,
                            float bendSignHint = 0.0f,
                            float maxStretchScale = 1.5f,
                            float *outStretchScale = nullptr,
                            const std::vector<float> *jointInfluences = nullptr);

        void ClearAllAnimations();

    private:
        std::unordered_map<const NodeId *, size_t> m_nodeToIndex;
        std::vector<AnimationNodeState> m_states;
    };
} // namespace pe
