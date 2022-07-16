#pragma once

#undef max
#undef min

namespace pe
{
    class Node;

    struct AnimationChannel
    {
        AnimationPathType path;
        Node *node;
        int32_t samplerIndex;
    };

    struct AnimationSampler
    {
        AnimationInterpolationType interpolation;
        std::vector<float> inputs;
        std::vector<vec4> outputsVec4;
    };

    struct Animation
    {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
        float start = std::numeric_limits<float>::max();
        float end = std::numeric_limits<float>::min();
    };
}