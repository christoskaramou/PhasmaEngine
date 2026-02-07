#include "Camera/Camera.h"
#include "API/Image.h"
#include "RenderPasses/TAAPass.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    Camera::Camera()
    {
        m_worldOrientation = vec3(1.f, -1.f, 1.f);

        // total pitch, yaw, roll
        m_euler = vec3(0.f, radians(-180.0f), 0.f);
        m_orientation = quat(m_euler);
        m_position = vec3(0.f, 0.01f, 0.1f);

        m_nearPlane = 0.005f;
        m_farPlane = FLT_MAX; // Indicates infinite far plane
        m_fovx = radians(87.0f);
        m_rotationSpeed = radians(2.864f);
        m_speed = 3.5f;

        m_projJitter = vec2(0.f, 0.f);
        m_prevProjJitter = vec2(0.f, 0.f);

        m_view = mat4(1.0f);
        m_previousView = mat4(1.0f);
        m_projection = mat4(1.0f);
        m_previousProjection = mat4(1.0f);
        m_projectionNoJitter = mat4(1.0f);
        m_viewProjection = mat4(1.0f);
        m_previousViewProjection = mat4(1.0f);
        m_invView = mat4(1.0f);
        m_invProjection = mat4(1.0f);
        m_invViewProjection = mat4(1.0f);
    }

    void Camera::Destroy()
    {
    }

    void Camera::Update()
    {
        m_front = m_orientation * WorldFront();
        m_right = m_orientation * WorldRight();
        m_up = m_orientation * WorldUp();

        UpdatePerspective();
        UpdateView();

        m_invViewProjection = m_invView * m_invProjection;
        m_previousViewProjection = m_viewProjection;
        m_viewProjection = m_projection * m_view;

        // On first update (or if reset), sync previous with current to avoid jump artifacts
        if (m_previousViewProjection == mat4(1.0f))
        {
            m_previousViewProjection = m_viewProjection;
            m_previousView = m_view;
            m_previousProjection = m_projection;
        }

        ExtractFrustum();
    }

    float Camera::GetAspect()
    {
        Image *displayRT = GetGlobalSystem<RendererSystem>()->GetDisplayRT();
        Rect2Di rect = Rect2Di(0, 0, displayRT->GetWidth(), displayRT->GetHeight());
        return static_cast<float>(rect.width) / static_cast<float>(rect.height);
    }

    void Camera::UpdatePerspective()
    {
        m_prevProjJitter = m_projJitter;
        m_previousProjection = m_projection;

        // Infinite Reverse-Z Projection
        // Maps:
        // z_view = -near    => z_ndc = 1
        // z_view = -infinity => z_ndc = 0
        //
        // Matrix (Column-Major):
        // [ 1/(a*tan)   0       0    0 ]
        // [    0      1/tan     0    0 ]
        // [    0        0       0    n ]
        // [    0        0      -1    0 ]

        float const tanHalfFovy = tan(Fovy() / 2.0f);
        float const aspect = GetAspect();

        m_projection = mat4(0.0f);
        m_projection[0][0] = 1.0f / (aspect * tanHalfFovy);
        m_projection[1][1] = 1.0f / tanHalfFovy; // Vulkan Y-flip
        m_projection[2][2] = 0.0f;
        m_projection[2][3] = 1.0f;
        m_projection[3][2] = m_nearPlane;

        m_projectionNoJitter = m_projection; // Save the clean projection matrix

        if (Settings::Get<GlobalSettings>().taa)
        {
            TAAPass *taa = GetGlobalComponent<TAAPass>();
            if (taa)
            {
                taa->GenerateJitter();
                m_projJitter = taa->GetProjectionJitter();

                mat4 jitterMat = translate(mat4(1.0f), vec3(m_projJitter.x, m_projJitter.y, 0.f));
                m_projection = jitterMat * m_projection;
            }
        }

        m_invProjection = inverse(m_projection);
    }

    void Camera::UpdateView()
    {
        m_previousView = m_view;
        m_view = lookAt(m_position, m_position + m_front, m_up);
        m_invView = inverse(m_view);
    }

    void Camera::Move(CameraDirection direction, float speed)
    {
        if (direction == CameraDirection::FORWARD)
            m_position += m_front * speed;
        if (direction == CameraDirection::BACKWARD)
            m_position -= m_front * speed;
        if (direction == CameraDirection::RIGHT)
            m_position -= m_right * speed;
        if (direction == CameraDirection::LEFT)
            m_position += m_right * speed;
    }

    void Camera::Rotate(float xoffset, float yoffset)
    {
        const float x = radians(yoffset * m_rotationSpeed);  // pitch
        const float y = radians(-xoffset * m_rotationSpeed); // yaw

        m_euler.x += x;
        m_euler.y += y;

        m_orientation = quat(m_euler);
    }

    vec3 Camera::WorldRight() const
    {
        return vec3(m_worldOrientation.x, 0.f, 0.f);
    }

    vec3 Camera::WorldUp() const
    {
        return vec3(0.f, m_worldOrientation.y, 0.f);
    }

    vec3 Camera::WorldFront() const
    {
        return vec3(0.f, 0.f, m_worldOrientation.z);
    }

    void Camera::ExtractFrustum()
    {
        if (Settings::Get<GlobalSettings>().freeze_frustum_culling)
            return;

        // Transpose just to make the calculations look simpler
        mat4 pv = transpose(m_viewProjection);

        auto ExtractAndNormalize = [&](int index, const vec4 &row_diff)
        {
            vec4 temp = row_diff / length(vec3(row_diff));
            m_frustum[index].normal[0] = temp.x;
            m_frustum[index].normal[1] = temp.y;
            m_frustum[index].normal[2] = temp.z;
            m_frustum[index].d = temp.w;
        };

        // Right and Left planes
        ExtractAndNormalize(0, pv[3] - pv[0]);
        ExtractAndNormalize(1, pv[3] + pv[0]);

        // Bottom and Top planes
        ExtractAndNormalize(2, pv[3] - pv[1]);
        ExtractAndNormalize(3, pv[3] + pv[1]);

        // Far and Near planes
        ExtractAndNormalize(4, pv[3] - pv[2]);
        ExtractAndNormalize(5, pv[3] + pv[2]);
    }

    bool Camera::PointInFrustum(const vec3 &point, float radius) const
    {
        for (const Plane &plane : m_frustum)
        {
            vec3 normal = make_vec3(plane.normal);
            const float dist = dot(normal, point) + plane.d;

            if (dist < -radius)
                return false;

            if (fabs(dist) < radius)
                return true;
        }

        return true;
    }

    bool Camera::AABBInFrustum(const AABB &aabb) const
    {
        const vec3 &center = aabb.GetCenter();
        const vec3 &size = aabb.GetSize();

        for (const Plane &plane : m_frustum)
        {
            vec3 normal = vec3(plane.normal[0], plane.normal[1], plane.normal[2]);
            float distance = dot(normal, center) + plane.d;
            float radius = dot(abs(normal), size * 0.5f);

            if (distance < -radius)
                return false;
        }

        return true;
    }
} // namespace pe
