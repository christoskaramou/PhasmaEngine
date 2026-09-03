#include "AnimationClipTools.h"

#include "Animation/AnimationEvaluator.h"

namespace pe::AnimationClipTools
{
    namespace
    {
        constexpr float kEpsilon = 0.00001f;
        constexpr float kMaxSpringFramesPerSecond = 480.0f;
        constexpr float kMaxSpringFrameStep = 1000.0f;
        constexpr float kMaxSpringStiffness = 1000.0f;
        constexpr float kMaxSpringDamping = 200.0f;
        constexpr float kMaxSpringResponse = 10.0f;
        constexpr float kMaxSpringDrag = 10.0f;
        constexpr double kMaxSpringSubsteps = 4096.0;
        constexpr double kMaxSpringWorkItems = 10000000.0;
        constexpr size_t kMaxSpringSamples = 100000;
        constexpr size_t kMaxSimplifyComparisons = 5000000;
        constexpr size_t kMaxTweenSamples = 4096;

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
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(value[column][row]))
                        return false;
            return true;
        }

        template <typename Key>
        bool FiniteKeys(const std::vector<Key> &keys)
        {
            return std::all_of(keys.begin(), keys.end(), [](const Key &key)
                               { return std::isfinite(key.time) && Finite(key.value); });
        }

        quat Negated(const quat &q)
        {
            return quat(-q.w, -q.x, -q.y, -q.z);
        }

        quat SafeNormalized(const quat &q)
        {
            const float lengthSquared = glm::dot(q, q);
            return std::isfinite(lengthSquared) && lengthSquared > kEpsilon ? q / std::sqrt(lengthSquared)
                                                                            : quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        quat SameHemisphere(const quat &reference, const quat &value)
        {
            const quat normalized = SafeNormalized(value);
            return glm::dot(SafeNormalized(reference), normalized) < 0.0f ? Negated(normalized) : normalized;
        }

        quat ShortestSlerp(const quat &a, const quat &b, float factor)
        {
            const quat normalizedA = SafeNormalized(a);
            return SafeNormalized(glm::slerp(normalizedA, SameHemisphere(normalizedA, b), factor));
        }

        float RotationDistanceDegrees(const quat &a, const quat &b)
        {
            const float dot = std::clamp(std::abs(glm::dot(SafeNormalized(a), SafeNormalized(b))), 0.0f, 1.0f);
            return glm::degrees(2.0f * std::acos(dot));
        }

        vec3 AngularVelocityDegrees(const quat &from, const quat &to, float seconds)
        {
            if (seconds <= kEpsilon)
                return vec3(0.0f);

            quat delta = SafeNormalized(glm::conjugate(SafeNormalized(from)) * SafeNormalized(to));
            if (delta.w < 0.0f)
                delta = Negated(delta);

            const float angle = 2.0f * std::acos(std::clamp(delta.w, -1.0f, 1.0f));
            const float sinHalf = std::sqrt(std::max(1.0f - delta.w * delta.w, 0.0f));
            if (sinHalf <= kEpsilon)
                return vec3(0.0f);
            return vec3(delta.x, delta.y, delta.z) / sinHalf * (glm::degrees(angle) / seconds);
        }

        float CurveFactor(AnimationInterpolation interpolation, float factor)
        {
            factor = std::clamp(factor, 0.0f, 1.0f);
            if (interpolation == AnimationInterpolation::Smooth)
                return factor * factor * (3.0f - 2.0f * factor);
            if (interpolation == AnimationInterpolation::Stepped)
                return 0.0f;
            return factor;
        }

        float SecondsBetween(float from, float to, float ticksPerSecond)
        {
            return (to - from) / std::max(ticksPerSecond, kEpsilon);
        }

        bool BoneSelected(std::span<const int> boneIndices, int boneIndex)
        {
            return boneIndices.empty() || std::find(boneIndices.begin(), boneIndices.end(), boneIndex) != boneIndices.end();
        }

        template <typename Key, typename Interpolate, typename Error>
        std::vector<bool> BuildKeepMask(const std::vector<Key> &keys,
                                        float tolerance,
                                        Interpolate interpolate,
                                        Error error,
                                        size_t &comparisonsRemaining)
        {
            std::vector<bool> keep(keys.size(), false);
            if (keys.empty())
                return keep;
            if (!std::isfinite(tolerance) || tolerance < 0.0f)
            {
                std::fill(keep.begin(), keep.end(), true);
                return keep;
            }
            keep.front() = true;
            keep.back() = true;
            if (keys.size() <= 2)
                return keep;

            std::vector<size_t> anchors = {0, keys.size() - 1};
            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                if (keys[i].interpolation == AnimationInterpolation::Stepped ||
                    keys[i + 1].time - keys[i].time <= kEpsilon)
                {
                    anchors.push_back(i);
                    anchors.push_back(i + 1);
                }
                if (i > 0 && keys[i - 1].interpolation != keys[i].interpolation)
                    anchors.push_back(i);
            }
            std::sort(anchors.begin(), anchors.end());
            anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
            for (size_t anchor : anchors)
                keep[anchor] = true;

            std::vector<std::pair<size_t, size_t>> segments;
            segments.reserve(anchors.size());
            for (size_t i = 1; i < anchors.size(); ++i)
                segments.emplace_back(anchors[i - 1], anchors[i]);

            while (!segments.empty())
            {
                const auto [first, last] = segments.back();
                segments.pop_back();
                if (last <= first + 1)
                    continue;
                const float duration = keys[last].time - keys[first].time;
                if (duration <= kEpsilon)
                    continue;

                const size_t segmentComparisons = last - first - 1;
                if (segmentComparisons > comparisonsRemaining)
                {
                    for (size_t i = first; i <= last; ++i)
                        keep[i] = true;
                    for (const auto &[pendingFirst, pendingLast] : segments)
                        for (size_t i = pendingFirst; i <= pendingLast; ++i)
                            keep[i] = true;
                    break;
                }
                comparisonsRemaining -= segmentComparisons;

                size_t worst = first;
                float worstError = -1.0f;
                for (size_t i = first + 1; i < last; ++i)
                {
                    const float factor = CurveFactor(keys[first].interpolation,
                                                     (keys[i].time - keys[first].time) / duration);
                    const float currentError = error(keys[i].value,
                                                     interpolate(keys[first].value, keys[last].value, factor));
                    if (currentError > worstError)
                    {
                        worstError = currentError;
                        worst = i;
                    }
                }
                if (worstError > tolerance)
                {
                    keep[worst] = true;
                    segments.emplace_back(first, worst);
                    segments.emplace_back(worst, last);
                }
            }
            return keep;
        }

        template <typename Key>
        size_t ApplyKeepMask(std::vector<Key> &keys, const std::vector<bool> &keep)
        {
            const size_t oldSize = keys.size();
            size_t write = 0;
            for (size_t i = 0; i < oldSize; ++i)
                if (keep[i])
                    keys[write++] = keys[i];
            keys.resize(write);
            return oldSize - write;
        }

        template <typename Key, typename Interpolate, typename Error>
        void AddRedundantIssues(const std::vector<Key> &keys,
                                const std::vector<bool> &keep,
                                int boneIndex,
                                ChannelMask channel,
                                Interpolate interpolate,
                                Error error,
                                Analysis &analysis)
        {
            for (size_t i = 1; i + 1 < keys.size(); ++i)
            {
                if (keep[i])
                    continue;
                size_t previous = i - 1;
                while (!keep[previous])
                    --previous;
                size_t next = i + 1;
                while (!keep[next])
                    ++next;
                const float duration = keys[next].time - keys[previous].time;
                const float factor = duration > kEpsilon
                                         ? CurveFactor(keys[previous].interpolation,
                                                       (keys[i].time - keys[previous].time) / duration)
                                         : 0.0f;
                analysis.issues.push_back({IssueType::RedundantKey,
                                           boneIndex,
                                           channel,
                                           keys[i].time,
                                           error(keys[i].value,
                                                 interpolate(keys[previous].value, keys[next].value, factor))});
            }
        }

        template <typename Key>
        AnimationInterpolation InterpolationAt(const std::vector<Key> &keys, float time)
        {
            if (keys.empty())
                return AnimationInterpolation::Linear;
            const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const Key &key)
                                               { return t < key.time; });
            return next == keys.begin() ? next->interpolation : std::prev(next)->interpolation;
        }

        template <typename T>
        size_t UpsertKey(std::vector<AnimationKey<T>> &keys,
                         float time,
                         const T &value,
                         AnimationInterpolation interpolation)
        {
            if (!std::isfinite(time) || !Finite(value))
                return 0;
            auto it = std::lower_bound(keys.begin(), keys.end(), time - kEpsilon, [](const AnimationKey<T> &key, float t)
                                       { return key.time < t; });
            if (it != keys.end() && std::abs(it->time - time) <= kEpsilon)
            {
                it->time = time;
                it->value = value;
                it->interpolation = interpolation;
            }
            else
            {
                it = std::lower_bound(keys.begin(), keys.end(), time, [](const AnimationKey<T> &key, float t)
                                      { return key.time < t; });
                keys.insert(it, {time, value, interpolation});
            }
            return 1;
        }

        template <typename Key>
        size_t OffsetTimes(std::vector<Key> &keys, float deltaTime, float duration, bool wrap)
        {
            if (keys.empty())
                return 0;
            const size_t touched = keys.size();
            for (Key &key : keys)
            {
                double time = static_cast<double>(key.time) + deltaTime;
                if (wrap)
                {
                    time = std::fmod(time, duration);
                    if (time < 0.0f)
                        time += duration;
                }
                else
                {
                    time = std::clamp(time, 0.0, static_cast<double>(duration));
                }
                key.time = static_cast<float>(time);
            }

            std::stable_sort(keys.begin(), keys.end(), [](const Key &a, const Key &b)
                             { return a.time < b.time; });
            size_t write = 0;
            for (const Key &key : keys)
            {
                if (write > 0 && std::abs(keys[write - 1].time - key.time) <= kEpsilon)
                    keys[write - 1] = key;
                else
                    keys[write++] = key;
            }
            keys.resize(write);
            return touched;
        }

        template <typename Key>
        size_t SmoothVectors(std::vector<Key> &keys, const SmoothSettings &settings)
        {
            auto first = std::lower_bound(keys.begin(), keys.end(), settings.startTime, [](const Key &key, float time)
                                          { return key.time < time; });
            auto afterLast = std::upper_bound(keys.begin(), keys.end(), settings.endTime, [](float time, const Key &key)
                                              { return time < key.time; });
            if (std::distance(first, afterLast) < 3)
                return 0;

            const size_t firstIndex = static_cast<size_t>(std::distance(keys.begin(), first));
            const size_t lastIndex = static_cast<size_t>(std::distance(keys.begin(), afterLast) - 1);
            size_t writes = 0;
            for (int pass = 0; pass < settings.passes; ++pass)
            {
                const std::vector<Key> source = keys;
                for (size_t i = firstIndex + 1; i < lastIndex; ++i)
                {
                    const float duration = source[i + 1].time - source[i - 1].time;
                    if (duration <= kEpsilon)
                        continue;
                    const float factor = (source[i].time - source[i - 1].time) / duration;
                    const auto expected = glm::mix(source[i - 1].value, source[i + 1].value, factor);
                    keys[i].value = glm::mix(source[i].value, expected, settings.strength);
                    ++writes;
                }
            }
            return writes;
        }

        size_t SmoothRotations(std::vector<RotationKey> &keys, const SmoothSettings &settings)
        {
            auto first = std::lower_bound(keys.begin(), keys.end(), settings.startTime,
                                          [](const RotationKey &key, float time)
                                          { return key.time < time; });
            auto afterLast = std::upper_bound(keys.begin(), keys.end(), settings.endTime,
                                              [](float time, const RotationKey &key)
                                              { return time < key.time; });
            if (std::distance(first, afterLast) < 3)
                return 0;

            const size_t firstIndex = static_cast<size_t>(std::distance(keys.begin(), first));
            const size_t lastIndex = static_cast<size_t>(std::distance(keys.begin(), afterLast) - 1);
            size_t writes = 0;
            for (int pass = 0; pass < settings.passes; ++pass)
            {
                const std::vector<RotationKey> source = keys;
                for (size_t i = firstIndex + 1; i < lastIndex; ++i)
                {
                    const float duration = source[i + 1].time - source[i - 1].time;
                    if (duration <= kEpsilon)
                        continue;
                    const float factor = (source[i].time - source[i - 1].time) / duration;
                    const quat expected = ShortestSlerp(source[i - 1].value, source[i + 1].value, factor);
                    keys[i].value = ShortestSlerp(source[i].value, expected, settings.strength);
                    ++writes;
                }
            }
            return writes;
        }

        int FindBone(const Skeleton &skeleton, const std::string &name)
        {
            const int mapped = skeleton.GetBoneIndex(name);
            if (mapped >= 0)
                return mapped;
            for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
                if (skeleton.bones[i].name == name)
                    return i;
            return -1;
        }

        int MirroredBone(const Skeleton &skeleton, int boneIndex, bool includeCenterBones)
        {
            if (boneIndex < 0 || boneIndex >= static_cast<int>(skeleton.bones.size()))
                return -1;
            std::string name = skeleton.bones[boneIndex].name;
            if (name.ends_with(".L"))
                name.replace(name.size() - 2, 2, ".R");
            else if (name.ends_with(".R"))
                name.replace(name.size() - 2, 2, ".L");
            else
                return includeCenterBones ? boneIndex : -1;
            return FindBone(skeleton, name);
        }

        AnimationChannel &FindOrAddChannel(AnimationClip &clip, int boneIndex)
        {
            const auto it = std::find_if(clip.channels.begin(), clip.channels.end(), [boneIndex](const AnimationChannel &channel)
                                         { return channel.boneIndex == boneIndex; });
            if (it != clip.channels.end())
                return *it;
            clip.channels.push_back({});
            clip.channels.back().boneIndex = boneIndex;
            return clip.channels.back();
        }

        quat MatrixRotation(const mat4 &matrix)
        {
            vec3 x = vec3(matrix[0]);
            vec3 y = vec3(matrix[1]);
            vec3 z = vec3(matrix[2]);
            const float xLength = glm::length(x);
            const float yLength = glm::length(y);
            const float zLength = glm::length(z);
            if (xLength <= kEpsilon || yLength <= kEpsilon || zLength <= kEpsilon)
                return quat(1.0f, 0.0f, 0.0f, 0.0f);
            x /= xLength;
            y /= yLength;
            z /= zLength;
            return SafeNormalized(glm::quat_cast(mat3(x, y, z)));
        }

        vec3 MatrixScale(const mat4 &matrix)
        {
            return vec3(glm::length(vec3(matrix[0])),
                        glm::length(vec3(matrix[1])),
                        glm::length(vec3(matrix[2])));
        }

        mat4 ComposeTransform(const vec3 &position, const quat &rotation, const vec3 &scale)
        {
            return glm::translate(mat4(1.0f), position) * glm::mat4_cast(SafeNormalized(rotation)) *
                   glm::scale(mat4(1.0f), scale);
        }

        void SampleGlobalTransforms(const AnimationClip &clip,
                                    const Skeleton &skeleton,
                                    float time,
                                    std::vector<mat4> &globalTransforms)
        {
            std::vector<mat4> jointMatrices;
            AnimationEvaluator::EvaluatePose(clip, skeleton, time, jointMatrices);
            globalTransforms.resize(jointMatrices.size());
            for (size_t i = 0; i < jointMatrices.size(); ++i)
                globalTransforms[i] = jointMatrices[i] * glm::inverse(skeleton.bones[i].offsetMatrix);
        }

        void SampleGlobalRotations(const AnimationClip &clip,
                                   const Skeleton &skeleton,
                                   float time,
                                   std::vector<quat> &globalRotations)
        {
            std::vector<mat4> globalTransforms;
            SampleGlobalTransforms(clip, skeleton, time, globalTransforms);
            globalRotations.resize(globalTransforms.size());
            for (size_t i = 0; i < globalTransforms.size(); ++i)
                globalRotations[i] = MatrixRotation(globalTransforms[i]);
        }

        const AnimationChannel *FindChannel(const AnimationClip &clip, int boneIndex)
        {
            const auto channel = std::find_if(clip.channels.begin(), clip.channels.end(), [boneIndex](const AnimationChannel &candidate)
                                              { return candidate.boneIndex == boneIndex; });
            return channel == clip.channels.end() ? nullptr : &*channel;
        }

        quat SampleLocalRotation(const AnimationClip &clip, const Skeleton &skeleton, int boneIndex, float time)
        {
            vec3 position, scale;
            quat rotation;
            if (const AnimationChannel *channel = FindChannel(clip, boneIndex))
                AnimationEvaluator::SampleChannel(*channel, skeleton.bones[boneIndex], time, position, rotation, scale);
            else
                AnimationEvaluator::BindPose(skeleton.bones[boneIndex], position, rotation, scale);
            return SafeNormalized(rotation);
        }

        vec3 WorldRotationVector(const quat &from, const quat &to)
        {
            quat delta = SafeNormalized(SafeNormalized(to) * glm::conjugate(SafeNormalized(from)));
            if (delta.w < 0.0f)
                delta = Negated(delta);
            const float angle = 2.0f * std::acos(std::clamp(delta.w, -1.0f, 1.0f));
            const float sinHalf = std::sqrt(std::max(1.0f - delta.w * delta.w, 0.0f));
            return sinHalf > kEpsilon ? vec3(delta.x, delta.y, delta.z) / sinHalf * angle : vec3(0.0f);
        }

        quat ApplyWorldRotation(const quat &rotation, const vec3 &rotationVector)
        {
            const float angle = glm::length(rotationVector);
            if (angle <= kEpsilon)
                return rotation;
            return SafeNormalized(glm::angleAxis(angle, rotationVector / angle) * rotation);
        }

        template <typename Key>
        bool HasKeyAt(const std::vector<Key> &keys, float time, float tolerance)
        {
            return std::any_of(keys.begin(), keys.end(), [time, tolerance](const Key &key)
                               { return std::abs(key.time - time) <= tolerance; });
        }

        // One curve's share of a tween. SampleClip keys what the curve already plays at each interior
        // frame; RebuildFromEnds pins the two ends, drops the interior and re-keys it from that pair.
        template <typename Key, typename Sample, typename Mix>
        size_t TweenKeys(std::vector<Key> &keys, float startTime, float endTime, float stepTicks, TweenMode mode,
                         Sample sample, Mix mix)
        {
            if (keys.empty())
                return 0;
            using Value = decltype(sample(keys, startTime));
            // Ticks per frame can be large (assimp clips run at 1000 ticks/s), so the "is this the same
            // key" window follows the frame step instead of a fixed tick epsilon.
            const float tolerance = std::max(kEpsilon, stepTicks * 0.001f);
            const float span = endTime - startTime;
            const Value startValue = sample(keys, startTime);
            const Value endValue = sample(keys, endTime);
            const AnimationInterpolation blend = InterpolationAt(keys, startTime);

            // Every sample is taken before anything is written: a key inserted mid-loop would otherwise
            // change the curve the later frames sample. Interior times sit on the clip's own frame grid, so
            // an unsnapped or fractional interval still keys whole frames.
            std::vector<std::pair<float, Value>> samples;
            std::vector<AnimationInterpolation> modes;
            const float first = std::ceil((startTime + tolerance) / stepTicks) * stepTicks;
            for (size_t step = 0; step < kMaxTweenSamples; ++step)
            {
                const float time = first + stepTicks * static_cast<float>(step);
                if (time >= endTime - tolerance)
                    break;
                samples.push_back({time,
                                   mode == TweenMode::SampleClip
                                       ? sample(keys, time)
                                       : mix(startValue, endValue, CurveFactor(blend, (time - startTime) / span))});
                // Baked keys play back straight - the samples already carry the easing, and a Smooth key
                // would make the evaluator ease an eased curve a second time. A stepped segment keeps its
                // hold so it still jumps on its own key.
                const AnimationInterpolation source = mode == TweenMode::SampleClip ? InterpolationAt(keys, time) : blend;
                modes.push_back(source == AnimationInterpolation::Stepped ? AnimationInterpolation::Stepped
                                                                          : AnimationInterpolation::Linear);
            }
            if (samples.empty())
                return 0;

            size_t writes = 0;
            if (mode == TweenMode::RebuildFromEnds)
            {
                // The ends anchor the rebuilt halves, so pin them to the pose the curve already shows there
                // (the value does not change) before the interior goes.
                if (!HasKeyAt(keys, startTime, tolerance))
                    writes += UpsertKey(keys, startTime, startValue, modes.front());
                if (!HasKeyAt(keys, endTime, tolerance))
                    writes += UpsertKey(keys, endTime, endValue, InterpolationAt(keys, endTime));
                std::erase_if(keys, [&](const Key &key)
                              { return key.time > startTime + tolerance && key.time < endTime - tolerance; });
            }
            for (size_t i = 0; i < samples.size(); i++)
                writes += UpsertKey(keys, samples[i].first, samples[i].second, modes[i]);
            return writes;
        }
    } // namespace

    bool Analysis::Has(IssueType type) const
    {
        return std::any_of(issues.begin(), issues.end(), [type](const Issue &issue)
                           { return issue.type == type; });
    }

    size_t Analysis::Count(IssueType type) const
    {
        return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [type](const Issue &issue)
                                                 { return issue.type == type; }));
    }

    Analysis Analyze(const AnimationClip &clip, const Skeleton &skeleton, const AnalysisSettings &settings)
    {
        Analysis analysis;
        const float ticksPerSecond = std::max(clip.ticksPerSecond, kEpsilon);
        const auto mixVector = [](const vec3 &a, const vec3 &b, float factor)
        { return glm::mix(a, b, factor); };
        const auto vectorError = [](const vec3 &a, const vec3 &b)
        { return glm::length(a - b); };
        const auto mixRotation = [](const quat &a, const quat &b, float factor)
        { return ShortestSlerp(a, b, factor); };
        size_t simplifyComparisonsRemaining = kMaxSimplifyComparisons;

        for (const AnimationChannel &channel : clip.channels)
        {
            const int boneIndex = channel.boneIndex;
            if (!channel.rotationKeys.empty())
            {
                quat previous = SafeNormalized(channel.rotationKeys.front().value);
                for (size_t i = 1; i < channel.rotationKeys.size(); ++i)
                {
                    quat current = SafeNormalized(channel.rotationKeys[i].value);
                    if (glm::dot(previous, current) < 0.0f)
                    {
                        analysis.issues.push_back({IssueType::QuaternionHemisphereFlip,
                                                   boneIndex,
                                                   ChannelMask::Rotation,
                                                   channel.rotationKeys[i].time,
                                                   1.0f});
                        current = Negated(current);
                    }
                    previous = current;
                }
            }

            if (channel.positionKeys.size() >= 2)
            {
                const auto &first = channel.positionKeys.front();
                const auto &second = channel.positionKeys[1];
                const auto &previous = channel.positionKeys[channel.positionKeys.size() - 2];
                const auto &last = channel.positionKeys.back();
                const float poseSeam = glm::length(last.value - first.value);
                if (poseSeam > settings.loopPositionTolerance)
                    analysis.issues.push_back({IssueType::LoopPoseSeam,
                                               boneIndex,
                                               ChannelMask::Position,
                                               clip.duration,
                                               poseSeam});

                const float firstSeconds = SecondsBetween(first.time, second.time, ticksPerSecond);
                const float lastSeconds = SecondsBetween(previous.time, last.time, ticksPerSecond);
                if (firstSeconds > kEpsilon && lastSeconds > kEpsilon)
                {
                    const float velocitySeam = glm::length((second.value - first.value) / firstSeconds -
                                                           (last.value - previous.value) / lastSeconds);
                    if (velocitySeam > settings.loopPositionVelocityTolerance)
                        analysis.issues.push_back({IssueType::LoopVelocitySeam,
                                                   boneIndex,
                                                   ChannelMask::Position,
                                                   clip.duration,
                                                   velocitySeam});
                }

                if (boneIndex >= 0 && boneIndex < static_cast<int>(skeleton.bones.size()) &&
                    skeleton.bones[boneIndex].parentIndex < 0 && poseSeam > settings.rootDriftTolerance)
                    analysis.issues.push_back({IssueType::RootDrift,
                                               boneIndex,
                                               ChannelMask::Position,
                                               clip.duration,
                                               poseSeam});

                for (size_t i = 1; i + 1 < channel.positionKeys.size(); ++i)
                {
                    const float before = SecondsBetween(channel.positionKeys[i - 1].time,
                                                        channel.positionKeys[i].time,
                                                        ticksPerSecond);
                    const float after = SecondsBetween(channel.positionKeys[i].time,
                                                       channel.positionKeys[i + 1].time,
                                                       ticksPerSecond);
                    if (before <= kEpsilon || after <= kEpsilon)
                        continue;
                    const float spike = glm::length((channel.positionKeys[i + 1].value - channel.positionKeys[i].value) /
                                                        after -
                                                    (channel.positionKeys[i].value - channel.positionKeys[i - 1].value) /
                                                        before);
                    if (spike > settings.positionJitterVelocityDelta)
                        analysis.issues.push_back({IssueType::JitterSpike,
                                                   boneIndex,
                                                   ChannelMask::Position,
                                                   channel.positionKeys[i].time,
                                                   spike});
                }

                const auto keep = BuildKeepMask(channel.positionKeys,
                                                settings.redundantPositionTolerance,
                                                mixVector,
                                                vectorError,
                                                simplifyComparisonsRemaining);
                AddRedundantIssues(channel.positionKeys,
                                   keep,
                                   boneIndex,
                                   ChannelMask::Position,
                                   mixVector,
                                   vectorError,
                                   analysis);
            }

            if (channel.rotationKeys.size() >= 2)
            {
                const auto &first = channel.rotationKeys.front();
                const auto &second = channel.rotationKeys[1];
                const auto &previous = channel.rotationKeys[channel.rotationKeys.size() - 2];
                const auto &last = channel.rotationKeys.back();
                const float poseSeam = RotationDistanceDegrees(last.value, first.value);
                if (poseSeam > settings.loopRotationToleranceDegrees)
                    analysis.issues.push_back({IssueType::LoopPoseSeam,
                                               boneIndex,
                                               ChannelMask::Rotation,
                                               clip.duration,
                                               poseSeam});

                const float firstSeconds = SecondsBetween(first.time, second.time, ticksPerSecond);
                const float lastSeconds = SecondsBetween(previous.time, last.time, ticksPerSecond);
                if (firstSeconds > kEpsilon && lastSeconds > kEpsilon)
                {
                    const float velocitySeam = glm::length(AngularVelocityDegrees(first.value, second.value, firstSeconds) -
                                                           AngularVelocityDegrees(previous.value, last.value, lastSeconds));
                    if (velocitySeam > settings.loopRotationVelocityToleranceDegrees)
                        analysis.issues.push_back({IssueType::LoopVelocitySeam,
                                                   boneIndex,
                                                   ChannelMask::Rotation,
                                                   clip.duration,
                                                   velocitySeam});
                }

                for (size_t i = 1; i + 1 < channel.rotationKeys.size(); ++i)
                {
                    const float before = SecondsBetween(channel.rotationKeys[i - 1].time,
                                                        channel.rotationKeys[i].time,
                                                        ticksPerSecond);
                    const float after = SecondsBetween(channel.rotationKeys[i].time,
                                                       channel.rotationKeys[i + 1].time,
                                                       ticksPerSecond);
                    if (before <= kEpsilon || after <= kEpsilon)
                        continue;
                    const float spike = glm::length(AngularVelocityDegrees(channel.rotationKeys[i - 1].value,
                                                                           channel.rotationKeys[i].value,
                                                                           before) -
                                                    AngularVelocityDegrees(channel.rotationKeys[i].value,
                                                                           channel.rotationKeys[i + 1].value,
                                                                           after));
                    if (spike > settings.rotationJitterVelocityDeltaDegrees)
                        analysis.issues.push_back({IssueType::JitterSpike,
                                                   boneIndex,
                                                   ChannelMask::Rotation,
                                                   channel.rotationKeys[i].time,
                                                   spike});
                }

                const auto keep = BuildKeepMask(channel.rotationKeys,
                                                settings.redundantRotationToleranceDegrees,
                                                mixRotation,
                                                RotationDistanceDegrees,
                                                simplifyComparisonsRemaining);
                AddRedundantIssues(channel.rotationKeys,
                                   keep,
                                   boneIndex,
                                   ChannelMask::Rotation,
                                   mixRotation,
                                   RotationDistanceDegrees,
                                   analysis);
            }

            if (channel.scaleKeys.size() >= 2)
            {
                const auto &first = channel.scaleKeys.front();
                const auto &second = channel.scaleKeys[1];
                const auto &previous = channel.scaleKeys[channel.scaleKeys.size() - 2];
                const auto &last = channel.scaleKeys.back();
                const float poseSeam = glm::length(last.value - first.value);
                if (poseSeam > settings.loopScaleTolerance)
                    analysis.issues.push_back({IssueType::LoopPoseSeam,
                                               boneIndex,
                                               ChannelMask::Scale,
                                               clip.duration,
                                               poseSeam});

                const float firstSeconds = SecondsBetween(first.time, second.time, ticksPerSecond);
                const float lastSeconds = SecondsBetween(previous.time, last.time, ticksPerSecond);
                if (firstSeconds > kEpsilon && lastSeconds > kEpsilon)
                {
                    const float velocitySeam = glm::length((second.value - first.value) / firstSeconds -
                                                           (last.value - previous.value) / lastSeconds);
                    if (velocitySeam > settings.loopScaleVelocityTolerance)
                        analysis.issues.push_back({IssueType::LoopVelocitySeam,
                                                   boneIndex,
                                                   ChannelMask::Scale,
                                                   clip.duration,
                                                   velocitySeam});
                }

                const auto keep = BuildKeepMask(channel.scaleKeys,
                                                settings.redundantScaleTolerance,
                                                mixVector,
                                                vectorError,
                                                simplifyComparisonsRemaining);
                AddRedundantIssues(channel.scaleKeys,
                                   keep,
                                   boneIndex,
                                   ChannelMask::Scale,
                                   mixVector,
                                   vectorError,
                                   analysis);
            }
        }
        return analysis;
    }

    size_t FixQuaternionHemisphereFlips(AnimationClip &clip)
    {
        size_t flips = 0;
        for (AnimationChannel &channel : clip.channels)
        {
            for (size_t i = 1; i < channel.rotationKeys.size(); ++i)
            {
                if (glm::dot(SafeNormalized(channel.rotationKeys[i - 1].value),
                             SafeNormalized(channel.rotationKeys[i].value)) < 0.0f)
                {
                    channel.rotationKeys[i].value = Negated(channel.rotationKeys[i].value);
                    ++flips;
                }
            }
        }
        return flips;
    }

    size_t ExtractRootMotion(AnimationClip &clip, int boneIndex, bool includeVertical)
    {
        if (!clip.rootMotion.Empty())
            return 0;
        auto channel = std::find_if(clip.channels.begin(), clip.channels.end(), [&](const AnimationChannel &candidate)
                                    { return candidate.boneIndex == boneIndex; });
        if (channel == clip.channels.end() || channel->positionKeys.size() < 2 || !FiniteKeys(channel->positionKeys))
            return 0;

        const vec3 origin = AnimationEvaluator::InterpolatePosition(channel->positionKeys, 0.f);
        std::vector<PositionKey> motion;
        motion.reserve(channel->positionKeys.size());
        float maxTravel = 0.f;
        for (const PositionKey &key : channel->positionKeys)
        {
            vec3 travel = key.value - origin;
            if (!includeVertical)
                travel.y = 0.f;
            maxTravel = std::max(maxTravel, glm::length(travel));
            motion.push_back({key.time, travel, key.interpolation});
        }
        if (maxTravel <= kEpsilon)
            return 0;

        for (size_t i = 0; i < channel->positionKeys.size(); ++i)
            channel->positionKeys[i].value -= motion[i].value;
        clip.rootMotion.boneIndex = boneIndex;
        clip.rootMotion.positionKeys = std::move(motion);
        return channel->positionKeys.size();
    }

    size_t BakeRootMotion(AnimationClip &clip)
    {
        if (clip.rootMotion.Empty() || !FiniteKeys(clip.rootMotion.positionKeys))
            return 0;
        auto channel = std::find_if(clip.channels.begin(), clip.channels.end(), [&](const AnimationChannel &candidate)
                                    { return candidate.boneIndex == clip.rootMotion.boneIndex; });
        if (channel == clip.channels.end() || channel->positionKeys.empty() || !FiniteKeys(channel->positionKeys))
            return 0;

        const std::vector<PositionKey> pose = channel->positionKeys;
        std::vector<float> times;
        times.reserve(pose.size() + clip.rootMotion.positionKeys.size());
        for (const PositionKey &key : pose)
            times.push_back(key.time);
        for (const PositionKey &key : clip.rootMotion.positionKeys)
            times.push_back(key.time);
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](float a, float b)
                                { return std::abs(a - b) <= kEpsilon; }),
                    times.end());

        channel->positionKeys.clear();
        channel->positionKeys.reserve(times.size());
        for (float time : times)
            channel->positionKeys.push_back({time,
                                             AnimationEvaluator::InterpolatePosition(pose, time) +
                                                 AnimationEvaluator::InterpolatePosition(clip.rootMotion.positionKeys, time),
                                             InterpolationAt(pose, time)});
        clip.rootMotion = {};
        return channel->positionKeys.size();
    }

    size_t MakeCyclic(AnimationClip &clip, ChannelMask channels)
    {
        if (!std::isfinite(clip.duration) || clip.duration <= kEpsilon)
            return 0;
        for (const AnimationChannel &channel : clip.channels)
            if ((HasChannel(channels, ChannelMask::Position) && !FiniteKeys(channel.positionKeys)) ||
                (HasChannel(channels, ChannelMask::Rotation) && !FiniteKeys(channel.rotationKeys)) ||
                (HasChannel(channels, ChannelMask::Scale) && !FiniteKeys(channel.scaleKeys)))
                return 0;
        size_t writes = 0;
        for (AnimationChannel &channel : clip.channels)
        {
            if (HasChannel(channels, ChannelMask::Position) && !channel.positionKeys.empty())
            {
                const vec3 value = AnimationEvaluator::InterpolatePosition(channel.positionKeys, 0.0f);
                writes += UpsertKey(channel.positionKeys,
                                    clip.duration,
                                    value,
                                    InterpolationAt(channel.positionKeys, 0.0f));
            }
            if (HasChannel(channels, ChannelMask::Rotation) && !channel.rotationKeys.empty())
            {
                quat value = AnimationEvaluator::InterpolateRotation(channel.rotationKeys, 0.0f);
                const auto beforeEnd = std::lower_bound(channel.rotationKeys.begin(),
                                                        channel.rotationKeys.end(),
                                                        clip.duration - kEpsilon,
                                                        [](const RotationKey &key, float time)
                                                        { return key.time < time; });
                if (beforeEnd != channel.rotationKeys.begin())
                    value = SameHemisphere(std::prev(beforeEnd)->value, value);
                writes += UpsertKey(channel.rotationKeys,
                                    clip.duration,
                                    value,
                                    InterpolationAt(channel.rotationKeys, 0.0f));
            }
            if (HasChannel(channels, ChannelMask::Scale) && !channel.scaleKeys.empty())
            {
                const vec3 value = AnimationEvaluator::InterpolateScale(channel.scaleKeys, 0.0f);
                writes += UpsertKey(channel.scaleKeys,
                                    clip.duration,
                                    value,
                                    InterpolationAt(channel.scaleKeys, 0.0f));
            }
        }
        return writes;
    }

    size_t TweenInterval(AnimationClip &clip,
                         float startTime,
                         float endTime,
                         float stepTicks,
                         TweenMode mode,
                         std::span<const int> boneIndices,
                         ChannelMask channels)
    {
        if (!std::isfinite(startTime) || !std::isfinite(endTime) || !std::isfinite(stepTicks) ||
            stepTicks <= kEpsilon || endTime - startTime <= stepTicks + kEpsilon ||
            static_cast<double>(endTime - startTime) / stepTicks > static_cast<double>(kMaxTweenSamples))
            return 0;

        size_t writes = 0;
        for (AnimationChannel &channel : clip.channels)
        {
            if (!BoneSelected(boneIndices, channel.boneIndex))
                continue;
            // Each track is independent: a NaN on rotation must not skip a finite location curve.
            if (HasChannel(channels, ChannelMask::Position) && FiniteKeys(channel.positionKeys))
                writes += TweenKeys(
                    channel.positionKeys, startTime, endTime, stepTicks, mode,
                    [](const std::vector<PositionKey> &keys, float time)
                    { return AnimationEvaluator::InterpolatePosition(keys, time); },
                    [](const vec3 &a, const vec3 &b, float factor)
                    { return glm::mix(a, b, factor); });
            if (HasChannel(channels, ChannelMask::Rotation) && FiniteKeys(channel.rotationKeys))
                writes += TweenKeys(
                    channel.rotationKeys, startTime, endTime, stepTicks, mode,
                    [](const std::vector<RotationKey> &keys, float time)
                    { return AnimationEvaluator::InterpolateRotation(keys, time); },
                    [](const quat &a, const quat &b, float factor)
                    { return ShortestSlerp(a, b, factor); });
            if (HasChannel(channels, ChannelMask::Scale) && FiniteKeys(channel.scaleKeys))
                writes += TweenKeys(
                    channel.scaleKeys, startTime, endTime, stepTicks, mode,
                    [](const std::vector<ScaleKey> &keys, float time)
                    { return AnimationEvaluator::InterpolateScale(keys, time); },
                    [](const vec3 &a, const vec3 &b, float factor)
                    { return glm::mix(a, b, factor); });
        }
        return writes;
    }

    size_t BallisticInterval(AnimationClip &clip,
                             int boneIndex,
                             const vec3 &restPosition,
                             float startTime,
                             float endTime,
                             float stepTicks,
                             float gravity)
    {
        if (boneIndex < 0 || !std::isfinite(startTime) || !std::isfinite(endTime) || !std::isfinite(stepTicks) ||
            !std::isfinite(gravity) || gravity < 0.f || !Finite(restPosition) || stepTicks <= kEpsilon ||
            clip.ticksPerSecond <= kEpsilon || endTime - startTime <= stepTicks + kEpsilon ||
            static_cast<double>(endTime - startTime) / stepTicks > static_cast<double>(kMaxTweenSamples))
            return 0;

        // The clip is not touched until every sample is known good - a refused bake must leave it alone,
        // including the channel it would have had to create.
        AnimationChannel *channel = nullptr;
        for (AnimationChannel &candidate : clip.channels)
            if (candidate.boneIndex == boneIndex)
                channel = &candidate;
        const std::vector<PositionKey> none;
        const std::vector<PositionKey> &source = channel ? channel->positionKeys : none;
        if (!FiniteKeys(source))
            return 0;

        // An empty curve plays the bind translation, so that is where the arc launches from and lands.
        const vec3 startValue =
            source.empty() ? restPosition : AnimationEvaluator::InterpolatePosition(source, startTime);
        const vec3 endValue = source.empty() ? restPosition : AnimationEvaluator::InterpolatePosition(source, endTime);
        // Read while the curve is still the source one: rewriting the start key Linear would otherwise hand
        // the inserted end key its own mode instead of the mode the segment really plays there.
        const AnimationInterpolation endBlend = InterpolationAt(source, endTime);
        const float tolerance = std::max(kEpsilon, stepTicks * 0.001f);
        const float flight = (endTime - startTime) / clip.ticksPerSecond; // seconds in the air
        // The up velocity that lands exactly on the end value: y1 = y0 + v0*T - g*T*T/2.
        // ponytail: rig space is Y-up like the rest of the editor; a Z-up rig needs the axis exposed.
        const float launch = (endValue.y - startValue.y) / flight + 0.5f * gravity * flight;

        // Every sample is taken before anything is written, exactly like the tween bake.
        std::vector<std::pair<float, vec3>> samples;
        const float first = std::ceil((startTime + tolerance) / stepTicks) * stepTicks;
        for (size_t step = 0; step < kMaxTweenSamples; ++step)
        {
            const float time = first + stepTicks * static_cast<float>(step);
            if (time >= endTime - tolerance)
                break;
            const float elapsed = (time - startTime) / clip.ticksPerSecond;
            vec3 value = glm::mix(startValue, endValue, (time - startTime) / (endTime - startTime));
            value.y = startValue.y + launch * elapsed - 0.5f * gravity * elapsed * elapsed;
            if (!Finite(value)) // a large enough gravity overflows the parabola
                return 0;
            samples.push_back({time, value});
        }
        if (samples.empty())
            return 0;

        if (!channel)
        {
            channel = &clip.channels.emplace_back();
            channel->boneIndex = boneIndex;
        }
        std::vector<PositionKey> &keys = channel->positionKeys;

        size_t writes = 0;
        // The ends anchor the arc: without keys there, the segments outside the interval would re-aim at the
        // first and last interior sample instead of at the pose the clip already shows. Interpolation is
        // outgoing, so the start key is rewritten Linear even when it already existed: a Smooth or Stepped one
        // would ease or hold the first segment of a curve that is meant to be pure gravity.
        writes += UpsertKey(keys, startTime, startValue, AnimationInterpolation::Linear);
        if (!HasKeyAt(keys, endTime, tolerance))
            writes += UpsertKey(keys, endTime, endValue, endBlend);
        std::erase_if(keys, [&](const PositionKey &key)
                      { return key.time > startTime + tolerance && key.time < endTime - tolerance; });
        for (const auto &[time, value] : samples)
            writes += UpsertKey(keys, time, value, AnimationInterpolation::Linear);
        return writes;
    }

    size_t BallisticBodyInterval(AnimationClip &clip,
                                 const Skeleton &skeleton,
                                 int rootBone,
                                 const vec3 &restPosition,
                                 std::span<const float> boneMasses,
                                 std::span<const vec3> restCentres,
                                 float startTime,
                                 float endTime,
                                 float stepTicks,
                                 float gravity)
    {
        const int boneCount = skeleton.GetBoneCount();
        if (rootBone < 0 || rootBone >= boneCount || static_cast<int>(boneMasses.size()) != boneCount ||
            static_cast<int>(restCentres.size()) != boneCount || !std::isfinite(startTime) || !std::isfinite(endTime) ||
            !std::isfinite(stepTicks) || !std::isfinite(gravity) || gravity < 0.f || !Finite(restPosition) ||
            stepTicks <= kEpsilon || clip.ticksPerSecond <= kEpsilon || endTime - startTime <= stepTicks + kEpsilon ||
            static_cast<double>(endTime - startTime) / stepTicks > static_cast<double>(kMaxTweenSamples))
            return 0;
        // Only the mass hanging under the moved bone is the thrown body: a control root above the hips or a
        // separate prop root does not move with it and is left out, which also keeps the one-pass correction exact.
        std::vector<char> thrown(boneCount, 0);
        float totalMass = 0.f;
        for (int i = 0; i < boneCount; ++i)
        {
            if (!std::isfinite(boneMasses[i]) || boneMasses[i] < 0.f || !Finite(restCentres[i]))
                return 0;
            int bone = i;
            for (int depth = 0; bone >= 0 && bone != rootBone && depth < boneCount; ++depth)
                bone = skeleton.bones[bone].parentIndex;
            thrown[i] = bone == rootBone;
            if (thrown[i])
                totalMass += boneMasses[i];
        }
        if (!std::isfinite(totalMass) || totalMass <= kEpsilon)
            return 0;

        const float tolerance = std::max(kEpsilon, stepTicks * 0.001f);
        const float flight = (endTime - startTime) / clip.ticksPerSecond;

        // Mass centres ride the posed bones in rig space (the skeleton with its root transform removed, where the
        // rest centres are given). The root's Location key reaches rig space through its parent and its prefix,
        // so that linear map is what turns a wanted centre-of-mass shift into a key delta, and its rotation is
        // what turns a wanted turn of the body into a rotation key.
        const mat4 invRoot = glm::inverse(skeleton.rootTransform);
        struct Frame
        {
            float time = 0.f;
            vec3 centre = vec3(0.f), rootHead = vec3(0.f);
            quat rootGlobal = quat(1.f, 0.f, 0.f, 0.f), keyRotation = quat(1.f, 0.f, 0.f, 0.f);
            mat3 keyToRig = mat3(1.f);
            std::vector<vec3> centres; // per bone, thrown bones only meaningful
            std::vector<vec3> axes;    // per bone, the rod's direction in rig space (its rest head -> mass centre)
        };
        // Each bone is a thin rod from its rest head through its mass centre: length and rest direction once
        std::vector<float> rodLength(boneCount, 0.f);
        std::vector<vec3> rodRest(boneCount, vec3(0.f, 1.f, 0.f));
        for (int i = 0; i < boneCount; ++i)
        {
            const vec3 restHead = vec3((invRoot * glm::inverse(skeleton.bones[i].offsetMatrix))[3]);
            const vec3 half = restCentres[i] - restHead;
            const float length = glm::length(half);
            rodLength[i] = 2.f * length;
            if (length > kEpsilon)
                rodRest[i] = half / length;
        }
        std::vector<mat4> globals;
        auto sample = [&](float time, Frame &frame)
        {
            SampleGlobalTransforms(clip, skeleton, time, globals);
            if (static_cast<int>(globals.size()) != boneCount)
                return false;
            frame.time = time;
            frame.centres.assign(boneCount, vec3(0.f));
            frame.axes.assign(boneCount, vec3(0.f, 1.f, 0.f));
            vec3 sum(0.f);
            for (int i = 0; i < boneCount; ++i)
            {
                if (!thrown[i] || boneMasses[i] <= 0.f)
                    continue;
                const mat4 restToPosed = invRoot * globals[i] * skeleton.bones[i].offsetMatrix * skeleton.rootTransform;
                frame.centres[i] = vec3(restToPosed * vec4(restCentres[i], 1.f));
                const vec3 axis = mat3(restToPosed) * rodRest[i];
                if (const float length = glm::length(axis); length > kEpsilon)
                    frame.axes[i] = axis / length;
                sum += boneMasses[i] * frame.centres[i];
            }
            frame.centre = sum / totalMass;
            const int parent = skeleton.bones[rootBone].parentIndex;
            const mat4 keyToRig4 = invRoot * (parent >= 0 ? globals[parent] : mat4(1.f)) * skeleton.bones[rootBone].intermediatePrefix;
            frame.keyToRig = mat3(keyToRig4);
            frame.keyRotation = MatrixRotation(keyToRig4);
            frame.rootHead = vec3((invRoot * globals[rootBone])[3]);
            frame.rootGlobal = MatrixRotation(invRoot * globals[rootBone]);
            const float det = glm::determinant(frame.keyToRig);
            return Finite(frame.centre) && Finite(frame.rootHead) && std::isfinite(det) && std::abs(det) > kEpsilon;
        };
        // Every sample is taken from the untouched source before anything is written: the two ends and the frame
        // grid between them (the ends feed the velocity differences and stay pinned).
        std::vector<Frame> frames(1);
        if (!sample(startTime, frames.front()))
            return 0;
        const float first = std::ceil((startTime + tolerance) / stepTicks) * stepTicks;
        for (size_t step = 0; step < kMaxTweenSamples; ++step)
        {
            const float time = first + stepTicks * static_cast<float>(step);
            if (time >= endTime - tolerance)
                break;
            Frame frame;
            if (!sample(time, frame))
                return 0;
            frames.push_back(std::move(frame));
        }
        Frame last;
        if (frames.size() < 2 || !sample(endTime, last))
            return 0;
        frames.push_back(std::move(last));
        const int n = static_cast<int>(frames.size());
        const vec3 startCentre = frames.front().centre, endCentre = frames.back().centre;
        // ponytail: rig space is Y-up like the rest of the editor; a Z-up rig needs the axis exposed.
        const float launch = (endCentre.y - startCentre.y) / flight + 0.5f * gravity * flight;

        // Angular momentum: in the air nothing can turn the body but its own limbs, so the angular momentum of
        // the thrown masses about their centre stays what it was. Each interior frame measures the momentum the
        // source poses carry - the mass centres orbiting the body centre (central differences on the grid) plus
        // every rod spinning about its own centre (m l^2 / 12 for a thin rod, its direction change per frame) - and
        // the root turns against it at -J^-1 L, J being the body's inertia tensor about its centre (point masses
        // plus the rods' own tensors); the net turn over the flight is then removed as a linear ramp so both ends
        // keep their authored rotation - only a swing that is not uniform in time shows, and it shows where the
        // swing happens. ponytail: rods, not capsules; the spin of a rod about its own axis is invisible to the
        // direction and weighs nothing here.
        std::vector<vec3> turn(n, vec3(0.f));
        vec3 previousOmega(0.f);
        for (int k = 1; k < n; ++k)
        {
            const int mid = std::min(k, n - 2);
            const Frame &a = frames[mid - 1], &b = frames[mid], &c = frames[mid + 1];
            const float span = (c.time - a.time) / clip.ticksPerSecond;
            vec3 momentum(0.f);
            mat3 tensor(0.f);
            if (span > kEpsilon)
            {
                const vec3 centreVelocity = (c.centre - a.centre) / span;
                for (int i = 0; i < boneCount; ++i)
                    if (thrown[i] && boneMasses[i] > 0.f)
                    {
                        const vec3 r = b.centres[i] - b.centre;
                        const float rod = boneMasses[i] * rodLength[i] * rodLength[i] / 12.f;
                        momentum += boneMasses[i] * glm::cross(r, (c.centres[i] - a.centres[i]) / span - centreVelocity);
                        momentum += rod * glm::cross(a.axes[i], c.axes[i]) / span; // the rod's own turn rate
                        tensor += boneMasses[i] * (glm::dot(r, r) * mat3(1.f) - glm::outerProduct(r, r));
                        tensor += rod * (mat3(1.f) - glm::outerProduct(b.axes[i], b.axes[i]));
                    }
            }
            vec3 omega(0.f);
            if (const float det = glm::determinant(tensor); std::isfinite(det) && std::abs(det) > kEpsilon * kEpsilon * kEpsilon)
            {
                omega = -(glm::inverse(tensor) * momentum);
                if (!Finite(omega))
                    omega = vec3(0.f);
            }
            // trapezoid rule: the momentum was measured at the frames, the turn accrues between them
            turn[k] = turn[k - 1] + (k == 1 ? omega : (previousOmega + omega) * 0.5f) *
                                        ((frames[k].time - frames[k - 1].time) / clip.ticksPerSecond);
            previousOmega = omega;
        }
        const vec3 net = turn[n - 1];
        for (int k = 1; k < n - 1; ++k)
            turn[k] -= net * ((frames[k].time - startTime) / (endTime - startTime));
        if (!Finite(net))
            return 0;

        AnimationChannel *channel = nullptr;
        for (AnimationChannel &candidate : clip.channels)
            if (candidate.boneIndex == rootBone)
                channel = &candidate;
        const std::vector<PositionKey> none;
        const std::vector<RotationKey> noneRotation;
        const std::vector<PositionKey> &source = channel ? channel->positionKeys : none;
        const std::vector<RotationKey> &sourceRotation = channel ? channel->rotationKeys : noneRotation;
        if (!FiniteKeys(source) || !FiniteKeys(sourceRotation))
            return 0;
        auto rootAt = [&](float time)
        { return source.empty() ? restPosition : AnimationEvaluator::InterpolatePosition(source, time); };
        vec3 bindPosition, bindScale;
        quat bindRotation;
        AnimationEvaluator::BindPose(skeleton.bones[rootBone], bindPosition, bindRotation, bindScale);
        auto rotationAt = [&](float time)
        { return sourceRotation.empty() ? bindRotation : AnimationEvaluator::InterpolateRotation(sourceRotation, time); };
        const vec3 startRoot = rootAt(startTime), endRoot = rootAt(endTime);
        const quat startRotation = rotationAt(startTime), endRotation = rotationAt(endTime);
        const AnimationInterpolation endBlend = InterpolationAt(source, endTime);
        const AnimationInterpolation endRotationBlend = InterpolationAt(sourceRotation, endTime);

        std::vector<std::pair<float, vec3>> samples;
        std::vector<std::pair<float, quat>> turns;
        quat previous = startRotation;
        bool turned = false;
        for (int k = 1; k < n - 1; ++k)
        {
            const Frame &frame = frames[k];
            const float elapsed = (frame.time - startTime) / clip.ticksPerSecond;
            vec3 target = glm::mix(startCentre, endCentre, (frame.time - startTime) / (endTime - startTime));
            target.y = startCentre.y + launch * elapsed - 0.5f * gravity * elapsed * elapsed;
            const float angle = glm::length(turn[k]);
            const quat rotation = angle > kEpsilon ? glm::angleAxis(angle, turn[k] / angle) : quat(1.f, 0.f, 0.f, 0.f);
            // the turn is about the centre of mass: turning the root about its own head needs the head carried round
            const vec3 arm = frame.rootHead - frame.centre;
            const vec3 value = rootAt(frame.time) + glm::inverse(frame.keyToRig) * ((target - frame.centre) + (rotation * arm - arm));
            const quat key = SameHemisphere(previous, SafeNormalized(glm::conjugate(frame.keyRotation) * rotation * frame.rootGlobal));
            if (!Finite(value) || !Finite(key))
                return 0;
            samples.push_back({frame.time, value});
            turned = turned || angle > kEpsilon;
            turns.push_back({frame.time, key});
            previous = key;
        }
        if (samples.empty())
            return 0;
        if (!turned)
            turns.clear(); // nothing turned: the rotation curve is not touched

        if (!channel)
        {
            channel = &clip.channels.emplace_back();
            channel->boneIndex = rootBone;
        }
        std::vector<PositionKey> &keys = channel->positionKeys;
        size_t writes = 0;
        writes += UpsertKey(keys, startTime, startRoot, AnimationInterpolation::Linear);
        if (!HasKeyAt(keys, endTime, tolerance))
            writes += UpsertKey(keys, endTime, endRoot, endBlend);
        std::erase_if(keys, [&](const PositionKey &key)
                      { return key.time > startTime + tolerance && key.time < endTime - tolerance; });
        for (const auto &[time, value] : samples)
            writes += UpsertKey(keys, time, value, AnimationInterpolation::Linear);
        if (!turns.empty())
        {
            std::vector<RotationKey> &rotations = channel->rotationKeys;
            writes += UpsertKey(rotations, startTime, startRotation, AnimationInterpolation::Linear);
            if (!HasKeyAt(rotations, endTime, tolerance))
                writes += UpsertKey(rotations, endTime, SameHemisphere(previous, endRotation), endRotationBlend);
            std::erase_if(rotations, [&](const RotationKey &key)
                          { return key.time > startTime + tolerance && key.time < endTime - tolerance; });
            for (const auto &[time, value] : turns)
                writes += UpsertKey(rotations, time, value, AnimationInterpolation::Linear);
            // the sign stays continuous into and out of the interval: from the start key to the end of the curve
            const size_t from = static_cast<size_t>(
                std::lower_bound(rotations.begin(), rotations.end(), startTime - tolerance, [](const RotationKey &key, float t)
                                 { return key.time < t; }) -
                rotations.begin());
            for (size_t key = std::max<size_t>(from, 1); key < rotations.size(); ++key)
                rotations[key].value = SameHemisphere(rotations[key - 1].value, rotations[key].value);
        }
        return writes;
    }

    size_t Smooth(AnimationClip &clip, const SmoothSettings &settings, std::span<const int> boneIndices)
    {
        const bool invalidStart = std::isnan(settings.startTime) ||
                                  (std::isinf(settings.startTime) && settings.startTime > 0.0f);
        const bool invalidEnd = std::isnan(settings.endTime) ||
                                (std::isinf(settings.endTime) && settings.endTime < 0.0f);
        if (invalidStart || invalidEnd || !std::isfinite(settings.strength) ||
            settings.startTime > settings.endTime ||
            settings.strength < 0.0f || settings.strength > 1.0f ||
            settings.passes < 1 || settings.passes > MaxSmoothPasses || settings.strength <= kEpsilon)
            return 0;

        for (const AnimationChannel &channel : clip.channels)
        {
            if (!BoneSelected(boneIndices, channel.boneIndex))
                continue;
            if ((HasChannel(settings.channels, ChannelMask::Position) && !FiniteKeys(channel.positionKeys)) ||
                (HasChannel(settings.channels, ChannelMask::Rotation) && !FiniteKeys(channel.rotationKeys)) ||
                (HasChannel(settings.channels, ChannelMask::Scale) && !FiniteKeys(channel.scaleKeys)))
                return 0;
        }

        size_t writes = 0;
        for (AnimationChannel &channel : clip.channels)
        {
            if (!BoneSelected(boneIndices, channel.boneIndex))
                continue;
            if (HasChannel(settings.channels, ChannelMask::Position))
                writes += SmoothVectors(channel.positionKeys, settings);
            if (HasChannel(settings.channels, ChannelMask::Rotation))
                writes += SmoothRotations(channel.rotationKeys, settings);
            if (HasChannel(settings.channels, ChannelMask::Scale))
                writes += SmoothVectors(channel.scaleKeys, settings);
        }
        return writes;
    }

    size_t Simplify(AnimationClip &clip, const SimplifySettings &settings, std::span<const int> boneIndices)
    {
        if (!std::isfinite(settings.positionTolerance) ||
            !std::isfinite(settings.rotationToleranceDegrees) || !std::isfinite(settings.scaleTolerance) ||
            settings.positionTolerance < 0.0f || settings.rotationToleranceDegrees < 0.0f ||
            settings.scaleTolerance < 0.0f)
            return 0;

        for (const AnimationChannel &channel : clip.channels)
        {
            if (!BoneSelected(boneIndices, channel.boneIndex))
                continue;
            if ((HasChannel(settings.channels, ChannelMask::Position) && !FiniteKeys(channel.positionKeys)) ||
                (HasChannel(settings.channels, ChannelMask::Rotation) && !FiniteKeys(channel.rotationKeys)) ||
                (HasChannel(settings.channels, ChannelMask::Scale) && !FiniteKeys(channel.scaleKeys)))
                return 0;
        }

        size_t removed = 0;
        const auto mixVector = [](const vec3 &a, const vec3 &b, float factor)
        { return glm::mix(a, b, factor); };
        const auto vectorError = [](const vec3 &a, const vec3 &b)
        { return glm::length(a - b); };
        const auto mixRotation = [](const quat &a, const quat &b, float factor)
        { return ShortestSlerp(a, b, factor); };
        size_t comparisonsRemaining = kMaxSimplifyComparisons;

        for (AnimationChannel &channel : clip.channels)
        {
            if (!BoneSelected(boneIndices, channel.boneIndex))
                continue;
            if (HasChannel(settings.channels, ChannelMask::Position))
                removed += ApplyKeepMask(channel.positionKeys,
                                         BuildKeepMask(channel.positionKeys,
                                                       settings.positionTolerance,
                                                       mixVector,
                                                       vectorError,
                                                       comparisonsRemaining));
            if (HasChannel(settings.channels, ChannelMask::Rotation))
                removed += ApplyKeepMask(channel.rotationKeys,
                                         BuildKeepMask(channel.rotationKeys,
                                                       settings.rotationToleranceDegrees,
                                                       mixRotation,
                                                       RotationDistanceDegrees,
                                                       comparisonsRemaining));
            if (HasChannel(settings.channels, ChannelMask::Scale))
                removed += ApplyKeepMask(channel.scaleKeys,
                                         BuildKeepMask(channel.scaleKeys,
                                                       settings.scaleTolerance,
                                                       mixVector,
                                                       vectorError,
                                                       comparisonsRemaining));
        }
        return removed;
    }

    size_t OffsetBoneKeyTimes(AnimationClip &clip,
                              int boneIndex,
                              float deltaTime,
                              bool wrap,
                              ChannelMask channels)
    {
        if (!std::isfinite(clip.duration) || !std::isfinite(deltaTime) || clip.duration <= kEpsilon ||
            std::abs(deltaTime) <= kEpsilon)
            return 0;
        const auto channel = std::find_if(clip.channels.begin(), clip.channels.end(), [boneIndex](const AnimationChannel &candidate)
                                          { return candidate.boneIndex == boneIndex; });
        const bool offsetRootMotion = HasChannel(channels, ChannelMask::Position) &&
                                      clip.rootMotion.boneIndex == boneIndex && !clip.rootMotion.Empty();
        if (channel == clip.channels.end() && !offsetRootMotion)
            return 0;
        if ((channel != clip.channels.end() && HasChannel(channels, ChannelMask::Position) &&
             !FiniteKeys(channel->positionKeys)) ||
            (channel != clip.channels.end() && HasChannel(channels, ChannelMask::Rotation) &&
             !FiniteKeys(channel->rotationKeys)) ||
            (channel != clip.channels.end() && HasChannel(channels, ChannelMask::Scale) &&
             !FiniteKeys(channel->scaleKeys)) ||
            (offsetRootMotion && !FiniteKeys(clip.rootMotion.positionKeys)))
            return 0;

        size_t touched = 0;
        if (channel != clip.channels.end() && HasChannel(channels, ChannelMask::Position))
            touched += OffsetTimes(channel->positionKeys, deltaTime, clip.duration, wrap);
        if (channel != clip.channels.end() && HasChannel(channels, ChannelMask::Rotation))
            touched += OffsetTimes(channel->rotationKeys, deltaTime, clip.duration, wrap);
        if (channel != clip.channels.end() && HasChannel(channels, ChannelMask::Scale))
            touched += OffsetTimes(channel->scaleKeys, deltaTime, clip.duration, wrap);
        if (offsetRootMotion)
            touched += OffsetTimes(clip.rootMotion.positionKeys, deltaTime, clip.duration, wrap);
        return touched;
    }

    namespace
    {
        // Bone role lookup by name: strip a side suffix, glob the base against the preset's aliases.
        bool GaitGlob(std::string_view pattern, std::string_view text)
        {
            size_t p = 0, t = 0, star = std::string_view::npos, retry = 0;
            while (t < text.size())
            {
                if (p < pattern.size() && (pattern[p] == '?' || std::tolower(static_cast<unsigned char>(pattern[p])) ==
                                                                    std::tolower(static_cast<unsigned char>(text[t]))))
                    ++p, ++t;
                else if (p < pattern.size() && pattern[p] == '*')
                    star = p++, retry = t;
                else if (star != std::string_view::npos)
                    p = star + 1, t = ++retry;
                else
                    return false;
            }
            while (p < pattern.size() && pattern[p] == '*')
                ++p;
            return p == pattern.size();
        }

        // "thigh.L" -> {"thigh", 'L'}; a name without a side suffix keeps side 0.
        std::pair<std::string, char> GaitSide(const std::string &name)
        {
            static const char *suffixes[] = {".L", ".R", "_L", "_R", ".l", ".r", "_l", "_r"};
            for (const char *suffix : suffixes)
                if (name.size() > 2 && name.compare(name.size() - 2, 2, suffix) == 0)
                    return {name.substr(0, name.size() - 2), static_cast<char>(std::toupper(static_cast<unsigned char>(suffix[1])))};
            return {name, 0};
        }

        int GaitRole(const Skeleton &skeleton, std::span<const char *const> patterns, char side, std::vector<char> &taken)
        {
            for (const char *pattern : patterns)
                for (int i = 0; i < skeleton.GetBoneCount(); i++)
                {
                    if (taken[i])
                        continue;
                    const auto [base, boneSide] = GaitSide(skeleton.bones[i].name);
                    if (boneSide == side && GaitGlob(pattern, base))
                    {
                        taken[i] = 1;
                        return i;
                    }
                }
            return -1;
        }
    } // namespace

    bool GenerateGait(const Skeleton &skeleton, const GaitSettings &settings, AnimationClip &out, GaitReport &report)
    {
        report = {};
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount == 0 || settings.frames < 4 || settings.frames > 4096 || settings.ticksPerFrame <= kEpsilon ||
            !std::isfinite(settings.amplitude) || settings.amplitude <= 0.0f)
        {
            report.error = "needs a skeleton, 4..4096 frames and a positive amplitude";
            return false;
        }
        std::vector<char> taken(boneCount, 0);
        static constexpr const char *kHips[] = {"hips", "pelvis", "*hip*", "*pelvis*"};
        static constexpr const char *kSpine[] = {"spine", "body", "*spine*", "*torso*", "*body*"};
        static constexpr const char *kChest[] = {"chest", "*chest*", "*ribcage*"};
        static constexpr const char *kNeck[] = {"neck", "*neck*"};
        static constexpr const char *kHead[] = {"head", "*head*"};
        static constexpr const char *kShoulder[] = {"shoulder", "*shoulder*", "*clavicle*"};
        static constexpr const char *kUpperArm[] = {"upper_arm", "*upper*arm*", "*upperarm*", "*arm*"};
        static constexpr const char *kForearm[] = {"forearm", "*forearm*", "*lower*arm*", "*lowerarm*"};
        static constexpr const char *kHand[] = {"hand", "*hand*"};
        static constexpr const char *kThigh[] = {"thigh", "*thigh*", "*upper*leg*", "*upperleg*", "*leg*"};
        static constexpr const char *kShin[] = {"shin", "*shin*", "*calf*", "*lower*leg*", "*lowerleg*"};
        static constexpr const char *kFoot[] = {"foot", "*foot*", "*boot*"};
        static constexpr const char *kToe[] = {"toe", "*toe*"};
        auto role = [&](const char *label, std::span<const char *const> patterns, char side)
        {
            const int bone = GaitRole(skeleton, patterns, side, taken);
            if (bone >= 0)
                report.roles.push_back({side ? std::string(label) + "." + side : std::string(label), skeleton.bones[bone].name});
            return bone;
        };
        // limbs first so "*body*" cannot swallow "upper_body_armor.L"-style parts before the arms are found
        struct Side
        {
            int shoulder, upperArm, forearm, hand, thigh, shin, foot, toe;
        } sides[2];
        for (int s = 0; s < 2; s++)
        {
            const char side = s == 0 ? 'L' : 'R';
            sides[s].thigh = role("thigh", kThigh, side);
            sides[s].shin = role("shin", kShin, side);
            sides[s].foot = role("foot", kFoot, side);
            sides[s].toe = role("toe", kToe, side);
            sides[s].shoulder = role("shoulder", kShoulder, side);
            sides[s].upperArm = role("upper_arm", kUpperArm, side);
            sides[s].forearm = role("forearm", kForearm, side);
            sides[s].hand = role("hand", kHand, side);
        }
        const int hips = role("hips", kHips, 0);
        const int spine = role("spine", kSpine, 0);
        const int chest = role("chest", kChest, 0);
        const int neck = role("neck", kNeck, 0);
        const int head = role("head", kHead, 0);
        // the hips carry the body when the rig names them (mocap: hips under a keyless control root), else the root
        int carrier = hips;
        for (int i = 0; i < boneCount && carrier < 0; i++)
            if (skeleton.bones[i].parentIndex < 0)
                carrier = i;
        const int pelvis = carrier; // the bone that sways and twists
        if (sides[0].thigh < 0 && sides[1].thigh < 0 && sides[0].upperArm < 0 && sides[1].upperArm < 0)
        {
            report.error = "no leg or arm bones found by name (thigh / leg / upper_arm with a .L / .R side)";
            return false;
        }

        // rig-space bind globals: a swing about a rig axis becomes a local delta in the bone's own bind frame
        AnimationClip bind;
        std::vector<mat4> bindGlobals;
        SampleGlobalTransforms(bind, skeleton, 0.0f, bindGlobals);
        const mat4 inverseRoot = glm::inverse(skeleton.rootTransform);
        std::vector<quat> rigRotation(boneCount);
        std::vector<vec3> rigPosition(boneCount);
        float top = -std::numeric_limits<float>::max(), bottom = std::numeric_limits<float>::max();
        for (int i = 0; i < boneCount; i++)
        {
            const mat4 rig = inverseRoot * bindGlobals[i];
            rigRotation[i] = MatrixRotation(rig);
            rigPosition[i] = vec3(rig[3]);
            top = std::max(top, rigPosition[i].y);
            bottom = std::min(bottom, rigPosition[i].y);
        }
        const float height = std::max(top - bottom, kEpsilon);
        float legLength = 0.0f;
        for (const Side &side : sides)
            if (side.thigh >= 0)
            {
                const int low = side.toe >= 0 ? side.toe : side.foot >= 0 ? side.foot
                                                       : side.shin >= 0   ? side.shin
                                                                          : -1;
                legLength = std::max(legLength, low >= 0 ? glm::distance(rigPosition[side.thigh], rigPosition[low])
                                                         : height * 0.45f);
            }
        if (legLength <= kEpsilon)
            legLength = height * 0.45f;

        const bool run = settings.gait == Gait::Run;
        const float a = settings.amplitude;
        // degrees; the run leans in, lifts the knees and pumps the arms harder
        const float thighSwing = (run ? 42.0f : 27.0f) * a, kneeBend = (run ? 85.0f : 45.0f) * a;
        const float footPitch = (run ? 30.0f : 18.0f) * a, armSwing = (run ? 38.0f : 22.0f) * a;
        const float elbowRest = run ? 70.0f : 15.0f, elbowSwing = (run ? 20.0f : 18.0f) * a;
        const float hipSway = 3.0f * a, hipTwist = (run ? 7.0f : 5.0f) * a, spineLean = run ? 12.0f : 2.0f;
        const float bob = height * (run ? 0.035f : 0.018f) * a;
        const float stride = settings.stride >= 0.0f ? settings.stride
                                                     : 4.0f * legLength * std::sin(glm::radians(thighSwing)) * (run ? 1.25f : 0.9f);
        report.stride = stride;

        out.channels.clear();
        out.rootMotion = {};
        out.markers.clear();
        out.duration = settings.ticksPerFrame * static_cast<float>(settings.frames);
        if (out.ticksPerSecond <= 0.0f)
            out.ticksPerSecond = 24.0f * settings.ticksPerFrame;
        size_t &writes = report.keysWritten;
        // a rig-space swing about `axis` (degrees) keyed as the bone's local rotation at `time`
        auto keyRigSwing = [&](int bone, float time, const vec3 &axisDegrees)
        {
            if (bone < 0)
                return;
            const BoneInfo &info = skeleton.bones[bone];
            vec3 bindPosition, bindScale;
            quat bindRotation;
            AnimationEvaluator::BindPose(info, bindPosition, bindRotation, bindScale);
            const quat swing = quat(glm::radians(axisDegrees)); // XYZ Euler in rig space
            const quat delta = glm::conjugate(rigRotation[bone]) * swing * rigRotation[bone];
            AnimationChannel &channel = FindOrAddChannel(out, bone);
            quat value = SafeNormalized(bindRotation * delta);
            if (!channel.rotationKeys.empty())
                value = SameHemisphere(channel.rotationKeys.back().value, value);
            writes += UpsertKey(channel.rotationKeys, time, value, AnimationInterpolation::Linear);
        };
        auto keyRigOffset = [&](int bone, float time, const vec3 &rigOffset)
        {
            if (bone < 0)
                return;
            const BoneInfo &info = skeleton.bones[bone];
            vec3 bindPosition, bindScale;
            quat bindRotation;
            AnimationEvaluator::BindPose(info, bindPosition, bindRotation, bindScale);
            const int parent = info.parentIndex;
            const vec3 local = parent >= 0 ? glm::conjugate(rigRotation[parent]) * rigOffset : rigOffset;
            writes += UpsertKey(FindOrAddChannel(out, bone).positionKeys, time, bindPosition + local,
                                AnimationInterpolation::Linear);
        };
        for (int frame = 0; frame <= settings.frames; frame++)
        {
            const float time = settings.ticksPerFrame * static_cast<float>(frame);
            const float phase = glm::two_pi<float>() * static_cast<float>(frame % settings.frames) / static_cast<float>(settings.frames);
            for (int s = 0; s < 2; s++)
            {
                const float p = phase + (s == 0 ? 0.0f : glm::pi<float>()); // left leads
                const float sinP = std::sin(p), cosP = std::cos(p);
                // legs: forward at p = 0 (heel strike), knee folds through the swing (p in pi..2pi)
                const float swingLift = std::max(0.0f, -sinP);
                keyRigSwing(sides[s].thigh, time, {-thighSwing * cosP, 0.0f, 0.0f});
                keyRigSwing(sides[s].shin, time, {kneeBend * swingLift * swingLift + (run ? 15.0f : 6.0f) * std::max(0.0f, sinP), 0.0f, 0.0f});
                // foot: toe down through push-off (p near pi), toe up at heel strike
                keyRigSwing(sides[s].foot, time, {footPitch * std::max(0.0f, std::sin(p - glm::half_pi<float>())) - footPitch * 0.4f * std::max(0.0f, cosP), 0.0f, 0.0f});
                keyRigSwing(sides[s].toe, time, {footPitch * 0.5f * std::max(0.0f, std::sin(p - glm::half_pi<float>())), 0.0f, 0.0f});
                // arms: opposite to the same-side leg; the elbow folds a little more as the arm comes forward
                keyRigSwing(sides[s].upperArm, time, {armSwing * cosP, 0.0f, 0.0f});
                keyRigSwing(sides[s].forearm, time, {-(elbowRest + elbowSwing * (0.5f - 0.5f * cosP)), 0.0f, 0.0f});
                keyRigSwing(sides[s].hand, time, {-5.0f * a * cosP, 0.0f, 0.0f});
                keyRigSwing(sides[s].shoulder, time, {0.0f, 0.0f, (s == 0 ? -1.0f : 1.0f) * 2.0f * a * cosP});
            }
            // pelvis: sway over the stance foot, twist the leading hip forward; the spine, chest and head counter it
            const float twist = -hipTwist * std::cos(phase), sway = hipSway * std::sin(phase);
            keyRigSwing(pelvis, time, {0.0f, twist, sway});
            keyRigSwing(spine, time, {-spineLean, -twist * 0.6f, -sway * 0.5f});
            keyRigSwing(chest, time, {0.0f, -twist * 0.4f, -sway * 0.5f});
            keyRigSwing(neck, time, {spineLean * 0.5f, 0.0f, 0.0f});
            keyRigSwing(head, time, {spineLean * 0.5f, 0.0f, 0.0f});
            // the body is lowest at each heel strike and highest over the stance leg: two bobs per cycle
            const float lift = -bob * std::cos(2.0f * phase);
            const float travel = stride * static_cast<float>(frame) / static_cast<float>(settings.frames);
            keyRigOffset(carrier, time, settings.rootMotion ? vec3(0.0f, lift, 0.0f) : vec3(0.0f, lift, travel));
            if (settings.rootMotion && carrier >= 0)
            {
                const int parent = skeleton.bones[carrier].parentIndex;
                const vec3 local = parent >= 0 ? glm::conjugate(rigRotation[parent]) * vec3(0.0f, 0.0f, travel)
                                               : vec3(0.0f, 0.0f, travel);
                writes += UpsertKey(out.rootMotion.positionKeys, time, local, AnimationInterpolation::Linear);
            }
        }
        if (settings.rootMotion && carrier >= 0)
            out.rootMotion.boneIndex = std::abs(stride) > kEpsilon ? carrier : -1;
        if (out.rootMotion.boneIndex < 0)
            out.rootMotion.positionKeys.clear();
        return writes > 0;
    }

    size_t LayerClip(AnimationClip &target, const AnimationClip &source, const Skeleton &skeleton,
                     const LayerSettings &settings)
    {
        if (&target == &source || !std::isfinite(settings.stepTicks) || settings.stepTicks <= kEpsilon ||
            !std::isfinite(settings.weight) || settings.weight <= 0.0f || !std::isfinite(target.duration))
            return 0;
        const float start = std::max(settings.startTime, 0.0f);
        const float end = settings.endTime < 0.0f ? target.duration : std::min(settings.endTime, target.duration);
        if (!std::isfinite(start) || !std::isfinite(end) || end < start - kEpsilon)
            return 0;
        const float weight = std::min(settings.weight, 1.0f);
        const float tolerance = std::max(kEpsilon, settings.stepTicks * 0.001f);
        auto sourceTime = [&](float time)
        {
            float mapped = settings.sourceOffset + (time - start);
            if (source.duration > kEpsilon)
            {
                mapped = std::fmod(mapped, source.duration);
                if (mapped < 0.0f)
                    mapped += source.duration;
            }
            return std::max(mapped, 0.0f);
        };

        struct Bake
        {
            int bone = -1;
            bool position = false, rotation = false, scale = false;
            std::vector<float> times;
            std::vector<vec3> positions, scales;
            std::vector<quat> rotations;
        };
        std::vector<Bake> bakes;
        for (const AnimationChannel &sourceChannel : source.channels)
        {
            if (sourceChannel.boneIndex < 0 || sourceChannel.boneIndex >= skeleton.GetBoneCount() ||
                !BoneSelected(settings.boneIndices, sourceChannel.boneIndex))
                continue;
            Bake bake;
            bake.bone = sourceChannel.boneIndex;
            bake.position = HasChannel(settings.channels, ChannelMask::Position) && !sourceChannel.positionKeys.empty();
            bake.rotation = HasChannel(settings.channels, ChannelMask::Rotation) && !sourceChannel.rotationKeys.empty();
            bake.scale = HasChannel(settings.channels, ChannelMask::Scale) && !sourceChannel.scaleKeys.empty();
            if (!bake.position && !bake.rotation && !bake.scale)
                continue;
            const BoneInfo &bone = skeleton.bones[bake.bone];
            vec3 bindPosition, bindScale;
            quat bindRotation;
            AnimationEvaluator::BindPose(bone, bindPosition, bindRotation, bindScale);
            const AnimationChannel *targetChannel = FindChannel(target, bake.bone);
            for (size_t step = 0; step < kMaxTweenSamples; ++step)
            {
                const float time = start + settings.stepTicks * static_cast<float>(step);
                if (time > end + tolerance)
                    break;
                vec3 position = bindPosition, scale = bindScale;
                quat rotation = bindRotation;
                if (targetChannel)
                    AnimationEvaluator::SampleChannel(*targetChannel, bone, time, position, rotation, scale);
                vec3 sourcePosition, sourceScale;
                quat sourceRotation;
                AnimationEvaluator::SampleChannel(sourceChannel, bone, sourceTime(time), sourcePosition, sourceRotation,
                                                  sourceScale);
                if (settings.additive)
                {
                    position += weight * (sourcePosition - bindPosition);
                    // the source's offset from its bind, in the bone's own frame, applied after the target's pose
                    const quat delta = SafeNormalized(glm::conjugate(SafeNormalized(bindRotation)) * sourceRotation);
                    rotation = SafeNormalized(rotation * ShortestSlerp(quat(1.0f, 0.0f, 0.0f, 0.0f), delta, weight));
                    for (int axis = 0; axis < 3; ++axis)
                        if (std::abs(bindScale[axis]) > kEpsilon)
                            scale[axis] *= 1.0f + weight * (sourceScale[axis] / bindScale[axis] - 1.0f);
                }
                else
                {
                    position = glm::mix(position, sourcePosition, weight);
                    rotation = ShortestSlerp(rotation, sourceRotation, weight);
                    scale = glm::mix(scale, sourceScale, weight);
                }
                if (!Finite(position) || !Finite(rotation) || !Finite(scale))
                    return 0;
                bake.times.push_back(time);
                bake.positions.push_back(position);
                bake.rotations.push_back(rotation);
                bake.scales.push_back(scale);
            }
            if (!bake.times.empty())
                bakes.push_back(std::move(bake));
        }

        size_t writes = 0;
        for (const Bake &bake : bakes)
        {
            AnimationChannel &channel = FindOrAddChannel(target, bake.bone);
            auto replace = [&](auto &keys, const auto &values, bool enabled)
            {
                if (!enabled)
                    return;
                std::erase_if(keys, [&](const auto &key)
                              { return key.time > start - tolerance && key.time < end + tolerance; });
                for (size_t i = 0; i < bake.times.size(); ++i)
                    writes += UpsertKey(keys, bake.times[i], values[i], AnimationInterpolation::Linear);
            };
            replace(channel.positionKeys, bake.positions, bake.position);
            replace(channel.rotationKeys, bake.rotations, bake.rotation);
            replace(channel.scaleKeys, bake.scales, bake.scale);
            for (size_t key = 1; bake.rotation && key < channel.rotationKeys.size(); ++key)
                channel.rotationKeys[key].value =
                    SameHemisphere(channel.rotationKeys[key - 1].value, channel.rotationKeys[key].value);
        }
        return writes;
    }

    size_t PasteMirroredPose(AnimationClip &clip,
                             const Skeleton &skeleton,
                             float sourceTime,
                             float targetTime,
                             std::span<const int> boneIndices,
                             bool includeCenterBones,
                             ChannelMask channels)
    {
        struct Sample
        {
            int targetBone = -1;
            bool hasPosition = false;
            bool hasRotation = false;
            bool hasScale = false;
            mat4 desiredGlobal = mat4(1.0f);
            AnimationInterpolation positionInterpolation = AnimationInterpolation::Linear;
            AnimationInterpolation rotationInterpolation = AnimationInterpolation::Linear;
            AnimationInterpolation scaleInterpolation = AnimationInterpolation::Linear;
        };

        if (!std::isfinite(clip.duration) || !std::isfinite(sourceTime) || !std::isfinite(targetTime))
            return 0;
        if (clip.duration > kEpsilon)
        {
            sourceTime = std::clamp(sourceTime, 0.0f, clip.duration);
            targetTime = std::clamp(targetTime, 0.0f, clip.duration);
        }
        else
        {
            sourceTime = std::max(sourceTime, 0.0f);
            targetTime = std::max(targetTime, 0.0f);
        }

        // Snapshot both poses before writing. Apart from making an in-place paste safe, this keeps parent solves
        // independent of channel order.
        const AnimationClip sourceClip = clip;
        std::vector<mat4> sourceGlobals;
        std::vector<mat4> targetGlobals;
        SampleGlobalTransforms(sourceClip, skeleton, sourceTime, sourceGlobals);
        SampleGlobalTransforms(sourceClip, skeleton, targetTime, targetGlobals);
        if (sourceGlobals.size() != skeleton.bones.size() || targetGlobals.size() != skeleton.bones.size() ||
            !std::all_of(sourceGlobals.begin(), sourceGlobals.end(), [](const mat4 &transform)
                         { return Finite(transform); }) ||
            !std::all_of(targetGlobals.begin(), targetGlobals.end(), [](const mat4 &transform)
                         { return Finite(transform); }))
            return 0;

        const mat4 inverseRoot = glm::inverse(skeleton.rootTransform);
        mat4 reflection(1.0f);
        reflection[0][0] = -1.0f;

        std::vector<Sample> samples;
        samples.reserve(sourceClip.channels.size());
        for (const AnimationChannel &source : sourceClip.channels)
        {
            if (!BoneSelected(boneIndices, source.boneIndex))
                continue;
            const int targetBone = MirroredBone(skeleton, source.boneIndex, includeCenterBones);
            if (targetBone < 0)
                continue;

            Sample sample;
            sample.targetBone = targetBone;
            if (HasChannel(channels, ChannelMask::Position) && !source.positionKeys.empty())
            {
                sample.hasPosition = true;
                sample.positionInterpolation = InterpolationAt(source.positionKeys, sourceTime);
            }
            if (HasChannel(channels, ChannelMask::Rotation) && !source.rotationKeys.empty())
            {
                sample.hasRotation = true;
                sample.rotationInterpolation = InterpolationAt(source.rotationKeys, sourceTime);
            }
            if (HasChannel(channels, ChannelMask::Scale) && !source.scaleKeys.empty())
            {
                sample.hasScale = true;
                sample.scaleInterpolation = InterpolationAt(source.scaleKeys, sourceTime);
            }
            if (sample.hasPosition || sample.hasRotation || sample.hasScale)
            {
                const mat4 sourceRig = inverseRoot * sourceGlobals[source.boneIndex];
                sample.desiredGlobal = skeleton.rootTransform * reflection * sourceRig * reflection;
                samples.push_back(sample);
            }
        }

        auto depth = [&](int bone)
        {
            int result = 0;
            for (int guard = 0; guard < skeleton.GetBoneCount() && bone >= 0; ++guard)
            {
                bone = skeleton.bones[bone].parentIndex;
                ++result;
            }
            return result;
        };
        std::stable_sort(samples.begin(), samples.end(), [&](const Sample &left, const Sample &right)
                         { return depth(left.targetBone) < depth(right.targetBone); });

        // finalGlobals tracks the pose that the written local keys will actually produce. Children therefore solve
        // through a mirrored parent when it is part of the paste, or through the untouched current parent otherwise.
        std::vector<mat4> finalGlobals = targetGlobals;
        struct SolvedSample
        {
            Sample sample;
            vec3 position = vec3(0.0f);
            quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
            vec3 scale = vec3(1.0f);
        };
        std::vector<SolvedSample> solved;
        solved.reserve(samples.size());
        for (const Sample &sample : samples)
        {
            const BoneInfo &bone = skeleton.bones[sample.targetBone];
            const mat4 parentGlobal = bone.parentIndex >= 0 ? finalGlobals[bone.parentIndex] : mat4(1.0f);
            const mat4 channelTransform = glm::inverse(bone.intermediatePrefix) *
                                          glm::inverse(parentGlobal) * sample.desiredGlobal;

            SolvedSample output;
            output.sample = sample;
            AnimationEvaluator::BindPose(bone, output.position, output.rotation, output.scale);
            if (const AnimationChannel *target = FindChannel(sourceClip, sample.targetBone))
                AnimationEvaluator::SampleChannel(*target,
                                                  bone,
                                                  targetTime,
                                                  output.position,
                                                  output.rotation,
                                                  output.scale);
            if (sample.hasPosition)
                output.position = vec3(channelTransform[3]);
            if (sample.hasRotation)
                output.rotation = MatrixRotation(channelTransform);
            if (sample.hasScale)
                output.scale = MatrixScale(channelTransform);
            if (!Finite(output.position) || !Finite(output.rotation) || !Finite(output.scale))
                return 0;

            const mat4 finalLocal = bone.intermediatePrefix *
                                    ComposeTransform(output.position, output.rotation, output.scale);
            finalGlobals[sample.targetBone] = parentGlobal * finalLocal;
            solved.push_back(output);
        }

        size_t writes = 0;
        for (const SolvedSample &output : solved)
        {
            const Sample &sample = output.sample;
            AnimationChannel &target = FindOrAddChannel(clip, sample.targetBone);
            if (sample.hasPosition)
                writes += UpsertKey(target.positionKeys,
                                    targetTime,
                                    output.position,
                                    sample.positionInterpolation);
            if (sample.hasRotation)
            {
                quat rotation = output.rotation;
                const auto next = std::lower_bound(target.rotationKeys.begin(),
                                                   target.rotationKeys.end(),
                                                   targetTime - kEpsilon,
                                                   [](const RotationKey &key, float time)
                                                   { return key.time < time; });
                if (next != target.rotationKeys.begin())
                    rotation = SameHemisphere(std::prev(next)->value, rotation);
                writes += UpsertKey(target.rotationKeys,
                                    targetTime,
                                    rotation,
                                    sample.rotationInterpolation);
            }
            if (sample.hasScale)
                writes += UpsertKey(target.scaleKeys,
                                    targetTime,
                                    output.scale,
                                    sample.scaleInterpolation);
        }
        return writes;
    }

    size_t CycleInterval(AnimationClip &clip,
                         const Skeleton &skeleton,
                         int carrierBone,
                         float startTime,
                         float endTime,
                         float stepTicks)
    {
        if (!std::isfinite(startTime) || !std::isfinite(endTime) || !std::isfinite(stepTicks) || stepTicks <= kEpsilon ||
            startTime < 0.f || endTime - startTime <= stepTicks + kEpsilon ||
            static_cast<double>(endTime - startTime) / stepTicks > static_cast<double>(kMaxTweenSamples) ||
            clip.ticksPerSecond <= kEpsilon)
            return 0;
        for (const AnimationChannel &channel : clip.channels)
            if (!FiniteKeys(channel.positionKeys) || !FiniteKeys(channel.rotationKeys) || !FiniteKeys(channel.scaleKeys))
                return 0;
        if (!clip.rootMotion.Empty() && !FiniteKeys(clip.rootMotion.positionKeys))
            return 0;
        const float tolerance = std::max(kEpsilon, stepTicks * 0.001f);
        const float span = endTime - startTime;
        const float newEnd = endTime + span;
        const AnimationClip source = clip;
        // the paste clamps its target to the duration, so the clip grows first; the seam is read from the source
        // before the mirrored copy lands on it
        clip.duration = std::max(clip.duration, newEnd);
        const AnimationChannel *carrier = carrierBone >= 0 ? FindChannel(source, carrierBone) : nullptr;
        const bool travels = carrier && !carrier->positionKeys.empty();
        const vec3 seam = travels ? AnimationEvaluator::InterpolatePosition(carrier->positionKeys, endTime) : vec3(0.f);
        // the seam keeps the stride's end pose (a symmetric stride ends on the mirrored start anyway; an
        // asymmetric one must not have its last segment rewritten), so the mirrored half starts one frame after it
        std::vector<float> offsets;
        for (size_t step = 1; step < kMaxTweenSamples; ++step)
        {
            const float t = static_cast<float>(step) * stepTicks;
            if (t >= span - tolerance)
                break;
            offsets.push_back(t);
        }
        offsets.push_back(span);
        // pasted from the end down: every source time is at or before the seam and every target after it, so no
        // pasted key is ever read back as a source
        size_t writes = 0;
        for (size_t i = offsets.size(); i-- > 0;)
            writes += PasteMirroredPose(clip, skeleton, startTime + offsets[i], endTime + offsets[i], {}, true, ChannelMask::All);
        if (writes == 0)
        {
            clip = source; // the duration grew before the pastes: nothing written, nothing kept
            return 0;
        }
        // the loop closes on the pose the stride started from
        for (const AnimationChannel &from : source.channels)
        {
            AnimationChannel *to = nullptr;
            for (AnimationChannel &candidate : clip.channels)
                if (candidate.boneIndex == from.boneIndex)
                    to = &candidate;
            if (!to)
                continue;
            if (!from.rotationKeys.empty())
            {
                quat value = AnimationEvaluator::InterpolateRotation(from.rotationKeys, startTime);
                if (!to->rotationKeys.empty())
                {
                    // the neighbour is the key at or before the new end - the clip may run on past it
                    const auto *previous = &to->rotationKeys.front();
                    for (const auto &key : to->rotationKeys)
                        if (key.time <= newEnd + tolerance)
                            previous = &key;
                    value = SameHemisphere(previous->value, value);
                }
                writes += UpsertKey(to->rotationKeys, newEnd, value, InterpolationAt(from.rotationKeys, startTime));
            }
            if (!from.scaleKeys.empty())
                writes += UpsertKey(to->scaleKeys, newEnd, AnimationEvaluator::InterpolateScale(from.scaleKeys, startTime),
                                    InterpolationAt(from.scaleKeys, startTime));
        }
        // the body keeps travelling: the mirrored half continues from where the stride ended, so every pasted
        // carrier key is offset by the seam minus the mirrored start (read from a scratch paste, the seam itself
        // is never rewritten)
        if (travels)
        {
            AnimationClip scratch = source;
            scratch.duration = clip.duration;
            const int carrierIndices[] = {carrierBone};
            PasteMirroredPose(scratch, skeleton, startTime, endTime, carrierIndices, true, ChannelMask::Position);
            const AnimationChannel *mirrored = FindChannel(scratch, carrierBone);
            const vec3 offset = mirrored && !mirrored->positionKeys.empty()
                                    ? seam - AnimationEvaluator::InterpolatePosition(mirrored->positionKeys, endTime)
                                    : vec3(0.f);
            for (AnimationChannel &channel : clip.channels)
                if (channel.boneIndex == carrierBone)
                    for (PositionKey &key : channel.positionKeys)
                        if (key.time > endTime + tolerance && key.time <= newEnd + tolerance)
                            key.value += offset;
        }
        if (!source.rootMotion.Empty())
        {
            const vec3 rootStart = AnimationEvaluator::InterpolatePosition(source.rootMotion.positionKeys, startTime);
            const vec3 rootSeam = AnimationEvaluator::InterpolatePosition(source.rootMotion.positionKeys, endTime);
            for (float offset : offsets)
            {
                const float sourceTime = startTime + offset;
                writes += UpsertKey(clip.rootMotion.positionKeys,
                                    endTime + offset,
                                    rootSeam + AnimationEvaluator::InterpolatePosition(source.rootMotion.positionKeys, sourceTime) -
                                        rootStart,
                                    InterpolationAt(source.rootMotion.positionKeys, sourceTime));
            }
        }
        return writes;
    }

    SpringBakeResult BakeSecondarySpring(AnimationClip &clip,
                                         const Skeleton &skeleton,
                                         std::span<const int> orderedBoneIndices,
                                         const SpringBakeSettings &settings)
    {
        SpringBakeResult result;
        if (orderedBoneIndices.empty())
        {
            result.status = SpringBakeStatus::EmptyChain;
            return result;
        }
        if (!std::isfinite(clip.duration) || !std::isfinite(clip.ticksPerSecond) ||
            clip.duration <= kEpsilon || clip.ticksPerSecond <= kEpsilon)
        {
            result.status = SpringBakeStatus::InvalidClipTiming;
            return result;
        }
        // The bake covers [rangeStart, rangeEnd] in ticks: the whole clip by default, or the Timeline's interval.
        // A partial range keeps every key outside it, so Cyclic - which closes the clip's own seam - is refused there.
        const float rangeStart = settings.startTime;
        const float rangeEnd = settings.endTime < 0.0f ? clip.duration : settings.endTime;
        const bool wholeClip = rangeStart <= kEpsilon && rangeEnd >= clip.duration - kEpsilon;
        if (!std::isfinite(rangeStart) || !std::isfinite(rangeEnd) || rangeStart < 0.0f ||
            rangeEnd > clip.duration + kEpsilon || rangeEnd - rangeStart <= kEpsilon ||
            (!wholeClip && settings.endpointMode == SpringEndpointMode::Cyclic))
        {
            result.status = SpringBakeStatus::InvalidSettings;
            return result;
        }
        if (!std::isfinite(settings.framesPerSecond) || !std::isfinite(settings.frameStep) ||
            !std::isfinite(settings.stiffness) || !std::isfinite(settings.damping) ||
            !std::isfinite(settings.response) || !std::isfinite(settings.drag) ||
            settings.framesPerSecond < 1.0f || settings.framesPerSecond > kMaxSpringFramesPerSecond ||
            settings.frameStep < 0.01f || settings.frameStep > kMaxSpringFrameStep ||
            settings.stiffness < 0.0f || settings.stiffness > kMaxSpringStiffness ||
            settings.damping < 0.0f || settings.damping > kMaxSpringDamping ||
            settings.response < 0.0f || settings.response > kMaxSpringResponse ||
            settings.drag < 0.0f || settings.drag > kMaxSpringDrag ||
            (settings.endpointMode != SpringEndpointMode::Free &&
             settings.endpointMode != SpringEndpointMode::PreserveSource &&
             settings.endpointMode != SpringEndpointMode::Cyclic) ||
            settings.cyclicWarmupCycles < 0 || settings.cyclicWarmupCycles > 32)
        {
            result.status = SpringBakeStatus::InvalidSettings;
            return result;
        }

        for (size_t i = 0; i < orderedBoneIndices.size(); ++i)
        {
            const int boneIndex = orderedBoneIndices[i];
            if (boneIndex < 0 || boneIndex >= skeleton.GetBoneCount())
            {
                result.status = SpringBakeStatus::InvalidBoneIndex;
                return result;
            }
            if (i > 0 && skeleton.bones[boneIndex].parentIndex != orderedBoneIndices[i - 1])
            {
                result.status = SpringBakeStatus::NonContiguousChain;
                return result;
            }
        }

        // Samples sit on the frame grid the settings describe: the first grid frame past the range start, then
        // every step, and the range end itself last, so an interval keys the frames Tween would.
        const float stepTicks = clip.ticksPerSecond / settings.framesPerSecond * settings.frameStep;
        const float tolerance = std::max(kEpsilon, stepTicks * 0.001f);
        std::vector<float> sampleTimes;
        const double first = std::ceil((rangeStart + tolerance) / stepTicks) * stepTicks;
        for (double time = first; time < rangeEnd - tolerance; time += stepTicks)
        {
            if (sampleTimes.size() >= kMaxSpringSamples)
            {
                result.status = SpringBakeStatus::InvalidSettings;
                return result;
            }
            sampleTimes.push_back(static_cast<float>(time));
        }
        sampleTimes.push_back(rangeEnd);
        const double stepSecondsDouble = static_cast<double>(stepTicks) / clip.ticksPerSecond;
        const double requestedSubsteps = std::ceil(stepSecondsDouble * 120.0);
        const double workItems = static_cast<double>(sampleTimes.size()) * std::max(requestedSubsteps, 1.0) *
                                 static_cast<double>(settings.cyclicWarmupCycles + 1) * orderedBoneIndices.size();
        if (!std::isfinite(stepSecondsDouble) || stepSecondsDouble <= 0.0 ||
            stepSecondsDouble > std::numeric_limits<float>::max() || !std::isfinite(requestedSubsteps) ||
            requestedSubsteps > kMaxSpringSubsteps || !std::isfinite(workItems) ||
            workItems > kMaxSpringWorkItems)
        {
            result.status = SpringBakeStatus::InvalidSettings;
            return result;
        }
        const size_t substeps = std::max<size_t>(static_cast<size_t>(requestedSubsteps), 1);
        auto secondsBefore = [&](size_t sample)
        {
            return (sampleTimes[sample] - (sample == 0 ? rangeStart : sampleTimes[sample - 1])) / clip.ticksPerSecond;
        };

        struct SpringState
        {
            quat worldRotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
            vec3 angularVelocity = vec3(0.0f);
            vec3 parentTargetVelocity = vec3(0.0f);
        };

        // Keep source evaluation independent from the channels replaced by the bake.
        const AnimationClip source = clip;
        auto finiteTargets = [&](const std::vector<quat> &targets)
        {
            return targets.size() == skeleton.bones.size() &&
                   std::all_of(targets.begin(), targets.end(), [](const quat &rotation)
                               { return Finite(rotation); });
        };
        // The chain starts on the source pose at the range start with no angular velocity of its own; the target
        // velocity history comes from one step earlier, so the first sample already sees the source moving.
        std::vector<quat> startTargets, leadTargets, currentTargets;
        SampleGlobalRotations(source, skeleton, rangeStart, startTargets);
        SampleGlobalRotations(source, skeleton, std::max(0.0f, rangeStart - stepTicks), leadTargets);
        if (!finiteTargets(startTargets) || !finiteTargets(leadTargets))
        {
            result.status = SpringBakeStatus::InvalidSettings;
            return result;
        }
        std::vector<quat> previousTargets = leadTargets;
        std::vector<SpringState> states(orderedBoneIndices.size());
        for (size_t i = 0; i < states.size(); ++i)
        {
            const int parentIndex = skeleton.bones[orderedBoneIndices[i]].parentIndex;
            states[i].worldRotation = startTargets[orderedBoneIndices[i]];
            if (parentIndex >= 0)
                states[i].parentTargetVelocity = WorldRotationVector(leadTargets[parentIndex], startTargets[parentIndex]) /
                                                 static_cast<float>(stepSecondsDouble);
        }

        auto advance = [&](float targetTime, float seconds)
        {
            if (!std::isfinite(seconds) || seconds <= 0.0f)
                return false;
            SampleGlobalRotations(source, skeleton, targetTime, currentTargets);
            if (!finiteTargets(currentTargets))
                return false;
            const size_t steps = std::clamp<size_t>(static_cast<size_t>(std::ceil(seconds * 120.0f)), 1, substeps);
            const float substepSeconds = seconds / static_cast<float>(steps);
            for (size_t i = 0; i < states.size(); ++i)
            {
                const int boneIndex = orderedBoneIndices[i];
                const int parentIndex = skeleton.bones[boneIndex].parentIndex;
                const vec3 targetVelocity = WorldRotationVector(previousTargets[boneIndex],
                                                                currentTargets[boneIndex]) /
                                            seconds;
                const vec3 parentVelocity = parentIndex >= 0
                                                ? WorldRotationVector(previousTargets[parentIndex],
                                                                      currentTargets[parentIndex]) /
                                                      seconds
                                                : vec3(0.0f);
                const vec3 parentAcceleration = (parentVelocity - states[i].parentTargetVelocity) / seconds;
                states[i].parentTargetVelocity = parentVelocity;
                const float depthResponse = settings.response / (1.0f + settings.drag * static_cast<float>(i));
                if (!Finite(targetVelocity) || !Finite(parentVelocity) || !Finite(parentAcceleration) ||
                    !std::isfinite(depthResponse))
                    return false;

                for (size_t substep = 0; substep < steps; ++substep)
                {
                    const vec3 error = WorldRotationVector(states[i].worldRotation, currentTargets[boneIndex]);
                    const vec3 acceleration = settings.stiffness * error +
                                              settings.damping * (depthResponse * targetVelocity -
                                                                  states[i].angularVelocity) -
                                              settings.drag * static_cast<float>(i + 1) * parentAcceleration;
                    if (!Finite(error) || !Finite(acceleration))
                        return false;
                    states[i].angularVelocity += acceleration * substepSeconds;
                    if (!Finite(states[i].angularVelocity))
                        return false;
                    states[i].worldRotation = ApplyWorldRotation(states[i].worldRotation,
                                                                 states[i].angularVelocity * substepSeconds);
                    if (!Finite(states[i].worldRotation))
                        return false;
                }
                const float lag = RotationDistanceDegrees(states[i].worldRotation, currentTargets[boneIndex]);
                if (!std::isfinite(lag))
                    return false;
                result.maxAngularLagDegrees = std::max(result.maxAngularLagDegrees, lag);
            }
            previousTargets = currentTargets;
            return true;
        };

        if (settings.endpointMode == SpringEndpointMode::Cyclic)
        {
            for (int cycle = 0; cycle < settings.cyclicWarmupCycles; ++cycle)
            {
                previousTargets = leadTargets;
                for (size_t sample = 0; sample < sampleTimes.size(); ++sample)
                    if (!advance(sampleTimes[sample], secondsBefore(sample)))
                    {
                        result.status = SpringBakeStatus::InvalidSettings;
                        return result;
                    }
            }
            previousTargets = leadTargets;
            result.maxAngularLagDegrees = 0.0f;
        }

        std::vector<std::vector<RotationKey>> bakedKeys(orderedBoneIndices.size());
        for (std::vector<RotationKey> &keys : bakedKeys)
            keys.reserve(sampleTimes.size() + 1);

        auto appendKeys = [&](float time, const std::vector<quat> &targetGlobals)
        {
            if (!std::isfinite(time) || targetGlobals.size() != skeleton.bones.size())
                return false;
            for (size_t i = 0; i < states.size(); ++i)
            {
                const int boneIndex = orderedBoneIndices[i];
                const int parentIndex = skeleton.bones[boneIndex].parentIndex;
                const quat parentWorld = i > 0              ? states[i - 1].worldRotation
                                         : parentIndex >= 0 ? targetGlobals[parentIndex]
                                                            : quat(1.0f, 0.0f, 0.0f, 0.0f);
                const quat actualLocal = SafeNormalized(glm::conjugate(parentWorld) * states[i].worldRotation);
                const quat prefixRotation = MatrixRotation(skeleton.bones[boneIndex].intermediatePrefix);
                quat channelRotation = SafeNormalized(glm::conjugate(prefixRotation) * actualLocal);
                if (!bakedKeys[i].empty())
                    channelRotation = SameHemisphere(bakedKeys[i].back().value, channelRotation);
                if (!Finite(channelRotation))
                    return false;
                bakedKeys[i].push_back({time, channelRotation, AnimationInterpolation::Linear});
            }
            return true;
        };

        if (!appendKeys(rangeStart, startTargets))
        {
            result.status = SpringBakeStatus::InvalidSettings;
            return result;
        }
        for (size_t sample = 0; sample < sampleTimes.size(); ++sample)
        {
            if (!advance(sampleTimes[sample], secondsBefore(sample)) || !appendKeys(sampleTimes[sample], currentTargets))
            {
                result.status = SpringBakeStatus::InvalidSettings;
                return result;
            }
        }

        for (size_t i = 0; i < bakedKeys.size(); ++i)
        {
            if (settings.endpointMode == SpringEndpointMode::PreserveSource)
            {
                bakedKeys[i].front().value = SampleLocalRotation(source, skeleton, orderedBoneIndices[i], rangeStart);
                bakedKeys[i].back().value = SampleLocalRotation(source, skeleton, orderedBoneIndices[i], rangeEnd);
            }
            else if (settings.endpointMode == SpringEndpointMode::Cyclic)
            {
                bakedKeys[i].back().value = bakedKeys[i].front().value;
            }
            for (size_t key = 1; key < bakedKeys[i].size(); ++key)
                bakedKeys[i][key].value = SameHemisphere(bakedKeys[i][key - 1].value,
                                                         bakedKeys[i][key].value);
            if (!FiniteKeys(bakedKeys[i]))
            {
                result.status = SpringBakeStatus::InvalidSettings;
                return result;
            }
        }

        for (size_t i = 0; i < bakedKeys.size(); ++i)
        {
            AnimationChannel &channel = FindOrAddChannel(clip, orderedBoneIndices[i]);
            std::vector<RotationKey> &keys = channel.rotationKeys;
            if (wholeClip)
            {
                keys = std::move(bakedKeys[i]);
                result.keysWritten += keys.size();
                continue;
            }
            // A partial range replaces only its interior: the keys outside stay, and the seam keys land with the
            // values the source shows there. The hemisphere pass then runs from the last key before the range to the
            // end of the curve so the sign stays continuous into and out of the bake (a sign is not a rotation).
            std::erase_if(keys, [&](const RotationKey &key)
                          { return key.time > rangeStart + tolerance && key.time < rangeEnd - tolerance; });
            // The start key is rewritten Linear like Ballistic (interpolation is outgoing); the end key is only
            // inserted when the curve has none there, with the mode the source played into it, so an existing one
            // keeps its value and mode and the segment after the range plays as before.
            const AnimationChannel *sourceChannel = FindChannel(source, orderedBoneIndices[i]);
            const std::vector<RotationKey> noKeys;
            const std::vector<RotationKey> &sourceKeys = sourceChannel ? sourceChannel->rotationKeys : noKeys;
            for (size_t key = 0; key + 1 < bakedKeys[i].size(); ++key)
                result.keysWritten += UpsertKey(keys, bakedKeys[i][key].time, bakedKeys[i][key].value,
                                                bakedKeys[i][key].interpolation);
            if (!HasKeyAt(keys, rangeEnd, tolerance))
                result.keysWritten += UpsertKey(keys, rangeEnd, bakedKeys[i].back().value, InterpolationAt(sourceKeys, rangeEnd));
            size_t from = 0;
            while (from < keys.size() && keys[from].time < rangeStart - tolerance)
                ++from;
            for (size_t key = std::max<size_t>(from, 1); key < keys.size(); ++key)
                keys[key].value = SameHemisphere(keys[key - 1].value, keys[key].value);
        }
        result.bonesBaked = orderedBoneIndices.size();
        result.sampleCount = sampleTimes.size() + 1;
        result.sampleStepTicks = stepTicks;
        return result;
    }

    WorldDriftResult AnalyzeWorldPositionDrift(const AnimationClip &clip,
                                               const Skeleton &skeleton,
                                               int boneIndex,
                                               float startTime,
                                               float endTime,
                                               float sampleStepTicks)
    {
        WorldDriftResult result;
        result.boneIndex = boneIndex;
        if (boneIndex < 0 || boneIndex >= skeleton.GetBoneCount())
        {
            result.status = WorldDriftStatus::InvalidBoneIndex;
            return result;
        }
        if (!std::isfinite(clip.duration) || clip.duration <= kEpsilon ||
            !std::isfinite(startTime) || !std::isfinite(endTime) || !std::isfinite(sampleStepTicks) ||
            sampleStepTicks <= kEpsilon)
        {
            result.status = WorldDriftStatus::InvalidRange;
            return result;
        }
        startTime = std::clamp(startTime, 0.0f, clip.duration);
        endTime = std::clamp(endTime, 0.0f, clip.duration);
        if (endTime - startTime <= kEpsilon)
        {
            result.status = WorldDriftStatus::InvalidRange;
            return result;
        }

        const double requestedIntervals = std::ceil(static_cast<double>(endTime - startTime) / sampleStepTicks);
        if (!std::isfinite(requestedIntervals) || requestedIntervals > 100000.0)
        {
            result.status = WorldDriftStatus::InvalidRange;
            return result;
        }
        const size_t intervals = std::max<size_t>(static_cast<size_t>(requestedIntervals), 1);
        const float step = (endTime - startTime) / static_cast<float>(intervals);
        std::vector<mat4> transforms;
        SampleGlobalTransforms(clip, skeleton, startTime, transforms);
        if (transforms.size() != static_cast<size_t>(skeleton.GetBoneCount()) ||
            !std::all_of(transforms.begin(), transforms.end(), [](const mat4 &transform)
                         { return Finite(transform); }))
        {
            result.status = WorldDriftStatus::InvalidAnimationData;
            return result;
        }
        const vec3 reference = vec3(transforms[boneIndex][3]);
        for (size_t sample = 0; sample <= intervals; ++sample)
        {
            const float time = sample == intervals ? endTime : startTime + step * static_cast<float>(sample);
            SampleGlobalTransforms(clip, skeleton, time, transforms);
            if (transforms.size() != static_cast<size_t>(skeleton.GetBoneCount()) ||
                !std::all_of(transforms.begin(), transforms.end(), [](const mat4 &transform)
                             { return Finite(transform); }))
            {
                result.status = WorldDriftStatus::InvalidAnimationData;
                return result;
            }
            const float drift = glm::length(vec3(transforms[boneIndex][3]) - reference);
            if (!std::isfinite(drift))
            {
                result.status = WorldDriftStatus::InvalidAnimationData;
                return result;
            }
            result.maxDrift = std::max(result.maxDrift, drift);
        }
        result.maxRemainingDrift = result.maxDrift;
        result.sampleCount = intervals + 1;
        return result;
    }

    WorldDriftResult StabilizeWorldPosition(AnimationClip &clip,
                                            const Skeleton &skeleton,
                                            int boneIndex,
                                            float startTime,
                                            float endTime,
                                            float sampleStepTicks,
                                            int compensationBoneIndex)
    {
        WorldDriftResult result = AnalyzeWorldPositionDrift(clip,
                                                            skeleton,
                                                            boneIndex,
                                                            startTime,
                                                            endTime,
                                                            sampleStepTicks);
        if (!result)
            return result;

        auto isAncestorOrSelf = [&](int candidate)
        {
            int current = boneIndex;
            for (int guard = 0; guard < skeleton.GetBoneCount() && current >= 0; ++guard)
            {
                if (current == candidate)
                    return true;
                current = skeleton.bones[current].parentIndex;
            }
            return false;
        };

        if (compensationBoneIndex >= 0 &&
            (compensationBoneIndex >= skeleton.GetBoneCount() || !isAncestorOrSelf(compensationBoneIndex)))
        {
            result.status = WorldDriftStatus::InvalidCompensationBone;
            return result;
        }
        if (compensationBoneIndex < 0)
        {
            int root = boneIndex;
            int current = skeleton.bones[boneIndex].parentIndex;
            if (current < 0)
                current = boneIndex;
            for (int guard = 0; guard < skeleton.GetBoneCount() && current >= 0; ++guard)
            {
                root = current;
                const AnimationChannel *channel = FindChannel(clip, current);
                if (channel && !channel->positionKeys.empty())
                {
                    compensationBoneIndex = current;
                    break;
                }
                current = skeleton.bones[current].parentIndex;
            }
            if (compensationBoneIndex < 0)
                compensationBoneIndex = root;
        }
        result.compensationBoneIndex = compensationBoneIndex;

        startTime = std::clamp(startTime, 0.0f, clip.duration);
        endTime = std::clamp(endTime, 0.0f, clip.duration);
        const size_t intervals = result.sampleCount - 1;
        const float step = (endTime - startTime) / static_cast<float>(intervals);
        const AnimationClip source = clip;
        std::vector<mat4> transforms;
        SampleGlobalTransforms(source, skeleton, startTime, transforms);
        if (transforms.size() != static_cast<size_t>(skeleton.GetBoneCount()) ||
            !std::all_of(transforms.begin(), transforms.end(), [](const mat4 &transform)
                         { return Finite(transform); }))
        {
            result.status = WorldDriftStatus::InvalidAnimationData;
            return result;
        }
        const vec3 reference = vec3(transforms[boneIndex][3]);
        std::vector<PositionKey> compensationKeys;
        compensationKeys.reserve(result.sampleCount);

        for (size_t sample = 0; sample <= intervals; ++sample)
        {
            const float time = sample == intervals ? endTime : startTime + step * static_cast<float>(sample);
            SampleGlobalTransforms(source, skeleton, time, transforms);
            if (transforms.size() != static_cast<size_t>(skeleton.GetBoneCount()) ||
                !std::all_of(transforms.begin(), transforms.end(), [](const mat4 &transform)
                             { return Finite(transform); }))
            {
                result.status = WorldDriftStatus::InvalidAnimationData;
                return result;
            }
            const int parentIndex = skeleton.bones[compensationBoneIndex].parentIndex;
            const mat4 parentTransform = parentIndex >= 0 ? transforms[parentIndex] : mat4(1.0f);
            const mat3 worldFromPosition = mat3(parentTransform *
                                                skeleton.bones[compensationBoneIndex].intermediatePrefix);
            const float determinant = glm::determinant(worldFromPosition);
            if (!std::isfinite(determinant) || std::abs(determinant) <= kEpsilon)
            {
                result.status = WorldDriftStatus::NonInvertibleParentTransform;
                return result;
            }

            vec3 position, scale;
            quat rotation;
            if (const AnimationChannel *channel = FindChannel(source, compensationBoneIndex))
                AnimationEvaluator::SampleChannel(*channel,
                                                  skeleton.bones[compensationBoneIndex],
                                                  time,
                                                  position,
                                                  rotation,
                                                  scale);
            else
                AnimationEvaluator::BindPose(skeleton.bones[compensationBoneIndex], position, rotation, scale);
            if (!Finite(position) || !Finite(rotation) || !Finite(scale))
            {
                result.status = WorldDriftStatus::InvalidAnimationData;
                return result;
            }

            const vec3 worldDelta = reference - vec3(transforms[boneIndex][3]);
            position += glm::inverse(worldFromPosition) * worldDelta;
            if (!Finite(position))
            {
                result.status = WorldDriftStatus::InvalidAnimationData;
                return result;
            }
            compensationKeys.push_back({time, position, AnimationInterpolation::Linear});
        }

        AnimationClip candidate = clip;
        AnimationChannel &channel = FindOrAddChannel(candidate, compensationBoneIndex);
        for (const PositionKey &key : compensationKeys)
            result.keysWritten += UpsertKey(channel.positionKeys,
                                            key.time,
                                            key.value,
                                            key.interpolation);
        const WorldDriftResult remaining = AnalyzeWorldPositionDrift(candidate,
                                                                     skeleton,
                                                                     boneIndex,
                                                                     startTime,
                                                                     endTime,
                                                                     sampleStepTicks);
        if (!remaining)
        {
            result.status = remaining.status;
            result.keysWritten = 0;
            return result;
        }
        result.maxRemainingDrift = remaining.maxDrift;
        clip = std::move(candidate);
        return result;
    }
} // namespace pe::AnimationClipTools
