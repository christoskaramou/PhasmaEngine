#pragma once

#include "Renderer/Compute.h"

namespace pe
{
    class Camera : public IComponent
    {
    public:
        struct Plane
        {
            float normal[3];
            float d;
        };

        mat4 view, previousView;
        mat4 projection, previousProjection, projectionNoJitter;
        mat4 viewProjection, previousViewProjection;
        mat4 invView, invProjection, invViewProjection;
        quat orientation;
        vec3 position, euler, worldOrientation;
        vec3 front, right, up;
        float nearPlane, farPlane, fovx, speed, rotationSpeed;
        std::vector<Plane> frustum{};
        Compute frustumCompute;
        vec2 projJitter;
        vec2 prevProjJitter;

        Camera();

        void Update();

        void UpdatePerspective();

        // In radians
        inline float FovyToFovx(float fovy) { return 2.0f * atan(tan(fovy * 0.5f) * GetAspect()); }

        // In radians
        inline float FovxToFovy(float fovx) { return 2.0f * atan(tan(fovx * 0.5f) / GetAspect()); }

        inline float Fovy() { return 2.0f * atan(tan(fovx * 0.5f) / GetAspect()); }

        float GetAspect();

        void UpdateView();

        vec3 WorldFront() const;

        vec3 WorldRight() const;

        vec3 WorldUp() const;

        void Move(CameraDirection direction, float velocity);

        void Rotate(float xoffset, float yoffset);

        void ExtractFrustum();

        bool PointInFrustum(const vec3 &point, float radius) const;

        bool AABBInFrustum(const AABB &aabb) const;

        void ReCreateComputePipelines();

        void Destroy() override;
    };
}
