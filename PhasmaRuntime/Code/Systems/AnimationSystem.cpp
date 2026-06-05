#include "AnimationSystem.h"
#include "Animation/AnimationEvaluator.h"
#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"

namespace pe
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTwoPi = 6.28318530717958647692f;
        constexpr float kEpsilon = 0.00001f;

        float NormalizeAngle(float angle)
        {
            while (angle > kPi)
                angle -= kTwoPi;
            while (angle < -kPi)
                angle += kTwoPi;
            return angle;
        }

        vec2 SafeDirection(const vec2 &v, const vec2 &fallback)
        {
            const float len = glm::length(v);
            if (len > kEpsilon)
                return v / len;

            const float fallbackLen = glm::length(fallback);
            return fallbackLen > kEpsilon ? fallback / fallbackLen : vec2(1.0f, 0.0f);
        }

        bool ApplyLocalRotationsZ(Scene &scene, NodeId *node, const Skeleton &skeleton, const std::vector<float> &rotationsRadians)
        {
            const int boneCount = skeleton.GetBoneCount();
            if (boneCount <= 0)
                return false;

            static thread_local std::vector<mat4> localTransforms;
            static thread_local std::vector<mat4> globalTransforms;
            static thread_local std::vector<bool> computed;

            localTransforms.resize(boneCount);
            globalTransforms.resize(boneCount);
            computed.assign(boneCount, false);

            for (int i = 0; i < boneCount; i++)
            {
                localTransforms[i] = skeleton.bones[i].localBindTransform;
                if (i < static_cast<int>(rotationsRadians.size()) && std::isfinite(rotationsRadians[i]))
                {
                    const quat rotation = glm::angleAxis(rotationsRadians[i], vec3(0.0f, 0.0f, 1.0f));
                    localTransforms[i] = localTransforms[i] * glm::mat4_cast(rotation);
                }
            }

            int remaining = boneCount;
            while (remaining > 0)
            {
                int progress = 0;
                for (int i = 0; i < boneCount; i++)
                {
                    if (computed[i])
                        continue;

                    const int parent = skeleton.bones[i].parentIndex;
                    if (parent < 0 || parent >= boneCount)
                    {
                        globalTransforms[i] = localTransforms[i];
                        computed[i] = true;
                        progress++;
                        remaining--;
                    }
                    else if (computed[parent])
                    {
                        globalTransforms[i] = globalTransforms[parent] * localTransforms[i];
                        computed[i] = true;
                        progress++;
                        remaining--;
                    }
                }

                if (progress == 0)
                    break;
            }

            NodeRuntime &rt = scene.GetNodeRuntime(node);
            rt.jointMatrices.resize(boneCount, mat4(1.0f));
            for (int i = 0; i < boneCount; i++)
            {
                if (computed[i])
                    rt.jointMatrices[i] = globalTransforms[i] * skeleton.bones[i].offsetMatrix;
                else
                    rt.jointMatrices[i] = mat4(1.0f);
            }

            scene.MarkNodeDirty(node);
            return true;
        }
    } // namespace

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
        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        float dt = static_cast<float>(FrameTimer::Instance().GetDelta());

        if (m_states.empty())
            return;

        for (auto &state : m_states)
        {
            if (!state.playing)
                continue;

            if (!state.nodeId || state.nodeId->revision != state.nodeRevision || !scene->IsNodeAlive(state.nodeId))
            {
                PE_INFO("[Animation] Update invalidated state: node=%p storedRevision=%u", static_cast<void *>(state.nodeId), state.nodeRevision);
                state.playing = false;
                continue;
            }

            const Skeleton &skeleton = scene->GetSkeletonForNode(state.nodeId);
            const auto &clips = scene->GetAnimationClipsForNode(state.nodeId);
            if (skeleton.bones.empty() || clips.empty() || state.clipIndex < 0 || state.clipIndex >= static_cast<int>(clips.size()))
            {
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

            NodeRuntime &rt = scene->GetNodeRuntime(state.nodeId);
            AnimationEvaluator::EvaluatePose(clip, skeleton, state.time, rt.jointMatrices);
            scene->MarkNodeDirty(state.nodeId);
        }
    }

    void AnimationSystem::Destroy()
    {
        m_states.clear();
        m_nodeToIndex.clear();
    }

    void AnimationSystem::PlayAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop)
    {
        if (!node || !scene.IsNodeAlive(node))
        {
            PE_INFO("[Animation] PlayAnimation rejected: invalid node clipIndex=%d", clipIndex);
            return;
        }

        const auto &clips = scene.GetAnimationClipsForNode(node);
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        {
            PE_INFO("[Animation] PlayAnimation rejected: node='%s' clipIndex=%d clips=%zu",
                    scene.GetNodeName(node).c_str(), clipIndex, clips.size());
            return;
        }

        const Skeleton &skeleton = scene.GetSkeletonForNode(node);
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
        if (!node)
            return;

        const auto &clips = scene.GetAnimationClipsForNode(node);
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

    void AnimationSystem::SetPlaybackTime(Scene &scene, NodeId *node, float timeTicks)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;

        auto &state = m_states[it->second];
        const auto &clips = scene.GetAnimationClipsForNode(state.nodeId);
        if (state.clipIndex < 0 || state.clipIndex >= static_cast<int>(clips.size()))
            return;

        const AnimationClip &clip = clips[state.clipIndex];
        state.time = std::clamp(timeTicks, 0.f, clip.duration);
        state.playing = false; // pause during scrub

        const Skeleton &skeleton = scene.GetSkeletonForNode(state.nodeId);
        if (!skeleton.bones.empty())
        {
            NodeRuntime &rt = scene.GetNodeRuntime(state.nodeId);
            AnimationEvaluator::EvaluatePose(clip, skeleton, state.time, rt.jointMatrices);
            scene.MarkNodeDirty(state.nodeId);
        }
    }

    void AnimationSystem::SetPaused(NodeId *node, bool paused)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;
        m_states[it->second].playing = !paused;
    }

    void AnimationSystem::SetLoop(NodeId *node, bool loop)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;
        m_states[it->second].loop = loop;
    }

    const AnimationNodeState *AnimationSystem::GetAnimationState(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return nullptr;
        return &m_states[it->second];
    }

    bool AnimationSystem::SetJointLocalRotationsZ(Scene &scene, NodeId *node, const std::vector<float> &rotationsRadians)
    {
        if (!node || !scene.IsNodeAlive(node) || !scene.NodeHasSkinnedMesh(node) || !scene.NodeUsesSkinnedStrip2D(node))
            return false;

        const Skeleton &skeleton = scene.GetSkeletonForNode(node);
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount <= 0)
            return false;

        StopAnimation(node);

        return ApplyLocalRotationsZ(scene, node, skeleton, rotationsRadians);
    }

    bool AnimationSystem::SolveStripIk2D(Scene &scene, NodeId *node, const vec2 &targetLocal, int iterations)
    {
        if (!node || !scene.IsNodeAlive(node) || !scene.NodeHasSkinnedMesh(node) || !scene.NodeUsesSkinnedStrip2D(node))
            return false;
        if (!std::isfinite(targetLocal.x) || !std::isfinite(targetLocal.y))
            return false;

        const Skeleton &skeleton = scene.GetSkeletonForNode(node);
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount < 2)
            return false;

        static thread_local std::vector<vec2> bindPositions;
        static thread_local std::vector<vec2> positions;
        static thread_local std::vector<float> lengths;
        static thread_local std::vector<float> rotations;

        bindPositions.resize(boneCount);
        positions.resize(boneCount);
        lengths.resize(boneCount - 1);
        rotations.assign(boneCount, 0.0f);

        for (int i = 0; i < boneCount; i++)
        {
            const vec2 localPosition = vec2(skeleton.bones[i].localBindTransform[3]);
            const int parent = skeleton.bones[i].parentIndex;
            bindPositions[i] = parent >= 0 && parent < i ? bindPositions[parent] + localPosition : localPosition;
            positions[i] = bindPositions[i];
        }

        float totalLength = 0.0f;
        for (int i = 0; i < boneCount - 1; i++)
        {
            lengths[i] = glm::max(glm::length(bindPositions[i + 1] - bindPositions[i]), kEpsilon);
            totalLength += lengths[i];
        }
        if (totalLength <= kEpsilon)
            return false;

        const vec2 root = bindPositions[0];
        const float targetDistance = glm::length(targetLocal - root);
        iterations = std::clamp(iterations, 1, 64);

        if (targetDistance >= totalLength)
        {
            const vec2 dir = SafeDirection(targetLocal - root, vec2(1.0f, 0.0f));
            positions[0] = root;
            for (int i = 0; i < boneCount - 1; i++)
                positions[i + 1] = positions[i] + dir * lengths[i];
        }
        else
        {
            for (int iteration = 0; iteration < iterations; iteration++)
            {
                positions[boneCount - 1] = targetLocal;
                for (int i = boneCount - 2; i >= 0; i--)
                {
                    const vec2 fallback = bindPositions[i] - bindPositions[i + 1];
                    const vec2 dir = SafeDirection(positions[i] - positions[i + 1], fallback);
                    positions[i] = positions[i + 1] + dir * lengths[i];
                }

                positions[0] = root;
                for (int i = 0; i < boneCount - 1; i++)
                {
                    const vec2 fallback = bindPositions[i + 1] - bindPositions[i];
                    const vec2 dir = SafeDirection(positions[i + 1] - positions[i], fallback);
                    positions[i + 1] = positions[i] + dir * lengths[i];
                }
            }
        }

        float parentGlobalAngle = 0.0f;
        for (int i = 0; i < boneCount - 1; i++)
        {
            const vec2 dir = SafeDirection(positions[i + 1] - positions[i], bindPositions[i + 1] - bindPositions[i]);
            const float globalAngle = std::atan2(dir.y, dir.x);
            rotations[i] = NormalizeAngle(globalAngle - parentGlobalAngle);
            parentGlobalAngle = globalAngle;
        }

        StopAnimation(node);
        return ApplyLocalRotationsZ(scene, node, skeleton, rotations);
    }

    void AnimationSystem::ClearAllAnimations()
    {
        PE_INFO("[Animation] ClearAllAnimations: states=%zu", m_states.size());

        if (Scene *scene = GetActiveScene())
        {
            for (auto &state : m_states)
            {
                if (state.nodeId && state.nodeId->revision == state.nodeRevision)
                {
                    NodeRuntime &rt = scene->GetNodeRuntime(state.nodeId);
                    rt.jointMatrices.clear();
                    scene->MarkNodeDirty(state.nodeId);
                }
            }
        }

        m_states.clear();
        m_nodeToIndex.clear();
    }
} // namespace pe
