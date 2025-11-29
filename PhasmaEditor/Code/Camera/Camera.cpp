#include "Camera/Camera.h"
#include "Scene/SceneNodeComponent.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "Systems/RendererSystem.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
    Camera::Camera()
        : m_sceneNode(nullptr)
    {
        m_worldOrientation = vec3(1.f, -1.f, 1.f);

        // total pitch, yaw, roll
        m_euler = vec3(0.f, 0.0f, 0.f);
        m_orientation = quat(m_euler);
        m_position = vec3(0.f, 0.f, 0.f);

        m_nearPlane = 0.005f;
        m_farPlane = 100.0f;
        m_fovx = radians(87.0f);
        m_rotationSpeed = radians(2.864f);

        m_projJitter = vec2(0.f, 0.f);
        m_prevProjJitter = vec2(0.f, 0.f);

    }

    void Camera::Destroy()
    {
    }

    void Camera::SetSceneNode(SceneNodeComponent *sceneNode)
    {
        m_sceneNode = sceneNode;
        if (m_sceneNode)
            m_sceneNode->SetScale(vec3(1.0f));

        SyncSceneNodeTransform();
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
        ExtractFrustum();
    }

    float Camera::GetAspect()
    {
        RenderArea &renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();
        return renderArea.viewport.width / renderArea.viewport.height;
    }

    void Camera::UpdatePerspective()
    {
        m_prevProjJitter = m_projJitter;
        m_previousProjection = m_projection;

        m_projection = perspective(Fovy(), GetAspect(), m_farPlane, m_nearPlane);
        m_projectionNoJitter = m_projection; // Save the clean projection matrix

        // If using FSR2, apply jitter to the projection matrix
        if (Settings::Get<SRSettings>().enable)
        {
            SuperResolutionPass *sr = Context::Get()->GetSystem<PostProcessSystem>()->GetEffect<SuperResolutionPass>();
            sr->GenerateJitter();
            m_projJitter = sr->GetProjectionJitter();

            mat4 jitterMat = translate(mat4(1.0f), vec3(m_projJitter.x, m_projJitter.y, 0.f));
            m_projection = jitterMat * m_projection;
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

        SyncSceneNodeTransform();
    }

    void Camera::Rotate(float xoffset, float yoffset)
    {
        const float x = radians(yoffset * m_rotationSpeed);  // pitch
        const float y = radians(-xoffset * m_rotationSpeed); // yaw

        m_euler.x += x;
        m_euler.y += y;

        m_orientation = quat(m_euler);

        SyncSceneNodeTransform();
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

    void Camera::SyncSceneNodeTransform() const
    {
        if (!m_sceneNode)
            return;

        m_sceneNode->SetTranslation(m_position);
        m_sceneNode->SetRotation(m_orientation);
    }
}
