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

        static vec3 InterpolatePosition(const std::vector<PositionKey> &keys, float time);
        static quat InterpolateRotation(const std::vector<RotationKey> &keys, float time);
        static vec3 InterpolateScale(const std::vector<ScaleKey> &keys, float time);
        static float ApplyInterpolation(AnimationInterpolation interpolation, float factor);

    private:
        template <typename T>
        static int FindKeyIndex(const std::vector<AnimationKey<T>> &keys, float time);
    };
} // namespace pe
