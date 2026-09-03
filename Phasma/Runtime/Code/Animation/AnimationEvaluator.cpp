#include "AnimationEvaluator.h"

namespace pe
{
    namespace
    {
        mat4 PlanarSplineFrame(const mat4 &source, const vec2 &tangent)
        {
            const float tangentLength = glm::length(tangent);
            if (tangentLength <= 1e-6f)
                return source;

            const vec2 y = tangent / tangentLength;
            const vec3 scale(glm::length(vec3(source[0])), glm::length(vec3(source[1])),
                             glm::length(vec3(source[2])));
            mat4 frame(1.f);
            frame[0] = vec4(y.y * scale.x, -y.x * scale.x, 0.f, 0.f);
            frame[1] = vec4(y.x * scale.y, y.y * scale.y, 0.f, 0.f);
            frame[2] = vec4(0.f, 0.f, scale.z, 0.f);
            frame[3] = source[3];
            return frame;
        }

        // Re-frames each planar spline link from a CENTRED tangent through its neighbours, so a chain
        // bends as one smooth curve instead of in rigid parts. The bake gives a planar rig pure Rz bind
        // frames, so rebuilding the frame from the tangent alone reproduces the rest pose exactly.
        // ponytail: the tail this draws is not the FK tail the puppet solver aims with, so a plain drag
        // on a spline chain lands near the handle rather than on it (measured ~2x the reachable minimum
        // on a two-link chain). Spline IK writes the stations themselves and is exact; make the puppet
        // solver iterate against the smoothed tail if that approximation ever matters.
        void SmoothPlanarSplines(const Skeleton &skeleton, std::vector<mat4> &globalTransforms)
        {
            const int boneCount = skeleton.GetBoneCount();
            static thread_local std::vector<int> chain;
            for (int first = 0; first < boneCount; ++first)
            {
                if (!skeleton.bones[first].spline)
                    continue;
                const int parent = skeleton.bones[first].parentIndex;
                if (parent >= 0 && parent < boneCount && skeleton.bones[parent].spline)
                    continue;

                chain.clear();
                for (int bone = first; bone >= 0;)
                {
                    chain.push_back(bone);
                    int child = -1;
                    for (int candidate = 0; candidate < boneCount; ++candidate)
                        if (skeleton.bones[candidate].spline && skeleton.bones[candidate].parentIndex == bone)
                        {
                            child = candidate;
                            break;
                        }
                    bone = child;
                }

                for (size_t i = 0; i < chain.size(); ++i)
                {
                    const int bone = chain[i];
                    const vec2 here(globalTransforms[bone][3]);
                    vec2 before = here, after = here;
                    if (i > 0)
                        before = vec2(globalTransforms[chain[i - 1]][3]);
                    if (i + 1 < chain.size())
                        after = vec2(globalTransforms[chain[i + 1]][3]);
                    else
                    {
                        const vec3 yAxis(globalTransforms[bone][1]);
                        const float scaleY = glm::length(yAxis);
                        const vec2 direction(yAxis);
                        if (scaleY > 1e-6f && glm::length(direction) > 1e-6f)
                            after += glm::normalize(direction) * skeleton.bones[bone].length * scaleY;
                    }
                    const vec2 tangent = i == 0 ? after - here : after - before;
                    globalTransforms[bone] = PlanarSplineFrame(globalTransforms[bone], tangent);
                }
            }
        }
    } // namespace

    template <typename T>
    int AnimationEvaluator::FindKeyIndex(const std::vector<AnimationKey<T>> &keys, float time)
    {
        for (int i = 0; i < static_cast<int>(keys.size()) - 1; i++)
        {
            if (time < keys[i + 1].time)
                return i;
        }
        return static_cast<int>(keys.size()) - 1;
    }

    float AnimationEvaluator::ApplyInterpolation(AnimationInterpolation interpolation, float factor)
    {
        factor = glm::clamp(factor, 0.0f, 1.0f);
        switch (interpolation)
        {
        case AnimationInterpolation::Smooth:
            return factor * factor * (3.0f - 2.0f * factor);
        case AnimationInterpolation::Stepped:
            return 0.0f;
        case AnimationInterpolation::Linear:
        default:
            return factor;
        }
    }

