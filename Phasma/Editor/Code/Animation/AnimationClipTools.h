#pragma once

#include "Animation/AnimationTypes.h"

#include <limits>
#include <span>

namespace pe::AnimationClipTools
{
    inline constexpr int MaxSmoothPasses = 64;

    enum class ChannelMask : uint8_t
    {
        None = 0,
        Position = 1 << 0,
        Rotation = 1 << 1,
        Scale = 1 << 2,
        All = (1 << 0) | (1 << 1) | (1 << 2)
    };

    constexpr ChannelMask operator|(ChannelMask a, ChannelMask b)
    {
        return static_cast<ChannelMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    constexpr bool HasChannel(ChannelMask mask, ChannelMask channel)
    {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(channel)) != 0;
    }

    enum class IssueType : uint8_t
    {
        QuaternionHemisphereFlip,
        LoopPoseSeam,
        LoopVelocitySeam,
        RootDrift,
        JitterSpike,
        RedundantKey
    };

    struct Issue
    {
        IssueType type = IssueType::JitterSpike;
        int boneIndex = -1;
        ChannelMask channel = ChannelMask::None;
        float time = 0.0f; // Clip ticks.
        // Position/scale units or rotation degrees; velocity diagnostics use the corresponding units per second.
        float magnitude = 0.0f;
    };

    struct AnalysisSettings
    {
        float loopPositionTolerance = 0.01f;
        float loopRotationToleranceDegrees = 1.0f;
        float loopScaleTolerance = 0.01f;
        float loopPositionVelocityTolerance = 0.1f;
        float loopRotationVelocityToleranceDegrees = 10.0f;
        float loopScaleVelocityTolerance = 0.1f;
        float rootDriftTolerance = 0.01f;
        float positionJitterVelocityDelta = 1.0f;
        float rotationJitterVelocityDeltaDegrees = 90.0f;
        float redundantPositionTolerance = 0.001f;
        float redundantRotationToleranceDegrees = 0.1f;
        float redundantScaleTolerance = 0.001f;
    };

    struct Analysis
    {
        std::vector<Issue> issues;

        bool Has(IssueType type) const;
        size_t Count(IssueType type) const;
    };

    struct SmoothSettings
    {
        float startTime = -std::numeric_limits<float>::infinity();
        float endTime = std::numeric_limits<float>::infinity();
        float strength = 0.5f;
        int passes = 1;
        ChannelMask channels = ChannelMask::All;
    };

    struct SimplifySettings
    {
        float positionTolerance = 0.001f;
        float rotationToleranceDegrees = 0.1f;
        float scaleTolerance = 0.001f;
        ChannelMask channels = ChannelMask::All;
    };

    enum class SpringEndpointMode : uint8_t
    {
        Free,
        PreserveSource,
        Cyclic
    };

    struct SpringBakeSettings
    {
        float framesPerSecond = 24.0f;
        float frameStep = 1.0f;  // Sample every N frames at framesPerSecond.
        float stiffness = 80.0f; // Orientation error gain, in 1/s^2.
        float damping = 12.0f;   // Relative angular velocity gain, in 1/s.
        float response = 1.0f;   // How strongly source angular velocity is inherited.
        float drag = 0.02f;      // Parent acceleration inertia, increasing toward the chain tip.
        SpringEndpointMode endpointMode = SpringEndpointMode::PreserveSource;
        int cyclicWarmupCycles = 2;
        // Clip ticks. The whole clip by default; the Timeline passes its interval. A partial range keeps every
        // key outside it and cannot be Cyclic.
        float startTime = 0.0f;
        float endTime = -1.0f; // < 0 = clip end
    };

    enum class SpringBakeStatus : uint8_t
    {
        Success,
        EmptyChain,
        InvalidClipTiming,
        InvalidSettings,
        InvalidBoneIndex,
        NonContiguousChain
    };

    struct SpringBakeResult
    {
        SpringBakeStatus status = SpringBakeStatus::Success;
        size_t bonesBaked = 0;
        size_t keysWritten = 0;
        size_t sampleCount = 0;
        float sampleStepTicks = 0.0f;
        float maxAngularLagDegrees = 0.0f;

        explicit operator bool() const { return status == SpringBakeStatus::Success; }
    };

    enum class WorldDriftStatus : uint8_t
    {
        Success,
        InvalidRange,
        InvalidBoneIndex,
        InvalidCompensationBone,
        NonInvertibleParentTransform,
        InvalidAnimationData
    };

    struct WorldDriftResult
    {
        WorldDriftStatus status = WorldDriftStatus::Success;
        int boneIndex = -1;
        int compensationBoneIndex = -1;
        size_t sampleCount = 0;
        size_t keysWritten = 0;
        float maxDrift = 0.0f;
        float maxRemainingDrift = 0.0f;

        explicit operator bool() const { return status == WorldDriftStatus::Success; }
    };

    Analysis Analyze(const AnimationClip &clip,
                     const Skeleton &skeleton,
                     const AnalysisSettings &settings = {});

    // Returns the number of quaternion keys negated into the preceding key's hemisphere.
    size_t FixQuaternionHemisphereFlips(AnimationClip &clip);

    // Writes the pose at time zero to clip.duration. Existing end keys are replaced; missing end keys are inserted.
    size_t MakeCyclic(AnimationClip &clip, ChannelMask channels = ChannelMask::All);

    enum class TweenMode : uint8_t
    {
        // Key what the clip already plays at every interior frame: existing breakdowns survive, the ends
        // are never written. This is the Tween button.
        SampleClip,
        // Drop the interior, pin both ends to the pose shown there and re-key the span from that pair.
        // This is interval mode, where a new extreme has to push the in-betweens around.
        RebuildFromEnds
    };

