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

        int GetBoneIndex(const std::string &name) const
        {
            auto it = boneNameToIndex.find(name);
            return (it != boneNameToIndex.end()) ? it->second : -1;
        }

        int GetBoneCount() const { return static_cast<int>(bones.size()); }
    };

    template <typename T>
    struct AnimationKey
    {
        float time;
        T value;
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

    struct AnimationClip
    {
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 25.0f;
        std::vector<AnimationChannel> channels;
    };
} // namespace pe
