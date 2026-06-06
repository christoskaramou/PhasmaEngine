#pragma once

#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/AnimationSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace pe::skinned_strip_2d_editor
{
    inline constexpr float kMinBendLimitDegrees = 5.0f;
    inline constexpr float kDefaultBendLimitDegrees = 60.0f;
    inline constexpr float kMaxBendLimitDegrees = 180.0f;
    inline constexpr float kMinStretchScale = 1.0f;
    inline constexpr float kDefaultStretchLimit = 1.5f;
    inline constexpr float kMaxStretchLimit = 3.0f;
    inline constexpr float kMinJointInfluence = 0.0f;
    inline constexpr float kMaxJointInfluence = 2.0f;
    inline constexpr float kMinWidthScale = 0.05f;
    inline constexpr float kMaxWidthScale = 3.0f;

    struct State
    {
        uint32_t nodeRevision = 0;
        int jointCount = 0;
        std::vector<float> rotationsRadians;
        std::vector<float> jointInfluences;
        std::vector<float> widthScales;
        vec2 ikTargetLocal = vec2(1.0f, 0.0f);
        int ikIterations = 8;
        float maxBendDegrees = kDefaultBendLimitDegrees;
        float bendSign = 1.0f;
        float stretchScale = 1.0f;
        float maxStretchScale = kDefaultStretchLimit;
    };

    inline bool GetBindPositionsLocal(const Scene &scene, const NodeId *node, std::vector<vec2> &bindPositions)
    {
        const Skeleton &skeleton = scene.GetSkeletonForNode(node);
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount <= 0)
        {
            bindPositions.clear();
            return false;
        }

        bindPositions.assign(static_cast<size_t>(boneCount), vec2(0.0f));
        for (int i = 0; i < boneCount; i++)
        {
            const vec2 localPosition = vec2(skeleton.bones[i].localBindTransform[3]);
            const int parent = skeleton.bones[i].parentIndex;
            bindPositions[static_cast<size_t>(i)] =
                parent >= 0 && parent < i ? bindPositions[static_cast<size_t>(parent)] + localPosition : localPosition;
        }

        return true;
    }

    inline vec2 GetBindRootLocal(const Scene &scene, const NodeId *node)
    {
        std::vector<vec2> bindPositions;
        if (!GetBindPositionsLocal(scene, node, bindPositions))
            return vec2(0.0f);

        return bindPositions.front();
    }

    inline vec2 GetBindEndLocal(const Scene &scene, const NodeId *node)
    {
        std::vector<vec2> bindPositions;
        if (!GetBindPositionsLocal(scene, node, bindPositions))
            return vec2(1.0f, 0.0f);

        return bindPositions.back();
    }

    inline float GetBindChainLengthLocal(const Scene &scene, const NodeId *node)
    {
        std::vector<vec2> bindPositions;
        if (!GetBindPositionsLocal(scene, node, bindPositions))
            return 0.0f;

        float length = 0.0f;
        for (size_t i = 1; i < bindPositions.size(); i++)
            length += glm::length(bindPositions[i] - bindPositions[i - 1]);
        return length;
    }

    inline bool GetPoseJointPositionsLocal(const Scene &scene, const NodeId *node, const State &state, std::vector<vec2> &positions)
    {
        std::vector<vec2> bindPositions;
        if (!GetBindPositionsLocal(scene, node, bindPositions))
            return false;

        positions = bindPositions;
        float globalAngle = 0.0f;
        for (size_t i = 1; i < positions.size(); i++)
        {
            const size_t rotationIndex = i - 1;
            if (rotationIndex < state.rotationsRadians.size() && std::isfinite(state.rotationsRadians[rotationIndex]))
                globalAngle += state.rotationsRadians[rotationIndex];

            const float stretchScale = std::isfinite(state.stretchScale) ? state.stretchScale : 1.0f;
            const float length = glm::length(bindPositions[i] - bindPositions[i - 1]) *
                                 std::clamp(stretchScale, kMinStretchScale, kMaxStretchLimit);
            positions[i] = positions[i - 1] + length * vec2(std::cos(globalAngle), std::sin(globalAngle));
        }

        return true;
    }

    inline std::unordered_map<const NodeId *, State> &States()
    {
        static std::unordered_map<const NodeId *, State> states;
        return states;
    }

    inline void NormalizeState(State &state, int jointCount)
    {
        state.jointCount = jointCount;
        state.ikIterations = std::clamp(state.ikIterations, 1, 64);
        state.maxBendDegrees = std::clamp(state.maxBendDegrees, kMinBendLimitDegrees, kMaxBendLimitDegrees);
        state.bendSign = state.bendSign < 0.0f ? -1.0f : 1.0f;
        state.maxStretchScale = std::clamp(state.maxStretchScale, kMinStretchScale, kMaxStretchLimit);
        state.stretchScale = std::clamp(state.stretchScale, kMinStretchScale, state.maxStretchScale);
        state.rotationsRadians.resize(static_cast<size_t>(std::max(jointCount, 0)), 0.0f);
        if (state.jointInfluences.empty())
            state.jointInfluences.assign(static_cast<size_t>(std::max(jointCount, 0)), 1.0f);
        else
            state.jointInfluences.resize(static_cast<size_t>(std::max(jointCount, 0)), 1.0f);
        for (float &influence : state.jointInfluences)
        {
            if (!std::isfinite(influence))
                influence = 1.0f;
            influence = std::clamp(influence, kMinJointInfluence, kMaxJointInfluence);
        }
        if (state.widthScales.empty())
            state.widthScales.assign(static_cast<size_t>(std::max(jointCount, 0)), 1.0f);
        else
            state.widthScales.resize(static_cast<size_t>(std::max(jointCount, 0)), 1.0f);
        for (float &widthScale : state.widthScales)
        {
            if (!std::isfinite(widthScale))
                widthScale = 1.0f;
            widthScale = std::clamp(widthScale, kMinWidthScale, kMaxWidthScale);
        }
    }

    inline void PersistState(Scene &scene, NodeId *node, State &state)
    {
        if (!node || !scene.IsNodeAlive(node) || !scene.NodeUsesSkinnedStrip2D(node))
            return;

        NormalizeState(state, state.jointCount);
        NodeSkinnedStrip2DComponent &stored = scene.GetOrCreateSkinnedStrip2DState(node);
        stored.rotationsRadians = state.rotationsRadians;
        stored.jointInfluences = state.jointInfluences;
        stored.widthScales = state.widthScales;
        stored.ikTargetLocal = state.ikTargetLocal;
        stored.ikIterations = state.ikIterations;
        stored.maxBendDegrees = state.maxBendDegrees;
        stored.bendSign = state.bendSign;
        stored.stretchScale = state.stretchScale;
        stored.maxStretchScale = state.maxStretchScale;
    }

    inline void PruneDeadStates(const Scene &scene)
    {
        auto &states = States();
        for (auto it = states.begin(); it != states.end();)
        {
            if (!scene.IsNodeAlive(it->first))
                it = states.erase(it);
            else
                ++it;
        }
    }

    inline State &GetState(Scene &scene, NodeId *node)
    {
        PruneDeadStates(scene);

        State &state = States()[node];
        const int jointCount = std::max(scene.GetJointCountForNode(node), 0);
        if (state.nodeRevision != node->revision || state.jointCount != jointCount)
        {
            if (const NodeSkinnedStrip2DComponent *stored = scene.GetSkinnedStrip2DState(node))
            {
                state.rotationsRadians = stored->rotationsRadians;
                state.jointInfluences = stored->jointInfluences;
                state.widthScales = stored->widthScales;
                state.ikTargetLocal = stored->ikTargetLocal;
                state.ikIterations = stored->ikIterations;
                state.maxBendDegrees = stored->maxBendDegrees;
                state.bendSign = stored->bendSign;
                state.stretchScale = stored->stretchScale;
                state.maxStretchScale = stored->maxStretchScale;
            }
            else
            {
                state.rotationsRadians.clear();
                state.jointInfluences.clear();
                state.widthScales.clear();
                state.ikTargetLocal = GetBindEndLocal(scene, node);
                state.ikIterations = std::clamp(state.ikIterations, 1, 64);
                state.maxBendDegrees = std::clamp(state.maxBendDegrees, kMinBendLimitDegrees, kMaxBendLimitDegrees);
                state.bendSign = state.bendSign < 0.0f ? -1.0f : 1.0f;
                state.stretchScale = std::clamp(state.stretchScale, kMinStretchScale, kMaxStretchLimit);
                state.maxStretchScale = std::clamp(state.maxStretchScale, kMinStretchScale, kMaxStretchLimit);
            }

            state.nodeRevision = node->revision;
            NormalizeState(state, jointCount);
        }

        return state;
    }

    inline bool ApplyRotations(AnimationSystem *anim, Scene &scene, NodeId *node, State &state)
    {
        if (!anim || state.jointCount <= 0)
            return false;
        NormalizeState(state, state.jointCount);
        if (!anim->SetJointLocalRotationsZ(scene, node, state.rotationsRadians, state.stretchScale, &state.widthScales))
            return false;
        PersistState(scene, node, state);
        return true;
    }

    inline bool SolveIk(AnimationSystem *anim, Scene &scene, NodeId *node, State &state)
    {
        if (!anim || state.jointCount <= 0)
            return false;
        state.ikIterations = std::clamp(state.ikIterations, 1, 64);
        state.maxBendDegrees = std::clamp(state.maxBendDegrees, kMinBendLimitDegrees, kMaxBendLimitDegrees);
        state.maxStretchScale = std::clamp(state.maxStretchScale, kMinStretchScale, kMaxStretchLimit);

        const vec2 root = GetBindRootLocal(scene, node);
        const float chainLength = glm::max(GetBindChainLengthLocal(scene, node), 0.01f);
        const float signDeadZone = glm::max(chainLength * 0.02f, 0.001f);
        const float targetY = state.ikTargetLocal.y - root.y;
        if (targetY > signDeadZone)
            state.bendSign = 1.0f;
        else if (targetY < -signDeadZone)
            state.bendSign = -1.0f;

        if (!anim->SolveStripIk2D(scene,
                                  node,
                                  state.ikTargetLocal,
                                  state.ikIterations,
                                  &state.rotationsRadians,
                                  glm::radians(state.maxBendDegrees),
                                  state.bendSign,
                                  state.maxStretchScale,
                                  &state.stretchScale,
                                  &state.jointInfluences,
                                  &state.widthScales))
        {
            return false;
        }

        PersistState(scene, node, state);
        return true;
    }
} // namespace pe::skinned_strip_2d_editor
