#pragma once

namespace pe
{
    struct Light
    {
    };

    struct DirectionalLight : public Light
    {
        vec4 color; // .w = intensity
        vec4 direction;
    };

    struct PointLight : public Light
    {
        vec4 color; // .w = intensity
        vec4 position;
    };

    struct SpotLight : public Light
    {
        vec4 color; // .w = intensity
        vec4 start; // .w = radius;
        vec4 end;
    };
}
