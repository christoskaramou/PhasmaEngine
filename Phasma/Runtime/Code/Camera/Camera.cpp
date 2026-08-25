#include "Camera/Camera.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Render/SceneRendererHost.h"
#include "RenderPasses/TAAPass.h"

namespace pe
{
    namespace
    {
        // PhasmaRuntime is static-linked into the editor host and hot-reload module,
        // so these callbacks are per image. Register from the image that calls them.
        CameraRuntimeCallbacks s_cameraRuntimeCallbacks;

        float DefaultCameraAspect()
        {
            SceneRendererHost *renderer = GetActiveSceneRendererHost();
            Image *displayRT = renderer ? renderer->GetDisplayRT() : nullptr;
            if (!displayRT || displayRT->GetHeight() == 0)
                return 16.0f / 9.0f;

            return displayRT->GetWidth_f() / displayRT->GetHeight_f();
        }

        bool DefaultCameraProjectionJitter(vec2 &jitter)
        {
            TAAPass *taa = GetGlobalComponent<TAAPass>();
            if (!taa)
                return false;

            taa->GenerateJitter();
            jitter = taa->GetProjectionJitter();
            return true;
        }
    } // namespace

    CameraRuntimeCallbacks CreateDefaultCameraRuntimeCallbacks()
    {
        return {
            DefaultCameraAspect,
            DefaultCameraProjectionJitter,
        };
    }

    void SetCameraRuntimeCallbacks(CameraRuntimeCallbacks callbacks)
    {
        s_cameraRuntimeCallbacks = callbacks;
    }

    Camera::Camera()
    {
        m_worldOrientation = vec3(1.f, -1.f, 1.f);

        // total pitch, yaw, roll
        m_euler = vec3(0.f, radians(-180.0f), 0.f);
        m_orientation = quat(m_euler);
        m_position = vec3(0.f, 0.01f, 0.1f);
        m_name = "Camera_" + std::to_string(reinterpret_cast<uintptr_t>(this));

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
        // ONCE PER FRAME for the previous-matrix rolls. The editor's late script-mutation
        // catch-up (RendererSystem::LateCatchUpForScriptMutations) runs Update() a second
        // time in the same frame; rolling previous again there sets previous == current and
        // zeroes the camera's motion vectors for that frame. TAA then alternates between
        // real and zero camera velocity across frames - visible trembling while the camera
        // moves. Same guard as Scene's motion roll and TAAPass::GenerateJitter.
        const uint32_t frameCounter = RHII.GetFrameCounter();
        const bool rollPrevious = frameCounter != m_matrixRollFrame;
        m_matrixRollFrame = frameCounter;

        m_front = m_orientation * WorldFront();
        m_right = m_orientation * WorldRight();
        m_up = m_orientation * WorldUp();

        UpdateProjection(rollPrevious);
        UpdateView(rollPrevious);

        m_invViewProjection = m_invView * m_invProjection;
        if (rollPrevious)
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
        m_dirty = false;
    }

    float Camera::GetAspect()
    {
        if (s_cameraRuntimeCallbacks.getAspect)
            return s_cameraRuntimeCallbacks.getAspect();

        return 16.0f / 9.0f;
    }

