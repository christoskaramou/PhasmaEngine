#include "AnimationPoseTools.h"

#include "Animation/AnimationEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pe::AnimationPoseTools
{
    namespace
    {
        constexpr float kEpsilon = 0.00001f;

        bool Finite(const vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Finite(const quat &value)
        {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        bool Finite(const mat4 &value)
        {
            for (int column = 0; column < 4; column++)
                for (int row = 0; row < 4; row++)
                    if (!std::isfinite(value[column][row]))
                        return false;
            return true;
        }

        quat SafeNormalized(const quat &value)
        {
            const float lengthSquared = glm::dot(value, value);
            return std::isfinite(lengthSquared) && lengthSquared > kEpsilon ? value / std::sqrt(lengthSquared)
                                                                            : quat(1.f, 0.f, 0.f, 0.f);
        }

        vec3 StablePerpendicular(const vec3 &direction)
        {
            const vec3 axis = std::abs(direction.x) <= std::abs(direction.y) && std::abs(direction.x) <= std::abs(direction.z)
                                  ? vec3(1.f, 0.f, 0.f)
                              : std::abs(direction.y) <= std::abs(direction.z) ? vec3(0.f, 1.f, 0.f)
                                                                               : vec3(0.f, 0.f, 1.f);
            return glm::normalize(glm::cross(direction, axis));
        }

        quat RotationBetween(const vec3 &from, const vec3 &to)
        {
            const vec3 a = glm::normalize(from), b = glm::normalize(to);
            const float cosine = std::clamp(glm::dot(a, b), -1.f, 1.f);
            if (cosine > 1.f - kEpsilon)
                return quat(1.f, 0.f, 0.f, 0.f);
            if (cosine < -1.f + kEpsilon)
                return glm::angleAxis(glm::pi<float>(), StablePerpendicular(a));
            const vec3 axis = glm::cross(a, b);
            return SafeNormalized(quat(1.f + cosine, axis.x, axis.y, axis.z));
        }

        quat ShortestSlerp(const quat &from, const quat &to, float factor)
        {
            const quat a = SafeNormalized(from), normalizedTo = SafeNormalized(to);
            const quat b = glm::dot(a, normalizedTo) < 0.f
                               ? quat(-normalizedTo.w, -normalizedTo.x, -normalizedTo.y, -normalizedTo.z)
                               : normalizedTo;
            return SafeNormalized(glm::slerp(a, b, factor));
        }

        template <typename Key, typename Value>
        void WriteKey(std::vector<Key> &keys, float time, const Value &value)
        {
            const auto at = std::find_if(keys.begin(), keys.end(), [time](const Key &key)
                                         { return std::abs(key.time - time) < kEpsilon; });
            if (at != keys.end())
            {
                at->value = value;
                return;
            }
            const auto next = std::lower_bound(keys.begin(), keys.end(), time, [](const Key &key, float keyTime)
                                               { return key.time < keyTime; });
            const AnimationInterpolation interpolation = next != keys.begin() ? std::prev(next)->interpolation
                                                                              : AnimationInterpolation::Linear;
            keys.insert(next, {time, value, interpolation});
        }

        bool SampleRigGlobalPose(const AnimationClip &clip,
                                 const Skeleton &skeleton,
                                 float time,
                                 std::vector<mat4> &rigGlobals)
        {
            if (!std::isfinite(time) || skeleton.GetBoneCount() <= 0)
                return false;
            std::vector<mat4> joints;
            AnimationEvaluator::EvaluatePose(clip, skeleton, time, joints);
            if (joints.size() != skeleton.bones.size())
                return false;
            const mat4 inverseRoot = glm::inverse(skeleton.rootTransform);
            rigGlobals.resize(joints.size());
            for (int bone = 0; bone < static_cast<int>(joints.size()); bone++)
            {
                rigGlobals[bone] = inverseRoot * joints[bone] * glm::inverse(skeleton.bones[bone].offsetMatrix);
                if (!Finite(rigGlobals[bone]))
                    return false;
            }
            return true;
        }
    } // namespace

    TwoBoneIkResult SolveTwoBoneIk(const TwoBoneIkInput &input)
    {
        TwoBoneIkResult result;
        if (!Finite(input.rootPosition) || !Finite(input.midPosition) || !Finite(input.tipPosition) ||
            !Finite(input.targetPosition) || !Finite(input.polePosition))
        {
            result.status = TwoBoneIkStatus::NonFiniteInput;
            return result;
        }

        const vec3 upper = input.midPosition - input.rootPosition;
        const vec3 lower = input.tipPosition - input.midPosition;
        const float upperLength = glm::length(upper), lowerLength = glm::length(lower);
        if (upperLength <= kEpsilon)
        {
            result.status = TwoBoneIkStatus::DegenerateUpperBone;
            return result;
        }
        if (lowerLength <= kEpsilon)
        {
            result.status = TwoBoneIkStatus::DegenerateLowerBone;
            return result;
        }

        const vec3 toTarget = input.targetPosition - input.rootPosition;
        const float requestedDistance = glm::length(toTarget);
        vec3 targetDirection = requestedDistance > kEpsilon ? toTarget / requestedDistance : input.tipPosition - input.rootPosition;
        if (glm::dot(targetDirection, targetDirection) <= kEpsilon)
            targetDirection = upper;
        targetDirection = glm::normalize(targetDirection);

        const float minReach = std::abs(upperLength - lowerLength);
        const float maxReach = upperLength + lowerLength;
        const float solveDistance = std::clamp(requestedDistance, std::max(minReach, kEpsilon), maxReach);
        result.solvedDistance = solveDistance;
        result.targetClamped = std::abs(solveDistance - requestedDistance) > kEpsilon;
        result.solvedTipPosition = input.rootPosition + targetDirection * solveDistance;

        vec3 bendDirection = input.polePosition - input.rootPosition;
        bendDirection -= targetDirection * glm::dot(bendDirection, targetDirection);
        if (glm::dot(bendDirection, bendDirection) <= kEpsilon)
        {
            bendDirection = upper - targetDirection * glm::dot(upper, targetDirection);
            if (glm::dot(bendDirection, bendDirection) <= kEpsilon)
                bendDirection = StablePerpendicular(targetDirection);
        }
        bendDirection = glm::normalize(bendDirection);

        const float along = std::clamp((upperLength * upperLength - lowerLength * lowerLength + solveDistance * solveDistance) /
                                           (2.f * solveDistance),
                                       -upperLength, upperLength);
        const float height = std::sqrt(std::max(upperLength * upperLength - along * along, 0.f));
        result.solvedMidPosition = input.rootPosition + targetDirection * along + bendDirection * height;

        result.rootGlobalDelta = RotationBetween(upper, result.solvedMidPosition - input.rootPosition);
        const vec3 inheritedLower = result.rootGlobalDelta * lower;
        result.midGlobalDelta = RotationBetween(inheritedLower, result.solvedTipPosition - result.solvedMidPosition);
        return result;
    }

    BreakdownResult InsertBreakdown(AnimationClip &clip,
                                    const Skeleton &skeleton,
                                    int boneIndex,
                                    float time,
                                    float bias)
    {
        BreakdownResult result;
        if (boneIndex < 0 || boneIndex >= skeleton.GetBoneCount())
        {
            result.status = BreakdownStatus::InvalidBone;
            return result;
        }
        if (!std::isfinite(time) || time < 0.f || !std::isfinite(bias))
        {
            result.status = BreakdownStatus::InvalidTime;
            return result;
        }
        result.appliedBias = std::clamp(bias, 0.f, 1.f);
        for (int channel = 0; channel < static_cast<int>(clip.channels.size()); channel++)
            if (clip.channels[channel].boneIndex == boneIndex)
            {
                result.channelIndex = channel;
                break;
            }
        if (result.channelIndex < 0)
        {
            result.status = BreakdownStatus::MissingChannel;
            return result;
        }

        AnimationChannel &channel = clip.channels[result.channelIndex];
        result.previousTime = -std::numeric_limits<float>::infinity();
        result.nextTime = std::numeric_limits<float>::infinity();
        auto collectTimes = [&](const auto &keys)
        {
            for (const auto &key : keys)
            {
                if (key.time < time - kEpsilon)
                    result.previousTime = std::max(result.previousTime, key.time);
                else if (key.time > time + kEpsilon)
                    result.nextTime = std::min(result.nextTime, key.time);
            }
        };
        collectTimes(channel.positionKeys);
        collectTimes(channel.rotationKeys);
        collectTimes(channel.scaleKeys);
        if (!std::isfinite(result.previousTime) || !std::isfinite(result.nextTime))
        {
            result.status = BreakdownStatus::MissingSurroundingKeys;
            return result;
        }

        vec3 previousPosition, previousScale, nextPosition, nextScale;
        quat previousRotation, nextRotation;
        AnimationChannel sampledChannel = channel;
        const auto byTime = [](const auto &left, const auto &right)
        { return left.time < right.time; };
        std::sort(sampledChannel.positionKeys.begin(), sampledChannel.positionKeys.end(), byTime);
        std::sort(sampledChannel.rotationKeys.begin(), sampledChannel.rotationKeys.end(), byTime);
        std::sort(sampledChannel.scaleKeys.begin(), sampledChannel.scaleKeys.end(), byTime);
        AnimationEvaluator::SampleChannel(sampledChannel, skeleton.bones[boneIndex], result.previousTime,
                                          previousPosition, previousRotation, previousScale);
        AnimationEvaluator::SampleChannel(sampledChannel, skeleton.bones[boneIndex], result.nextTime,
                                          nextPosition, nextRotation, nextScale);
        if (!Finite(previousPosition) || !Finite(previousRotation) || !Finite(previousScale) || !Finite(nextPosition) ||
            !Finite(nextRotation) || !Finite(nextScale))
        {
            result.status = BreakdownStatus::NonFiniteChannel;
            return result;
        }

        std::sort(channel.positionKeys.begin(), channel.positionKeys.end(), byTime);
        std::sort(channel.rotationKeys.begin(), channel.rotationKeys.end(), byTime);
        std::sort(channel.scaleKeys.begin(), channel.scaleKeys.end(), byTime);
        WriteKey(channel.positionKeys, time, glm::mix(previousPosition, nextPosition, result.appliedBias));
        WriteKey(channel.rotationKeys, time, ShortestSlerp(previousRotation, nextRotation, result.appliedBias));
        WriteKey(channel.scaleKeys, time, glm::mix(previousScale, nextScale, result.appliedBias));
        result.keysWritten = 3;
        return result;
    }

    MotionTrailResult SampleMotionTrail(const AnimationClip &clip,
                                        const Skeleton &skeleton,
                                        int boneIndex,
                                        const MotionTrailSettings &settings)
    {
        MotionTrailResult result;
        if (boneIndex < 0 || boneIndex >= skeleton.GetBoneCount())
        {
            result.status = TrailStatus::InvalidBone;
            return result;
        }
        if (!std::isfinite(clip.duration) || clip.duration < 0.f || !std::isfinite(settings.currentTime) ||
            !std::isfinite(settings.frameStep) || settings.frameStep <= 0.f ||
            settings.previousFrames < 0 || settings.nextFrames < 0 || !Finite(settings.boneLocalPoint))
        {
            result.status = TrailStatus::InvalidSettings;
            return result;
        }

        const float current = std::clamp(settings.currentTime, 0.f, std::max(clip.duration, 0.f));
        std::vector<mat4> globals;
        result.samples.reserve(static_cast<std::size_t>(settings.previousFrames) +
                               static_cast<std::size_t>(settings.nextFrames) + 1);
        for (std::int64_t offset = -static_cast<std::int64_t>(settings.previousFrames);
             offset <= static_cast<std::int64_t>(settings.nextFrames); offset++)
        {
            const float time = current + static_cast<float>(offset) * settings.frameStep;
            if (time < 0.f || time > clip.duration)
                continue;
            if (!SampleRigGlobalPose(clip, skeleton, time, globals))
            {
                result.status = TrailStatus::PoseEvaluationFailed;
                result.samples.clear();
                return result;
            }
            MotionTrailSample sample;
            sample.frameOffset = static_cast<int>(offset);
            sample.time = time;
            sample.rigGlobalTransform = globals[boneIndex];
            sample.point = vec3(sample.rigGlobalTransform * vec4(settings.boneLocalPoint, 1.f));
            if (!Finite(sample.point))
            {
                result.status = TrailStatus::PoseEvaluationFailed;
                result.samples.clear();
                return result;
            }
            result.samples.push_back(sample);
        }
        return result;
    }
} // namespace pe::AnimationPoseTools
