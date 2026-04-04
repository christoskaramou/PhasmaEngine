#include "AnimationSystem.h"
#include "Animation/AnimationEvaluator.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    AnimationSystem::~AnimationSystem()
    {
        Destroy();
    }

    void AnimationSystem::Init(CommandBuffer *)
    {
        SetEnabled(true);
    }

    void AnimationSystem::Update()
    {
        auto *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;

        Scene &scene = rs->GetScene();
        const Skeleton &skeleton = scene.GetSkeleton();
        const auto &clips = scene.GetAnimationClips();
        float dt = static_cast<float>(FrameTimer::Instance().GetDelta());

        if (skeleton.bones.empty() || clips.empty() || m_states.empty())
            return;

        for (auto &state : m_states)
        {
            if (!state.playing || state.clipIndex < 0 || state.clipIndex >= static_cast<int>(clips.size()))
                continue;

            if (!state.nodeId || state.nodeId->revision != state.nodeRevision)
            {
                PE_INFO("[Animation] Update invalidated state: node=%p storedRevision=%u", static_cast<void *>(state.nodeId), state.nodeRevision);
                state.playing = false;
                continue;
            }

            const AnimationClip &clip = clips[state.clipIndex];

            state.time += dt * state.speed * clip.ticksPerSecond;

            if (state.time >= clip.duration)
            {
                if (state.loop)
                    state.time = std::fmod(state.time, clip.duration);
                else
                {
                    state.time = clip.duration;
                    state.playing = false;
                }
            }

            NodeRuntime &rt = scene.GetNodeRuntime(state.nodeId);
            AnimationEvaluator::EvaluatePose(clip, skeleton, state.time, rt.jointMatrices);
            scene.MarkNodeDirty(state.nodeId);
        }
    }

    void AnimationSystem::Destroy()
    {
        m_states.clear();
        m_nodeToIndex.clear();
    }

    void AnimationSystem::PlayAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop)
    {
        if (!node)
        {
            PE_INFO("[Animation] PlayAnimation rejected: null node clipIndex=%d", clipIndex);
            return;
        }

        const auto &clips = scene.GetAnimationClips();
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        {
            PE_INFO("[Animation] PlayAnimation rejected: node='%s' clipIndex=%d clips=%zu",
                    scene.GetNodeName(node).c_str(), clipIndex, clips.size());
            return;
        }

        const Skeleton &skeleton = scene.GetSkeleton();
        const char *clipName = clips[clipIndex].name.empty() ? "<unnamed>" : clips[clipIndex].name.c_str();
        PE_INFO("[Animation] PlayAnimation node='%s' clipIndex=%d clip='%s' loop=%d skinned=%d bones=%zu clips=%zu",
                scene.GetNodeName(node).c_str(),
                clipIndex,
                clipName,
                loop ? 1 : 0,
                scene.NodeHasSkinnedMesh(node) ? 1 : 0,
                skeleton.bones.size(),
                clips.size());

        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
        {
            size_t idx = m_states.size();
            AnimationNodeState state;
            state.nodeId = node;
            state.nodeRevision = node->revision;
            m_states.push_back(state);
            m_nodeToIndex[node] = idx;
            it = m_nodeToIndex.find(node);
        }
        else
        {
            m_states[it->second].nodeRevision = node->revision;
        }

        auto &state = m_states[it->second];
        state.clipIndex = clipIndex;
        state.time = 0.0f;
        state.loop = loop;
        state.playing = true;
    }

    void AnimationSystem::PlayAnimation(Scene &scene, NodeId *node, const std::string &clipName, bool loop)
    {
        const auto &clips = scene.GetAnimationClips();
        for (int i = 0; i < static_cast<int>(clips.size()); i++)
        {
            if (clips[i].name == clipName)
            {
                PlayAnimation(scene, node, i, loop);
                return;
            }
        }
    }

    void AnimationSystem::StopAnimation(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;
        m_states[it->second].playing = false;
    }

    void AnimationSystem::RemoveAnimation(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;

        size_t idx = it->second;
        size_t last = m_states.size() - 1;

        if (idx != last)
        {
            m_states[idx] = std::move(m_states[last]);
            m_nodeToIndex[m_states[idx].nodeId] = idx;
        }

        m_states.pop_back();
        m_nodeToIndex.erase(it);
    }

    void AnimationSystem::SetSpeed(NodeId *node, float speed)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;
        m_states[it->second].speed = speed;
    }

    bool AnimationSystem::IsPlaying(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        return it != m_nodeToIndex.end() && m_states[it->second].playing;
    }

    int AnimationSystem::GetCurrentClip(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        return (it != m_nodeToIndex.end()) ? m_states[it->second].clipIndex : -1;
    }

    float AnimationSystem::GetPlaybackTime(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        return (it != m_nodeToIndex.end()) ? m_states[it->second].time : 0.0f;
    }

    void AnimationSystem::ClearAllAnimations()
    {
        PE_INFO("[Animation] ClearAllAnimations: states=%zu", m_states.size());

        if (auto *rs = GetGlobalSystem<RendererSystem>())
        {
            Scene &scene = rs->GetScene();
            for (auto &state : m_states)
            {
                if (state.nodeId && state.nodeId->revision == state.nodeRevision)
                {
                    NodeRuntime &rt = scene.GetNodeRuntime(state.nodeId);
                    rt.jointMatrices.clear();
                    scene.MarkNodeDirty(state.nodeId);
                }
            }
        }

        m_states.clear();
        m_nodeToIndex.clear();
    }
} // namespace pe