    void AnimationEvaluator::BindPose(const BoneInfo &bone, vec3 &pos, quat &rot, vec3 &scl)
    {
        const mat4 m = glm::inverse(bone.intermediatePrefix) * bone.localBindTransform;
        pos = vec3(m[3]);
        scl = vec3(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
        rot = glm::normalize(glm::quat_cast(mat3(vec3(m[0]) / scl.x, vec3(m[1]) / scl.y, vec3(m[2]) / scl.z)));
    }

    void AnimationEvaluator::SampleChannel(const AnimationChannel &chan, const BoneInfo &bone, float time, vec3 &pos,
                                           quat &rot, vec3 &scl)
    {
        if (chan.positionKeys.empty() || chan.rotationKeys.empty() || chan.scaleKeys.empty())
            BindPose(bone, pos, rot, scl);
        if (!chan.positionKeys.empty())
            pos = InterpolatePosition(chan.positionKeys, time);
        if (!chan.rotationKeys.empty())
            rot = InterpolateRotation(chan.rotationKeys, time);
        if (!chan.scaleKeys.empty())
            scl = InterpolateScale(chan.scaleKeys, time);
    }

    vec3 AnimationEvaluator::InterpolatePosition(const std::vector<PositionKey> &keys, float time)
    {
        if (keys.empty())
            return vec3(0.f);
        if (keys.size() == 1)
            return keys[0].value;

        int idx = FindKeyIndex(keys, time);
        int next = idx + 1;
        if (next >= static_cast<int>(keys.size()))
            return keys[idx].value;

        float dt = keys[next].time - keys[idx].time;
        float factor = (dt > 0.0f) ? (time - keys[idx].time) / dt : 0.0f;
        factor = ApplyInterpolation(keys[idx].interpolation, factor);

        return glm::mix(keys[idx].value, keys[next].value, factor);
    }

    quat AnimationEvaluator::InterpolateRotation(const std::vector<RotationKey> &keys, float time)
    {
        if (keys.empty())
            return quat(1.f, 0.f, 0.f, 0.f);
        if (keys.size() == 1)
            return keys[0].value;

        int idx = FindKeyIndex(keys, time);
        int next = idx + 1;
        if (next >= static_cast<int>(keys.size()))
            return keys[idx].value;

        float dt = keys[next].time - keys[idx].time;
        float factor = (dt > 0.0f) ? (time - keys[idx].time) / dt : 0.0f;
        factor = ApplyInterpolation(keys[idx].interpolation, factor);

        return glm::slerp(keys[idx].value, keys[next].value, factor);
    }

    vec3 AnimationEvaluator::InterpolateScale(const std::vector<ScaleKey> &keys, float time)
    {
        if (keys.empty())
            return vec3(1.f);
        if (keys.size() == 1)
            return keys[0].value;

        int idx = FindKeyIndex(keys, time);
        int next = idx + 1;
        if (next >= static_cast<int>(keys.size()))
            return keys[idx].value;

        float dt = keys[next].time - keys[idx].time;
        float factor = (dt > 0.0f) ? (time - keys[idx].time) / dt : 0.0f;
        factor = ApplyInterpolation(keys[idx].interpolation, factor);

        return glm::mix(keys[idx].value, keys[next].value, factor);
    }

    void AnimationEvaluator::EvaluatePose(const AnimationClip &clip,
                                          const Skeleton &skeleton,
                                          float time,
                                          std::vector<mat4> &outMatrices)
    {
        int boneCount = skeleton.GetBoneCount();
        outMatrices.resize(boneCount, mat4(1.f));

        static thread_local std::vector<mat4> localTransforms;
        static thread_local std::vector<mat4> globalTransforms;
        static thread_local std::vector<int> boneToChannel;
        static thread_local std::vector<bool> computed;

        localTransforms.resize(boneCount);
        globalTransforms.resize(boneCount);
        boneToChannel.assign(boneCount, -1);
        computed.assign(boneCount, false);

        for (int i = 0; i < boneCount; i++)
            localTransforms[i] = skeleton.bones[i].localBindTransform;

        for (int ch = 0; ch < static_cast<int>(clip.channels.size()); ch++)
        {
            int bi = clip.channels[ch].boneIndex;
            if (bi >= 0 && bi < boneCount)
                boneToChannel[bi] = ch;
        }

        for (int i = 0; i < boneCount; i++)
        {
            int chIdx = boneToChannel[i];
            if (chIdx >= 0)
            {
                vec3 pos, scl;
                quat rot;
                SampleChannel(clip.channels[chIdx], skeleton.bones[i], time, pos, rot, scl);

                mat4 animatedLocal = glm::translate(mat4(1.f), pos) *
                                     glm::mat4_cast(rot) *
                                     glm::scale(mat4(1.f), scl);
                localTransforms[i] = skeleton.bones[i].intermediatePrefix * animatedLocal;
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
                int parent = skeleton.bones[i].parentIndex;
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

        if (skeleton.planar2D)
            SmoothPlanarSplines(skeleton, globalTransforms);

        for (int i = 0; i < boneCount; i++)
            outMatrices[i] = globalTransforms[i] * skeleton.bones[i].offsetMatrix;
    }
} // namespace pe
