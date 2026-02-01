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
        inline float Fovx() { return m_fovx; }
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
        inline mat4 GetProjectionNoJitter() const { return m_projectionNoJitter; }
        inline mat4 GetViewProjection() const { return m_viewProjection; }
        inline mat4 GetPreviousViewProjection() const { return m_previousViewProjection; }
        inline mat4 GetInvView() const { return m_invView; }
        inline mat4 GetInvProjection() const { return m_invProjection; }
        inline mat4 GetInvViewProjection() const { return m_invViewProjection; }
        inline vec3 GetPosition() const { return m_position; }
        inline vec3 GetFront() const { return m_front; }
        inline vec3 GetRight() const { return m_right; }
        inline vec3 GetUp() const { return m_up; }
        inline float GetNearPlane() const { return m_nearPlane; }
        inline float GetFarPlane() const { return m_farPlane; }
        inline float GetRotationSpeed() const { return m_rotationSpeed; }
        inline float GetSpeed() const { return m_speed; }
        inline vec3 GetEuler() const { return m_euler; }
        inline void SetPosition(const vec3 &position) { m_position = position; }
        inline void SetEuler(const vec3 &euler) { m_euler = euler; m_orientation = quat(m_euler); }
        inline void SetRotationSpeed(float speed) { m_rotationSpeed = speed; }
        inline void SetSpeed(float speed) { m_speed = speed; }
        inline void SetNearPlane(float nearPlane) { m_nearPlane = nearPlane; }
        inline void SetFarPlane(float farPlane) { m_farPlane = farPlane; }
        inline void SetFovx(float fovx) { m_fovx = fovx; }
        inline void SetProjJitter(const vec2 &jitter) { m_projJitter = jitter; }
        inline void SetPrevProjJitter(const vec2 &prevJitter) { m_prevProjJitter = prevJitter; }

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
        float m_nearPlane, m_farPlane, m_fovx, m_rotationSpeed, m_speed;
        std::array<Plane, 6> m_frustum{};
        vec2 m_projJitter;
        vec2 m_prevProjJitter;
    };
} // namespace pe
