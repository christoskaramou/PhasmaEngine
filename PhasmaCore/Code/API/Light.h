#pragma once

namespace pe
{
    struct Light
    {
    };

    struct DirectionalLight
    {
        vec4 color; // .w = intensity
        vec4 direction;
    };

    struct PointLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = radius
    };

    struct SpotLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = range
        vec4 rotation; // .z = angle, .w = falloff
    };
} // namespace pe
