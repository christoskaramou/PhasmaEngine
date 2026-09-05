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
        constexpr float kDefaultStripIkMaxBendRadians = kPi / 3.0f;
        constexpr float kMinStripIkBendPerSegment = kPi / 36.0f;
        constexpr float kMaxStripIkTotalArc = kPi * 5.0f / 3.0f;
        constexpr float kMinStripIkReachFraction = 0.15f;
        constexpr float kDefaultStripIkMaxStretch = 1.5f;
        constexpr float kMinStripIkStretch = 1.0f;
        constexpr float kMaxStripIkStretch = 3.0f;
        constexpr float kMinStripIkJointInfluence = 0.0f;
        constexpr float kMaxStripIkJointInfluence = 2.0f;
        constexpr float kMinStripWidthScale = 0.05f;
        constexpr float kMaxStripWidthScale = 3.0f;
        constexpr float kRadToDeg = 57.29577951308232f;

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

        float StripJointInfluence(const std::vector<float> *jointInfluences, int jointIndex)
        {
            if (!jointInfluences || jointIndex < 0 || jointIndex >= static_cast<int>(jointInfluences->size()))
                return 1.0f;

            const float influence = (*jointInfluences)[static_cast<size_t>(jointIndex)];
            if (!std::isfinite(influence))
                return 1.0f;
            return std::clamp(influence, kMinStripIkJointInfluence, kMaxStripIkJointInfluence);
        }

        void NormalizeStripJointInfluences(std::vector<float> &jointInfluences, int jointCount)
        {
            if (jointCount <= 0)
            {
                jointInfluences.clear();
                return;
            }

            if (jointInfluences.empty())
                jointInfluences.assign(static_cast<size_t>(jointCount), 1.0f);
            else
                jointInfluences.resize(static_cast<size_t>(jointCount), 1.0f);

            for (float &influence : jointInfluences)
            {
                if (!std::isfinite(influence))
                    influence = 1.0f;
                influence = std::clamp(influence, kMinStripIkJointInfluence, kMaxStripIkJointInfluence);
            }
        }

        float StripWidthScale(const std::vector<float> *widthScales, int jointIndex)
        {
            if (!widthScales || jointIndex < 0 || jointIndex >= static_cast<int>(widthScales->size()))
                return 1.0f;

            const float widthScale = (*widthScales)[static_cast<size_t>(jointIndex)];
            if (!std::isfinite(widthScale))
                return 1.0f;
            return std::clamp(widthScale, kMinStripWidthScale, kMaxStripWidthScale);
        }

        void NormalizeStripWidthScales(std::vector<float> &widthScales, int jointCount)
        {
            if (jointCount <= 0)
            {
                widthScales.clear();
                return;
            }

            if (widthScales.empty())
                widthScales.assign(static_cast<size_t>(jointCount), 1.0f);
            else
                widthScales.resize(static_cast<size_t>(jointCount), 1.0f);

            for (float &widthScale : widthScales)
            {
                if (!std::isfinite(widthScale))
                    widthScale = 1.0f;
                widthScale = std::clamp(widthScale, kMinStripWidthScale, kMaxStripWidthScale);
            }
        }

        void WriteSmoothStripJointMatrices(const Skeleton &skeleton,
                                           const std::vector<mat4> &globalTransforms,
                                           const std::vector<bool> &computed,
                                           std::vector<mat4> &outMatrices,
                                           const std::vector<float> *widthScales = nullptr)
        {
            const int boneCount = skeleton.GetBoneCount();
            outMatrices.resize(boneCount, mat4(1.0f));

            static thread_local std::vector<vec2> jointPositions;
            jointPositions.resize(boneCount);
            for (int i = 0; i < boneCount; i++)
                jointPositions[i] = computed[i] ? vec2(globalTransforms[i][3]) : vec2(0.0f);

            for (int i = 0; i < boneCount; i++)
            {
                if (!computed[i])
                {
                    outMatrices[i] = mat4(1.0f);
                    continue;
                }

                const vec2 fallbackTangent = SafeDirection(vec2(globalTransforms[i][0]), vec2(1.0f, 0.0f));
                vec2 tangent = fallbackTangent;
                if (boneCount > 1)
                {
                    if (i == 0)
                        tangent = jointPositions[1] - jointPositions[0];
                    else if (i == boneCount - 1)
                        tangent = jointPositions[boneCount - 1] - jointPositions[boneCount - 2];
                    else
                        tangent = jointPositions[i + 1] - jointPositions[i - 1];
                }

                const vec2 dir = SafeDirection(tangent, fallbackTangent);
                const float angle = std::atan2(dir.y, dir.x);
                const float widthScale = StripWidthScale(widthScales, i);
                const mat4 curveFrame = glm::translate(mat4(1.0f), vec3(jointPositions[i], 0.0f)) *
                                        glm::mat4_cast(glm::angleAxis(angle, vec3(0.0f, 0.0f, 1.0f))) *
                                        glm::scale(mat4(1.0f), vec3(1.0f, widthScale, 1.0f));
                outMatrices[i] = curveFrame * skeleton.bones[i].offsetMatrix;
            }
        }

        void SmoothExistingStripJointMatrices(const Skeleton &skeleton,
                                              std::vector<mat4> &jointMatrices,
                                              const std::vector<float> *widthScales = nullptr)
        {
            const int boneCount = skeleton.GetBoneCount();
            if (boneCount <= 0 || static_cast<int>(jointMatrices.size()) != boneCount)
                return;

            static thread_local std::vector<mat4> globalTransforms;
            static thread_local std::vector<bool> computed;
            globalTransforms.resize(boneCount);
            computed.assign(boneCount, true);
            for (int i = 0; i < boneCount; i++)
                globalTransforms[i] = jointMatrices[i] * glm::inverse(skeleton.bones[i].offsetMatrix);

            WriteSmoothStripJointMatrices(skeleton, globalTransforms, computed, jointMatrices, widthScales);
        }

        bool ApplyLocalRotationsZ(Scene &scene,
                                  NodeId *node,
                                  const Skeleton &skeleton,
                                  const std::vector<float> &rotationsRadians,
                                  float stretchScale,
                                  const std::vector<float> *widthScales)
        {
            const int boneCount = skeleton.GetBoneCount();
            if (boneCount <= 0)
                return false;

            if (!std::isfinite(stretchScale))
                stretchScale = 1.0f;
            stretchScale = std::clamp(stretchScale, kMinStripIkStretch, kMaxStripIkStretch);

            const NodeSkinnedStrip2DComponent *storedState = scene.GetSkinnedStrip2DState(node);
            if (!widthScales && storedState && !storedState->widthScales.empty())
                widthScales = &storedState->widthScales;

            static thread_local std::vector<mat4> localTransforms;
            static thread_local std::vector<mat4> globalTransforms;
            static thread_local std::vector<bool> computed;

            localTransforms.resize(boneCount);
            globalTransforms.resize(boneCount);
            computed.assign(boneCount, false);

            for (int i = 0; i < boneCount; i++)
            {
                localTransforms[i] = skeleton.bones[i].localBindTransform;
                if (i > 0 && stretchScale != 1.0f)
                {
                    const vec3 localTranslation = vec3(localTransforms[i][3]);
                    localTransforms[i][3] = vec4(localTranslation * stretchScale, localTransforms[i][3].w);
                }
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
            WriteSmoothStripJointMatrices(skeleton, globalTransforms, computed, rt.jointMatrices, widthScales);

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

    namespace
    {
        void EvaluateState(Scene &scene, const AnimationNodeState &state)
        {
            const auto &clips = scene.GetAnimationClipsForNode(state.nodeId);
            const Skeleton &skeleton = scene.GetSkeletonForNode(state.nodeId);
            if (state.clipIndex < 0 || state.clipIndex >= static_cast<int>(clips.size()) || skeleton.bones.empty())
                return;
            NodeRuntime &rt = scene.GetNodeRuntime(state.nodeId);
            AnimationEvaluator::EvaluatePose(clips[state.clipIndex], skeleton, state.time, rt.jointMatrices);
            const auto &layer = state.layer;
            if (layer.clipIndex >= 0 && layer.clipIndex < static_cast<int>(clips.size()))
            {
                static thread_local std::vector<mat4> overlay;
                AnimationEvaluator::EvaluatePose(clips[layer.clipIndex], skeleton, layer.time, overlay);
                // Copy complete rig-space poses: an attacking torso must not drag the base clip's feet.
                // Include the entire arm/prop group in the mask to preserve an authored two-hand grip.
                for (int bone : layer.bones)
                    if (bone >= 0 && bone < static_cast<int>(rt.jointMatrices.size()))
                        rt.jointMatrices[bone] = overlay[bone];
            }
            if (scene.NodeUsesSkinnedStrip2D(state.nodeId))
            {
                const auto *strip = scene.GetSkinnedStrip2DState(state.nodeId);
                SmoothExistingStripJointMatrices(skeleton, rt.jointMatrices, strip ? &strip->widthScales : nullptr);
            }
            scene.MarkNodeDirty(state.nodeId);
        }

        // The travel between the last applied clip time and the new one (through the loop seam when the clip
        // wrapped), taken from the carrier's channel space to rig space - the node's own space, where the skinning
        // lives - through its parent's bind global and its prefix, then into the node's parent space by the
        // node's basis. The pose keeps playing in place; only the node moves.
        void ApplyRootMotion(Scene &scene, AnimationNodeState &state, const AnimationClip &clip,
                             const Skeleton &skeleton, int wrapped)
        {
            const RootMotionTrack &track = clip.rootMotion;
            auto at = [&](float time)
            { return AnimationEvaluator::InterpolatePosition(track.positionKeys, time); };
            vec3 delta;
            if (wrapped > 0)
                delta = (at(clip.duration) - at(state.motionTime)) + (at(state.time) - at(0.0f));
            else if (wrapped < 0)
                delta = (at(0.0f) - at(state.motionTime)) + (at(state.time) - at(clip.duration));
            else
                delta = at(state.time) - at(state.motionTime);
            state.motionTime = state.time;
            if (glm::dot(delta, delta) < 1e-14f || !std::isfinite(glm::dot(delta, delta)))
                return;
            const BoneInfo &bone = skeleton.bones[track.boneIndex];
            const mat4 parentBind =
                bone.parentIndex >= 0 ? glm::inverse(skeleton.bones[bone.parentIndex].offsetMatrix) : mat4(1.0f);
            const vec3 rigDelta =
                mat3(glm::inverse(skeleton.rootTransform) * parentBind * bone.intermediatePrefix) * delta;
            mat4 local = scene.GetLocalMatrix(state.nodeId);
            local[3] += vec4(mat3(local) * rigDelta, 0.0f);
            scene.SetLocalMatrix(state.nodeId, local);
        }
    } // namespace

    void AnimationSystem::Update()
    {
        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        float dt = static_cast<float>(FrameTimer::Instance().GetDelta()) * Settings::Get<SceneSettings>().time_scale;
        if (dt <= 0.0001f)
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

            int wrapped = 0; // +1 the loop ran past the end, -1 past the start
            if (state.time >= clip.duration)
            {
                if (state.loop)
                {
                    state.time = std::fmod(state.time, clip.duration);
                    wrapped = 1;
                }
                else
                {
                    state.time = clip.duration;
                    state.playing = false;
                }
            }
            else if (state.time < 0.0f) // reverse playback
            {
                if (state.loop)
                {
                    state.time = clip.duration + std::fmod(state.time, clip.duration);
                    wrapped = -1;
                }
                else
                {
                    state.time = 0.0f;
                    state.playing = false;
                }
            }
            if (state.rootMotion && !clip.rootMotion.Empty() && clip.rootMotion.boneIndex < skeleton.GetBoneCount())
                ApplyRootMotion(*scene, state, clip, skeleton, wrapped);
            else
                state.motionTime = state.time;

            auto &layer = state.layer;
            if (layer.clipIndex >= 0 && layer.clipIndex < static_cast<int>(clips.size()))
            {
                const auto &overlay = clips[layer.clipIndex];
                const double duration = overlay.ticksPerSecond > 0.f ? overlay.duration / overlay.ticksPerSecond : 0.;
                if (duration > 0. && std::isfinite(duration))
                {
                    layer.elapsed += static_cast<double>(dt) * layer.speed;
                    if (!layer.loop)
                        layer.elapsed = std::clamp(layer.elapsed, 0., duration);
                    double time = layer.loop ? std::fmod(layer.elapsed, duration) : layer.elapsed;
                    if (time < 0.)
                        time += duration;
                    layer.time = static_cast<float>(time * overlay.ticksPerSecond);
                }
                else
                    layer = {};
            }
            EvaluateState(*scene, state);
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
        state.motionTime = 0.0f;
        state.loop = loop;
        state.playing = true;
    }

    bool AnimationSystem::PlayLayer(Scene &scene, NodeId *node, const std::string &clipName,
                                    const std::vector<std::string> &bones, bool loop, float speed)
    {
        if (!node || !scene.IsNodeAlive(node) || !std::isfinite(speed) || bones.empty())
            return false;
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || m_states[it->second].nodeRevision != node->revision)
            return false;
        const auto &skeleton = scene.GetSkeletonForNode(node);
        if (skeleton.planar2D || scene.NodeUsesSkinnedStrip2D(node))
            return false;
        const auto &clips = scene.GetAnimationClipsForNode(node);
        AnimationLayerState layer;
        for (int i = 0; i < static_cast<int>(clips.size()); ++i)
            if (clips[i].name == clipName)
                layer.clipIndex = i;
        if (layer.clipIndex < 0)
            return false;
        const auto &clip = clips[layer.clipIndex];
        if (!std::isfinite(clip.duration) || clip.duration <= 0.f ||
            !std::isfinite(clip.ticksPerSecond) || clip.ticksPerSecond <= 0.f)
            return false;
        for (const auto &name : bones)
        {
            int index = -1;
            for (int i = 0; i < skeleton.GetBoneCount(); ++i)
                if (skeleton.bones[i].name == name)
                    index = i;
            if (index < 0)
                return false;
            if (std::find(layer.bones.begin(), layer.bones.end(), index) == layer.bones.end())
                layer.bones.push_back(index);
        }
        layer.loop = loop;
        layer.speed = speed;
        m_states[it->second].layer = std::move(layer);
        EvaluateState(scene, m_states[it->second]);
        return true;
    }

    bool AnimationSystem::SetLayerSpeed(NodeId *node, float speed)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || m_states[it->second].layer.clipIndex < 0 || !std::isfinite(speed))
            return false;
        m_states[it->second].layer.speed = speed;
        return true;
    }

    void AnimationSystem::StopLayer(Scene &scene, NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (!node || !scene.IsNodeAlive(node) || it == m_nodeToIndex.end())
            return;
        m_states[it->second].layer = {};
        EvaluateState(scene, m_states[it->second]);
    }

    void AnimationSystem::SetRootMotion(NodeId *node, bool enabled)
    {
        auto it = m_nodeToIndex.find(node);
        if (it != m_nodeToIndex.end())
        {
            m_states[it->second].rootMotion = enabled;
            m_states[it->second].motionTime = m_states[it->second].time; // never catch up on travel skipped while off
        }
    }

    bool AnimationSystem::GetRootMotion(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        return it != m_nodeToIndex.end() && m_states[it->second].rootMotion;
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
        if (it == m_nodeToIndex.end() || !std::isfinite(speed))
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
        state.motionTime = state.time; // a scrub teleports the pose, never the node
        state.playing = false;         // pause during scrub

        EvaluateState(scene, state);
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

    bool AnimationSystem::SetJointLocalRotationsZ(Scene &scene,
                                                  NodeId *node,
                                                  const std::vector<float> &rotationsRadians,
                                                  float stretchScale,
                                                  const std::vector<float> *widthScales)
    {
        if (!node || !scene.IsNodeAlive(node) || !scene.NodeHasSkinnedMesh(node) || !scene.NodeUsesSkinnedStrip2D(node))
            return false;

        const Skeleton &skeleton = scene.GetSkeletonForNode(node);
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount <= 0)
            return false;

        StopAnimation(node);

        NodeSkinnedStrip2DComponent &state = scene.GetOrCreateSkinnedStrip2DState(node);
        state.rotationsRadians = rotationsRadians;
        state.stretchScale = std::clamp(stretchScale, kMinStripIkStretch, kMaxStripIkStretch);
        if (widthScales)
        {
            state.widthScales = *widthScales;
            NormalizeStripWidthScales(state.widthScales, boneCount);
        }

        return ApplyLocalRotationsZ(scene, node, skeleton, rotationsRadians, stretchScale, widthScales);
    }

    bool AnimationSystem::SolveStripIk2D(Scene &scene,
                                         NodeId *node,
                                         const vec2 &targetLocal,
                                         int iterations,
                                         std::vector<float> *outRotationsRadians,
                                         float maxBendRadians,
                                         float bendSignHint,
                                         float maxStretchScale,
                                         float *outStretchScale,
                                         const std::vector<float> *jointInfluences,
                                         const std::vector<float> *widthScales)
    {
        if (!node || !scene.IsNodeAlive(node) || !scene.NodeHasSkinnedMesh(node) || !scene.NodeUsesSkinnedStrip2D(node))
            return false;
        if (!std::isfinite(targetLocal.x) || !std::isfinite(targetLocal.y))
            return false;

        const Skeleton &skeleton = scene.GetSkeletonForNode(node);
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount < 2)
            return false;

        const NodeSkinnedStrip2DComponent *storedState = scene.GetSkinnedStrip2DState(node);
        if (!jointInfluences && storedState && !storedState->jointInfluences.empty())
            jointInfluences = &storedState->jointInfluences;
        if (!widthScales && storedState && !storedState->widthScales.empty())
            widthScales = &storedState->widthScales;

        static thread_local std::vector<vec2> bindPositions;
        static thread_local std::vector<float> lengths;
        static thread_local std::vector<float> bendInfluences;
        static thread_local std::vector<float> rotations;

        bindPositions.resize(boneCount);
        lengths.resize(boneCount - 1);
        bendInfluences.resize(boneCount - 1);
        rotations.assign(boneCount, 0.0f);

        for (int i = 0; i < boneCount; i++)
        {
            const vec2 localPosition = vec2(skeleton.bones[i].localBindTransform[3]);
            const int parent = skeleton.bones[i].parentIndex;
            bindPositions[i] = parent >= 0 && parent < i ? bindPositions[parent] + localPosition : localPosition;
        }

        float totalLength = 0.0f;
        for (int i = 0; i < boneCount - 1; i++)
        {
            lengths[i] = glm::max(glm::length(bindPositions[i + 1] - bindPositions[i]), kEpsilon);
            totalLength += lengths[i];
        }
        if (totalLength <= kEpsilon)
            return false;

        float influenceSum = 0.0f;
        float maxInfluence = 0.0f;
        for (int i = 0; i < boneCount - 1; i++)
        {
            bendInfluences[i] = StripJointInfluence(jointInfluences, i);
            influenceSum += bendInfluences[i];
            maxInfluence = std::max(maxInfluence, bendInfluences[i]);
        }

        const vec2 root = bindPositions[0];
        const vec2 targetVector = targetLocal - root;
        const float targetDistance = glm::length(targetVector);
        iterations = std::clamp(iterations, 1, 64);

        if (!std::isfinite(maxStretchScale))
            maxStretchScale = kDefaultStripIkMaxStretch;
        maxStretchScale = std::clamp(maxStretchScale, kMinStripIkStretch, kMaxStripIkStretch);
        if (!std::isfinite(maxBendRadians))
            maxBendRadians = kDefaultStripIkMaxBendRadians;
        const float requestedMaxBend = std::clamp(maxBendRadians, kMinStripIkBendPerSegment, kPi);

        const float stretchScale = std::clamp(targetDistance / totalLength, kMinStripIkStretch, maxStretchScale);
        const float stretchedTotalLength = totalLength * stretchScale;
        const float solveDistance = std::clamp(targetDistance, totalLength * kMinStripIkReachFraction, stretchedTotalLength);

        const int segmentCount = boneCount - 1;
        float bendSign = bendSignHint < -0.5f ? -1.0f : 1.0f;
        const float centerlineThreshold = glm::max(totalLength * 0.02f, kEpsilon);
        if (targetVector.y > centerlineThreshold)
            bendSign = 1.0f;
        else if (targetVector.y < -centerlineThreshold)
            bendSign = -1.0f;

        if (segmentCount <= 1 || targetDistance >= stretchedTotalLength - kEpsilon)
        {
            const vec2 dir = SafeDirection(targetVector, vec2(1.0f, 0.0f));
            rotations[0] = std::atan2(dir.y, dir.x);
        }
        else
        {
            auto chordForBend = [&](float bend) -> vec2
            {
                vec2 chord(0.0f);
                float accumulatedAngle = 0.0f;
                for (int i = 0; i < segmentCount; i++)
                {
                    const float segmentBend = bend * bendInfluences[i];
                    const float angle = accumulatedAngle + segmentBend * 0.5f;
                    chord += lengths[i] * stretchScale * vec2(std::cos(angle), std::sin(angle));
                    accumulatedAngle += segmentBend;
                }
                return chord;
            };

            float low = 0.0f;
            const float effectiveInfluenceSum = std::max(influenceSum, kEpsilon);
            const float physicalMaxBend = std::max(kTwoPi / effectiveInfluenceSum - 0.0001f, 0.0f);
            const float totalArcMaxBend = kMaxStripIkTotalArc / effectiveInfluenceSum;
            const float localMaxBend = maxInfluence > kEpsilon ? requestedMaxBend / maxInfluence : requestedMaxBend;
            float high = std::min({requestedMaxBend, totalArcMaxBend, physicalMaxBend, localMaxBend});
            for (int iteration = 0; iteration < iterations; iteration++)
            {
                const float mid = (low + high) * 0.5f;
                const float chordLength = glm::length(chordForBend(mid));
                if (chordLength > solveDistance)
                    low = mid;
                else
                    high = mid;
            }

            const float bend = high * bendSign;
            const vec2 chord = chordForBend(bend);
            const vec2 targetDir = SafeDirection(targetVector, vec2(1.0f, 0.0f));
            const float chordAngle = std::atan2(targetDir.y, targetDir.x);
            const float rawChordAngle = std::atan2(chord.y, chord.x);
            const float startAngle = chordAngle - rawChordAngle;

            float parentGlobalAngle = 0.0f;
            float accumulatedAngle = 0.0f;
            for (int i = 0; i < segmentCount; i++)
            {
                const float segmentBend = bend * bendInfluences[i];
                const float globalAngle = startAngle + accumulatedAngle + segmentBend * 0.5f;
                rotations[i] = NormalizeAngle(globalAngle - parentGlobalAngle);
                parentGlobalAngle = globalAngle;
                accumulatedAngle += segmentBend;
            }
        }

        if (outRotationsRadians)
            *outRotationsRadians = rotations;
        if (outStretchScale)
            *outStretchScale = stretchScale;

        StopAnimation(node);
        NodeSkinnedStrip2DComponent &state = scene.GetOrCreateSkinnedStrip2DState(node);
        state.rotationsRadians = rotations;
        state.ikTargetLocal = targetLocal;
        state.ikIterations = iterations;
        state.maxBendDegrees = requestedMaxBend * kRadToDeg;
        state.bendSign = bendSign;
        state.stretchScale = stretchScale;
        state.maxStretchScale = maxStretchScale;
        if (jointInfluences)
        {
            state.jointInfluences = *jointInfluences;
            NormalizeStripJointInfluences(state.jointInfluences, boneCount);
        }
        if (widthScales)
        {
            state.widthScales = *widthScales;
            NormalizeStripWidthScales(state.widthScales, boneCount);
        }

        return ApplyLocalRotationsZ(scene, node, skeleton, rotations, stretchScale, widthScales);
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
