#pragma once

#include "Animation/AnimationTypes.h"

namespace pe
{
    class AnimationEvaluator
    {
    public:
        // Evaluate a pose at the given time and produce one mat4 per bone
        // (final joint matrix = globalTransform * offsetMatrix, ready for GPU)
        static void EvaluatePose(const AnimationClip &clip,
                                 const Skeleton &skeleton,
                                 float time,
                                 std::vector<mat4> &outMatrices);

        // The bone's rest TRS in the space the animated channel replaces (localBind without the intermediatePrefix).
        static void BindPose(const BoneInfo &bone, vec3 &pos, quat &rot, vec3 &scl);
        // Channel TRS at time; components without keys keep the bind pose (glTF/Blender semantics), so a
        // hand-keyed rotation-only channel does not collapse the bone onto its parent.
        static void SampleChannel(const AnimationChannel &chan, const BoneInfo &bone, float time, vec3 &pos, quat &rot,
                                  vec3 &scl);

        static vec3 InterpolatePosition(const std::vector<PositionKey> &keys, float time);
        static quat InterpolateRotation(const std::vector<RotationKey> &keys, float time);
        static vec3 InterpolateScale(const std::vector<ScaleKey> &keys, float time);
        static float ApplyInterpolation(AnimationInterpolation interpolation, float factor);

    private:
        template <typename T>
        static int FindKeyIndex(const std::vector<AnimationKey<T>> &keys, float time);
    };
} // namespace pe
