#pragma once

namespace pe
{
    struct BoneInfo
    {
        std::string name;
        int parentIndex = -1;
        mat4 offsetMatrix = mat4(1.f);
        mat4 localBindTransform = mat4(1.f);
        mat4 intermediatePrefix = mat4(1.f);
    };

    struct Skeleton
    {
        std::vector<BoneInfo> bones;
        std::unordered_map<std::string, int> boneNameToIndex;
        // The transform baked into root bones' intermediatePrefix (e.g. glTF root node
        // rotation).  Used to strip that transform from joint matrices before upload so
        // the shader's worldMatrix can apply it instead, allowing user transforms to
        // affect skinned meshes.
        mat4 rootTransform = mat4(1.f);

        int GetBoneIndex(const std::string &name) const
        {
            auto it = boneNameToIndex.find(name);
            return (it != boneNameToIndex.end()) ? it->second : -1;
        }

        int GetBoneCount() const { return static_cast<int>(bones.size()); }
    };

    enum class AnimationInterpolation : uint8_t
    {
        Linear = 0,
        Smooth,
        Stepped
    };

    template <typename T>
    struct AnimationKey
    {
        float time;
        T value;
        // Controls the segment from this key to the next key.
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    };

    using PositionKey = AnimationKey<vec3>;
    using RotationKey = AnimationKey<quat>;
    using ScaleKey = AnimationKey<vec3>;

    struct AnimationChannel
    {
        int boneIndex = -1;
        std::vector<PositionKey> positionKeys;
        std::vector<RotationKey> rotationKeys;
        std::vector<ScaleKey> scaleKeys;
    };

    // Travel removed from one bone's Location curve. The regular pose stays in place while this is present;
    // baking the track puts the travel back into the bone. Runtime pose evaluation intentionally ignores it.
    struct RootMotionTrack
    {
        int boneIndex = -1;
        std::vector<PositionKey> positionKeys;

        bool Empty() const { return boneIndex < 0 || positionKeys.empty(); }
    };

    // A named frame on the clip's ruler: a footstep, a hit frame, a phase boundary. Scripts read them through
    // animation.get_markers; playback itself never acts on them.
    struct ClipMarker
    {
        float time = 0.0f; // clip ticks
        std::string name;
    };

    struct AnimationClip
    {
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 25.0f;
        std::vector<AnimationChannel> channels;
        RootMotionTrack rootMotion;
        std::vector<ClipMarker> markers;
    };
} // namespace pe