    void Camera::UpdateProjection(bool rollPrevious)
    {
        if (rollPrevious)
        {
            m_prevProjJitter = m_projJitter;
            m_previousProjection = m_projection;
        }

        float const aspect = GetAspect();

        if (m_projectionMode == CameraProjectionMode::Orthographic)
        {
            const float halfHeight = std::max(0.001f, m_orthographicSize) * 0.5f;
            const float halfWidth = halfHeight * aspect;
            const bool hasFiniteFarPlane = std::isfinite(m_farPlane) && m_farPlane > m_nearPlane &&
                                           m_farPlane < std::numeric_limits<float>::max() * 0.5f;
            float nearPlane = m_nearPlane;
            float farPlane = hasFiniteFarPlane ? m_farPlane : 1000.0f;
            // Reverse-Z: swap near/far so ortho maps near->1, far->0, matching the
            // perspective path below and the GREATER_OR_EQUAL depth compare op. Not
            // gated on PE_USE_GLM — ortho() itself is GLM-only, so the swap can never
            // be skipped against a live ortho() call (the prior #if was dead/misleading).
            if (Settings::Get<SceneSettings>().reverse_depth)
                std::swap(nearPlane, farPlane);
            m_projection = ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
        }
        else
        {
            // Infinite Reverse-Z Projection
            // Maps:
            // z_view = -near     => z_ndc = 1
            // z_view = -infinity => z_ndc = 0
            //
            // Matrix (Column-Major):
            // [ 1/(a*tan)   0       0    0 ]
            // [    0      1/tan     0    0 ]
            // [    0        0       0    n ]
            // [    0        0      -1    0 ]
            float const tanHalfFovy = tan(Fovy() / 2.0f);

            m_projection = mat4(0.0f);
            m_projection[0][0] = 1.0f / (aspect * tanHalfFovy);
            m_projection[1][1] = 1.0f / tanHalfFovy;
            m_projection[2][2] = 0.0f;
            m_projection[2][3] = 1.0f;
            m_projection[3][2] = m_nearPlane;
        }

        m_projectionNoJitter = m_projection; // Save the clean projection matrix

        if (m_projectionMode == CameraProjectionMode::Orthographic)
        {
            m_projJitter = vec2(0.0f);
        }
        else if (ActivePostProcessProfile().taa && s_cameraRuntimeCallbacks.updateProjectionJitter)
        {
            vec2 jitter;
            if (s_cameraRuntimeCallbacks.updateProjectionJitter(jitter))
            {
                m_projJitter = jitter;

                mat4 jitterMat = translate(mat4(1.0f), vec3(m_projJitter.x, m_projJitter.y, 0.f));
                m_projection = jitterMat * m_projection;
            }
        }

        m_invProjection = inverse(m_projection);
    }

    void Camera::UpdateView(bool rollPrevious)
    {
        if (rollPrevious)
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

        m_dirty = true;
    }

    void Camera::Rotate(float xoffset, float yoffset)
    {
        const float x = radians(yoffset * m_rotationSpeed);  // pitch
        const float y = radians(-xoffset * m_rotationSpeed); // yaw

        m_euler.x += x;
        m_euler.y += y;

        m_orientation = quat(m_euler);
        m_dirty = true;
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
        if (Settings::Get<SceneSettings>().freeze_frustum_culling)
            return;

        mat4 cullViewProjection = m_projectionNoJitter * m_view;
        mat4 pv = transpose(cullViewProjection);

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

    bool Camera::BuildWorldRayFromNdc(float ndcX, float ndcY, vec3 &origin, vec3 &dir) const
    {
        vec4 nearPoint = m_invViewProjection * vec4(ndcX, ndcY, 1.0f, 1.0f);
        if (std::abs(nearPoint.w) < 1e-6f)
            return false;
        nearPoint /= nearPoint.w;

        vec4 farPoint = m_invViewProjection * vec4(ndcX, ndcY, 0.0f, 1.0f);
        origin = vec3(nearPoint);
        if (std::abs(farPoint.w) < 1e-6f)
            dir = normalize(vec3(farPoint));
        else
        {
            farPoint /= farPoint.w;
            dir = normalize(vec3(farPoint) - origin);
        }

        return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z) &&
               std::isfinite(dir.x) && std::isfinite(dir.y) && std::isfinite(dir.z);
    }

    bool ProjectWorldToViewportRect(const vec3 &world, const mat4 &viewProj, float minX, float minY, float width,
                                    float height, float &screenX, float &screenY)
    {
        const vec4 clip = viewProj * vec4(world, 1.0f);
        if (clip.w <= 0.0f)
            return false;
        const vec2 ndc = vec2(clip) / clip.w;
        screenX = (ndc.x * 0.5f + 0.5f) * width + minX;
        screenY = (ndc.y * 0.5f + 0.5f) * height + minY;
        return std::isfinite(screenX) && std::isfinite(screenY);
    }
} // namespace pe
