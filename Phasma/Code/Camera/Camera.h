/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Renderer/Compute.h"

namespace pe
{
    class Camera : public IComponent
    {
    public:
        enum class RelativeDirection
        {
            FORWARD,
            BACKWARD,
            LEFT,
            RIGHT
        };

        struct Plane
        {
            vec3 normal;
            float d;
        };

        mat4 view, previousView;
        mat4 projection, previousProjection;
        mat4 viewProjection, previousViewProjection;
        mat4 invView, invProjection, invViewProjection;
        quat orientation;
        vec3 position, euler, worldOrientation;
        vec3 front, right, up;
        float nearPlane, farPlane, FOV, speed, rotationSpeed;
        vec2 projOffset, projOffsetPrevious;
        std::vector <Plane> frustum{};
        Compute frustumCompute;

        Camera();

        void Update();

        void UpdatePerspective();

        // In radians
        inline float FovyToFovx(float fovy, float aspect)
        { return 2.0f * atan(tan(fovy * 0.5f) * aspect); }

        // In radians
        inline float FovxToFovy(float fovx, float aspect)
        { return 2.0f * atan(tan(fovx * 0.5f) / aspect); }

        void UpdateView();

        vec3 WorldFront() const;

        vec3 WorldRight() const;

        vec3 WorldUp() const;

        void Move(RelativeDirection direction, float velocity);

        void Rotate(float xoffset, float yoffset);

        void ExtractFrustum();

        bool PointInFrustum(const vec3 &point, float radius) const;

        bool AABBInFrustum(const AABB &aabb) const;

        void ReCreateComputePipelines();

        void Destroy() override;
    };
}
