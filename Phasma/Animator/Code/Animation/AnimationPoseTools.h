#pragma once

#include "Animation/AnimationTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pe::AnimationPoseTools
{
    enum class TwoBoneIkStatus : uint8_t
    {
        Success,
        NonFiniteInput,
        DegenerateUpperBone,
        DegenerateLowerBone
    };

    // Positions are global within rig space (the skeleton root transform has already been removed).
    struct TwoBoneIkInput
    {
        vec3 rootPosition = vec3(0.f);
        vec3 midPosition = vec3(0.f);
        vec3 tipPosition = vec3(0.f);
        vec3 targetPosition = vec3(0.f);
        vec3 polePosition = vec3(0.f);
    };

    struct TwoBoneIkResult
    {
        TwoBoneIkStatus status = TwoBoneIkStatus::Success;
        // Global-space deltas, applied parent-to-child. The mid bone first inherits rootGlobalDelta,
        // then receives midGlobalDelta.
        quat rootGlobalDelta = quat(1.f, 0.f, 0.f, 0.f);
        quat midGlobalDelta = quat(1.f, 0.f, 0.f, 0.f);
        vec3 solvedMidPosition = vec3(0.f);
        vec3 solvedTipPosition = vec3(0.f);
        float solvedDistance = 0.f;
        bool targetClamped = false;

        explicit operator bool() const { return status == TwoBoneIkStatus::Success; }
    };

    TwoBoneIkResult SolveTwoBoneIk(const TwoBoneIkInput &input);

    enum class PlanarSplineIkStatus : uint8_t
    {
        Success,
        NonFiniteInput,
        NotEnoughStations,
        DegenerateChain
    };

    struct PlanarSplineIkInput
    {
        std::vector<vec2> stations; // chain heads followed by the final tail, in rig space
        vec2 target = vec2(0.f);
        std::vector<float> bendInfluences; // one per segment, default 1, clamped to 0..2
        float maxBendRadians = glm::pi<float>() / 3.f;
        float bendSign = 1.f;
        float maxStretchScale = 1.5f;
        int iterations = 12;
    };

    struct PlanarSplineIkResult
    {
        PlanarSplineIkStatus status = PlanarSplineIkStatus::Success;
        std::vector<vec2> stations;
        std::vector<float> segmentAngles;
        float stretchScale = 1.f;
        float bendRadians = 0.f;
        float bendSign = 1.f;
        bool targetClamped = false;

        explicit operator bool() const { return status == PlanarSplineIkStatus::Success; }
    };

    PlanarSplineIkResult SolvePlanarSplineIk(const PlanarSplineIkInput &input);

    enum class BreakdownStatus : uint8_t
    {
        Success,
        InvalidBone,
        InvalidTime,
        MissingChannel,
        MissingSurroundingKeys,
        NonFiniteChannel
    };

    struct BreakdownResult
    {
        BreakdownStatus status = BreakdownStatus::Success;
        int channelIndex = -1;
        float previousTime = 0.f;
        float nextTime = 0.f;
        float appliedBias = 0.5f;
        std::size_t keysWritten = 0;

        explicit operator bool() const { return status == BreakdownStatus::Success; }
    };

    // Uses the nearest keyed times around time across the bone's TRS channel, samples complete poses
    // there, and writes one complete TRS breakdown. Bias 0 is the previous pose, 1 the next pose.
    BreakdownResult InsertBreakdown(AnimationClip &clip,
                                    const Skeleton &skeleton,
                                    int boneIndex,
                                    float time,
                                    float bias);

    enum class TrailStatus : uint8_t
    {
        Success,
        InvalidBone,
        InvalidSettings,
        PoseEvaluationFailed
    };

    struct MotionTrailSettings
    {
        float currentTime = 0.f; // clip ticks
        float frameStep = 1.f;   // ticks per requested frame
        int previousFrames = 2;
        int nextFrames = 2;
        vec3 boneLocalPoint = vec3(0.f); // zero samples the bone origin
    };

    struct MotionTrailSample
    {
        int frameOffset = 0;
        float time = 0.f;
        mat4 rigGlobalTransform = mat4(1.f);
        vec3 point = vec3(0.f);
    };

    struct MotionTrailResult
    {
        TrailStatus status = TrailStatus::Success;
        std::vector<MotionTrailSample> samples; // chronological, previous through current to next

        explicit operator bool() const { return status == TrailStatus::Success; }
    };

    MotionTrailResult SampleMotionTrail(const AnimationClip &clip,
                                        const Skeleton &skeleton,
                                        int boneIndex,
                                        const MotionTrailSettings &settings = {});
} // namespace pe::AnimationPoseTools
