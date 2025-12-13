#pragma once

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

        Camera();

        void Update();
        void UpdatePerspective();
        inline float FovyToFovx(float fovy) { return 2.0f * atan(tan(fovy * 0.5f) * GetAspect()); } // In radians
        inline float FovxToFovy(float fovx) { return 2.0f * atan(tan(fovx * 0.5f) / GetAspect()); } // In radians
        inline float Fovy() { return 2.0f * atan(tan(m_fovx * 0.5f) / GetAspect()); }
        float GetAspect();
        void UpdateView();
        vec3 WorldFront() const;
        vec3 WorldRight() const;
        vec3 WorldUp() const;
        void Move(CameraDirection direction, float speed);
        void Rotate(float xoffset, float yoffset);
        void ExtractFrustum();
        bool PointInFrustum(const vec3 &point, float radius) const;
        bool AABBInFrustum(const AABB &aabb) const;
        void Destroy() override;
        inline mat4 GetView() const { return m_view; }
        inline mat4 GetProjection() const { return m_projection; }
        inline mat4 GetViewProjection() const { return m_viewProjection; }
        inline mat4 GetPreviousViewProjection() const { return m_previousViewProjection; }
        inline mat4 GetInvView() const { return m_invView; }
        inline mat4 GetInvProjection() const { return m_invProjection; }
        inline mat4 GetInvViewProjection() const { return m_invViewProjection; }
        inline vec3 GetPosition() const { return m_position; }
        inline vec3 GetFront() const { return m_front; }
        inline float GetNearPlane() const { return m_nearPlane; }
        inline float GetFarPlane() const { return m_farPlane; }
        inline vec2 GetProjJitter() const { return m_projJitter; }
        inline vec2 GetPrevProjJitter() const { return m_prevProjJitter; }

    private:
        mat4 m_view, m_previousView;
        mat4 m_projection, m_previousProjection, m_projectionNoJitter;
        mat4 m_viewProjection, m_previousViewProjection;
        mat4 m_invView, m_invProjection, m_invViewProjection;
        quat m_orientation;
        vec3 m_position, m_euler, m_worldOrientation;
        vec3 m_front, m_right, m_up;
        float m_nearPlane, m_farPlane, m_fovx, m_rotationSpeed;
        std::array<Plane, 6> m_frustum{};
        vec2 m_projJitter;
        vec2 m_prevProjJitter;
    };
}