    // Keys the frames strictly between startTime and endTime on the clip's own frame grid (so an unsnapped
    // interval still lands on whole frames). Baked keys are Linear, or Stepped where the sampled segment was
    // stepped: the samples already carry the easing, and a Smooth key would ease an eased curve again.
    // Channels with no keys of a kind are left alone - tweening never invents a curve a bone does not have.
    size_t TweenInterval(AnimationClip &clip,
                         float startTime,
                         float endTime,
                         float stepTicks,
                         TweenMode mode = TweenMode::SampleClip,
                         std::span<const int> boneIndices = {},
                         ChannelMask channels = ChannelMask::All);

    // Replaces the interval's interior on one bone's Location curve with a ballistic arc: the horizontal
    // components keep the straight line between the two ends while rig Y follows gravity from the launch
    // velocity those ends imply. Both ends are keyed with the values the curve already shows there, so the
    // segments outside the interval keep playing what they played. Unlike TweenInterval this DOES create the
    // curve when the bone has none - the arc is the curve being authored, not an accidental constant - and
    // restPosition then stands in for the missing keys the way the evaluator's bind fallback does. Translation
    // bones only: the caller passes a root, or a bone that already owns a Location curve (mocap hips under a
    // keyless control root), and never an arbitrary child - posing refuses to give those a Location curve.
    size_t BallisticInterval(AnimationClip &clip,
                             int boneIndex,
                             const vec3 &restPosition,
                             float startTime,
                             float endTime,
                             float stepTicks,
                             float gravity = 9.81f);

    // Full-body companion to BallisticInterval: the same arc, but for the body's centre of mass. Every interior
    // frame samples the source pose, weighs each bone's mass centre (a rig-space rest centre carried by the posed
    // bone; masses from the caller - capsule volumes in the editor) and re-keys the root's Location so the centre
    // of mass, not the root, follows the arc between where it sits at the two ends: tucked limbs drop the root, a
    // heavy prop swung forward pulls the body after it. Only the mass under the moved bone counts - a control root
    // above the hips or a separate prop root is not part of the thrown body. Root rule, channel creation and end
    // handling as BallisticInterval. Airborne spans only - on the ground the feet move with the root. Angular
    // momentum too: the root turns against the limbs so the thrown body's momentum about its centre stays what
    // it was, with the net turn removed as a ramp so both ends keep their rotation (a uniform swing shows
    // nothing; a swing that starts and stops turns the body where it happens). Rotation keys are written on the
    // root only when the counter-turn is non-zero; a uniform swing leaves the rotation curve alone.
    // ponytail: point masses and a scalar inertia about the momentum axis; the full tensor is the upgrade.
    size_t BallisticBodyInterval(AnimationClip &clip,
                                 const Skeleton &skeleton,
                                 int rootBone,
                                 const vec3 &restPosition,
                                 std::span<const float> boneMasses,
                                 std::span<const vec3> restCentres,
                                 float startTime,
                                 float endTime,
                                 float stepTicks,
                                 float gravity = 9.81f);

    // Smooths interior selected keys and preserves the first and last selected key on every curve.
    size_t Smooth(AnimationClip &clip,
                  const SmoothSettings &settings,
                  std::span<const int> boneIndices = {});

    // Removes keys that remain within tolerance of the curve spanning their kept neighbors.
    size_t Simplify(AnimationClip &clip,
                    const SimplifySettings &settings = {},
                    std::span<const int> boneIndices = {});

    // Non-wrapped offsets clamp to the clip range. Duplicate times caused by clamp/wrap keep the later source key.
    size_t OffsetBoneKeyTimes(AnimationClip &clip,
                              int boneIndex,
                              float deltaTime,
                              bool wrap,
                              ChannelMask channels = ChannelMask::All);

    // Samples sourceTime before writing, reflects global rig-space transforms across X, then solves target local TRS.
    // In-place pastes are safe. Conventional .L/.R channels swap; center channels optionally mirror onto themselves.
    size_t PasteMirroredPose(AnimationClip &clip,
                             const Skeleton &skeleton,
                             float sourceTime,
                             float targetTime,
                             std::span<const int> boneIndices = {},
                             bool includeCenterBones = true,
                             ChannelMask channels = ChannelMask::All);

    // Bakes a directly parented, root-to-tip secondary chain as ordinary local rotation keys. The source clip is
    // sampled before any channel is replaced. Cyclic mode warms the spring to steady state and closes the pose seam.
    // Samples sit on the frame grid the settings describe; over a partial range the chain starts on the source
    // pose at the range start with no angular velocity, the interior is re-keyed and both ends keep the source.
    SpringBakeResult BakeSecondarySpring(AnimationClip &clip,
                                         const Skeleton &skeleton,
                                         std::span<const int> orderedBoneIndices,
                                         const SpringBakeSettings &settings = {});

    // Measures world-position displacement from the selected bone's pose at startTime.
    WorldDriftResult AnalyzeWorldPositionDrift(const AnimationClip &clip,
                                               const Skeleton &skeleton,
                                               int boneIndex,
                                               float startTime,
                                               float endTime,
                                               float sampleStepTicks);

    // Keeps the selected bone at its startTime world position by baking local position compensation into an existing
    // position-capable ancestor (or the hierarchy root). compensationBoneIndex < 0 selects that ancestor automatically.
    WorldDriftResult StabilizeWorldPosition(AnimationClip &clip,
                                            const Skeleton &skeleton,
                                            int boneIndex,
                                            float startTime,
                                            float endTime,
                                            float sampleStepTicks,
                                            int compensationBoneIndex = -1);
} // namespace pe::AnimationClipTools
