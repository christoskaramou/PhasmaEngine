#include "AnimationEvaluator.h"

namespace pe
{
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

        for (int i = 0; i < boneCount; i++)
            outMatrices[i] = globalTransforms[i] * skeleton.bones[i].offsetMatrix;
    }
} // namespace pe
