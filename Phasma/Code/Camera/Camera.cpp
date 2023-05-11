#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "Renderer/RHI.h"
#include "PostProcess/SuperResolution.h"
#include "Systems/RendererSystem.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
    Camera::Camera()
    {
        worldOrientation = vec3(1.f, -1.f, 1.f);

        // total pitch, yaw, roll
        euler = vec3(0.f, 0.0f, 0.f);
        orientation = quat(euler);
        position = vec3(0.f, 0.f, 0.f);

        nearPlane = 0.005f;
        farPlane = 100.0f;
        fovx = radians(87.0f);
        speed = 0.35f;
        rotationSpeed = radians(2.864f);

        frustum.resize(6);

        // frustumCompute = Compute::Create("Shaders/Compute/frustumCS.hlsl", 64, 96, "camera_frustum_compute");
    }

    void Camera::ReCreateComputePipelines()
    {
        // frustumCompute.CreatePipeline("Shaders/Compute/frustumCS.hlsl");
    }

    void Camera::Destroy()
    {
        // frustumCompute.Destroy();
    }

    void Camera::Update()
    {
        front = orientation * WorldFront();
        right = orientation * WorldRight();
        up = orientation * WorldUp();

        UpdatePerspective();
        UpdateView();
        invViewProjection = invView * invProjection;
        previousViewProjection = viewProjection;
        viewProjection = projection * view;
        ExtractFrustum();
    }

    float Camera::GetAspect()
    {
        auto &renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();
        return renderArea.viewport.width / renderArea.viewport.height;
    }

    void Camera::UpdatePerspective()
    {
        previousProjection = projection;
        projection = perspective(Fovy(), GetAspect(), farPlane, nearPlane);

        if (GUI::use_FSR2)
        {
            prevProjJitter = projJitter;

            SuperResolution *sr = Context::Get()->GetSystem<PostProcessSystem>()->GetEffect<SuperResolution>();
            sr->GenerateJitter();
            projJitter = sr->GetProjectionJitter();

            mat4 jitterMat = translate(mat4(1.0f), vec3(projJitter.x, projJitter.y, 0.f));
            projectionNoJitter = projection; // save projection with no jitter
            projection = jitterMat * projection;
        }

        invProjection = inverse(projection);
    }

    void Camera::UpdateView()
    {
        previousView = view;
        view = lookAt(position, position + front, up);
        invView = inverse(view);
    }

    void Camera::Move(CameraDirection direction, float velocity)
    {
        if (direction == CameraDirection::FORWARD)
            position += front * velocity;
        if (direction == CameraDirection::BACKWARD)
            position -= front * velocity;
        if (direction == CameraDirection::RIGHT)
            position -= right * velocity;
        if (direction == CameraDirection::LEFT)
            position += right * velocity;
    }

    void Camera::Rotate(float xoffset, float yoffset)
    {
        const float x = radians(yoffset * rotationSpeed);  // pitch
        const float y = radians(-xoffset * rotationSpeed); // yaw

        euler.x += x;
        euler.y += y;

        orientation = quat(euler);
    }

    vec3 Camera::WorldRight() const
    {
        return vec3(worldOrientation.x, 0.f, 0.f);
    }

    vec3 Camera::WorldUp() const
    {
        return vec3(0.f, worldOrientation.y, 0.f);
    }

    vec3 Camera::WorldFront() const
    {
        return vec3(0.f, 0.f, worldOrientation.z);
    }

    void Camera::ExtractFrustum()
    {
        if (GUI::freezeFrustumCulling)
            return;

        // Just testing computes, the specific one is not speeding up any process
        // frustumCompute.UpdateInput(&viewProjection, sizeof(mat4));
        // frustumCompute.Dispatch(1, 1, 1);
        // frustumCompute.Wait();
        // frustumCompute.CopyDataTo(frustum.data(), frustum.size()); // Update frustum planes

        // transpose just to make the calculations look simpler
        mat4 pvm = transpose(viewProjection);

        /* Extract the numbers for the RIGHT plane */
        vec4 temp = pvm[3] - pvm[0];
        temp /= length(vec3(temp));

        frustum[0].normal[0] = temp.x;
        frustum[0].normal[1] = temp.y;
        frustum[0].normal[2] = temp.z;
        frustum[0].d = temp.w;

        /* Extract the numbers for the LEFT plane */
        temp = pvm[3] + pvm[0];
        temp /= length(vec3(temp));

        frustum[1].normal[0] = temp.x;
        frustum[1].normal[1] = temp.y;
        frustum[1].normal[2] = temp.z;
        frustum[1].d = temp.w;

        /* Extract the BOTTOM plane */
        temp = pvm[3] - pvm[1];
        temp /= length(vec3(temp));

        frustum[2].normal[0] = temp.x;
        frustum[2].normal[1] = temp.y;
        frustum[2].normal[2] = temp.z;
        frustum[2].d = temp.w;

        /* Extract the TOP plane */
        temp = pvm[3] + pvm[1];
        temp /= length(vec3(temp));

        frustum[3].normal[0] = temp.x;
        frustum[3].normal[1] = temp.y;
        frustum[3].normal[2] = temp.z;
        frustum[3].d = temp.w;

        /* Extract the FAR plane */
        temp = pvm[3] - pvm[2];
        temp /= length(vec3(temp));

        frustum[4].normal[0] = temp.x;
        frustum[4].normal[1] = temp.y;
        frustum[4].normal[2] = temp.z;
        frustum[4].d = temp.w;

        /* Extract the NEAR plane */
        temp = pvm[3] + pvm[2];
        temp /= length(vec3(temp));

        frustum[5].normal[0] = temp.x;
        frustum[5].normal[1] = temp.y;
        frustum[5].normal[2] = temp.z;
        frustum[5].d = temp.w;
    }

    bool Camera::PointInFrustum(const vec3 &point, float radius) const
    {
        for (const auto &plane : frustum)
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
        vec3 center = aabb.GetCenter();
        vec3 size = aabb.GetSize();

        // Iterate through all six planes of the frustum
        for (int i = 0; i < 6; i++)
        {
            const Plane &plane = frustum[i];
            vec3 normal(plane.normal[0], plane.normal[1], plane.normal[2]);

            // Calculate the distance between the AABB center and the plane
            float distance = dot(normal, center) + plane.d;

            // Calculate the maximum distance of the AABB from the center along the plane normal
            float radius = dot(abs(normal), size * 0.5f);

            // Check if the AABB is outside of the plane
            if (distance < -radius)
            {
                // AABB is completely outside of the frustum, cull it
                return false;
            }
        }

        // AABB is not completely outside of any plane, it is inside or intersecting with the frustum
        return true;
    }
}
