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

#include "Camera.h"
#include "GUI/GUI.h"
#include "Renderer/Compute.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "ECS/Context.h"
#include "Systems/RendererSystem.h"
#include "Core/Settings.h"

namespace pe
{
    Camera::Camera()
    {
        worldOrientation = vec3(1.f, 1.f, 1.f);

        // total pitch, yaw, roll
        euler = vec3(0.f, 0.0f, 0.f);
        orientation = quat(euler);
        position = vec3(0.f, 0.f, 0.f);

        nearPlane = 0.005f;
        farPlane = 100.0f;
        FOV = radians(87.0f);
        speed = 0.35f;
        rotationSpeed = 0.05f;

        frustum.resize(6);

        frustumCompute = Compute::Create("Shaders/Compute/frustum.comp", 64, 96);
    }

    void Camera::ReCreateComputePipelines()
    {
        frustumCompute.createPipeline("Shaders/Compute/frustum.comp");
    }

    void Camera::Destroy()
    {
        frustumCompute.destroy();
    }

    void Camera::Update()
    {
        auto &renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();

        front = orientation * WorldFront();
        right = orientation * WorldRight();
        up = orientation * WorldUp();
        previousView = view;
        previousProjection = projection;
        projOffsetPrevious = projOffset;
        if (GUI::use_TAA)
        {
            // has the aspect ratio of the render area because the projection matrix has the same aspect ratio too,
            // doesn't matter if it renders in bigger image size,
            // it will be scaled down to render area size before GUI pass
            //const int i = static_cast<int>(floor(rand(0.0f, 15.99f)));
            //projOffset = vec2(&halton16[i * 2]);
            projOffset = halton_2_3_next(16);
            projOffset *= vec2(2.0f);
            projOffset -= vec2(1.0f);
            projOffset /= vec2(renderArea.viewport.width, renderArea.viewport.height);
            projOffset *= GUI::renderTargetsScale;
            projOffset *= GUI::TAA_jitter_scale;
        }
        else
        {
            projOffset = {0.0f, 0.0f};
        }
        UpdatePerspective();
        UpdateView();
        invView = inverse(view);
        invProjection = inverse(projection);
        invViewProjection = invView * invProjection;
        previousViewProjection = viewProjection;
        viewProjection = projection * view;
        ExtractFrustum();
    }

    void Camera::UpdatePerspective()
    {
        auto &renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();
        const float aspect = renderArea.viewport.width / renderArea.viewport.height;
        projection = perspective(FovxToFovy(FOV, aspect), aspect, nearPlane, farPlane, GlobalSettings::ReverseZ);
        projection[2][0] = projOffset.x;
        projection[2][1] = projOffset.y;
    }

    void Camera::UpdateView()
    {
        //view = lookAt(position, position + front, WorldUp());
        view = lookAt(position, front, right, up);
    }

    void Camera::Move(RelativeDirection direction, float velocity)
    {
        if (direction == RelativeDirection::FORWARD) position += front * velocity;
        if (direction == RelativeDirection::BACKWARD) position -= front * velocity;
        if (direction == RelativeDirection::RIGHT) position -= right * velocity;
        if (direction == RelativeDirection::LEFT) position += right * velocity;
    }

    void Camera::Rotate(float xoffset, float yoffset)
    {
        const float x = radians(yoffset * rotationSpeed);   // pitch
        const float y = radians(-xoffset * rotationSpeed);  // yaw

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
        // Just testing computes, the specific one is not speeding up any process
        frustumCompute.waitFence();
        frustumCompute.copyDataTo(frustum.data(), frustum.size()); // Update frustum planes
        frustumCompute.updateInput(&viewProjection, sizeof(mat4));
        frustumCompute.dispatch(1, 1, 1);

//		// transpose just to make the calculations look simpler
//		mat4 pvm = transpose(viewProjection);
//
//		/* Extract the numbers for the RIGHT plane */
//		vec4 temp = pvm[3] - pvm[0];
//		temp /= length(vec3(temp));
//
//		frustum[0].normal = vec3(temp);
//		frustum[0].d = temp.w;
//
//		/* Extract the numbers for the LEFT plane */
//		temp = pvm[3] + pvm[0];
//		temp /= length(vec3(temp));
//
//		frustum[1].normal = vec3(temp);
//		frustum[1].d = temp.w;
//
//		/* Extract the BOTTOM plane */
//		temp = pvm[3] - pvm[1];
//		temp /= length(vec3(temp));
//
//		frustum[2].normal = vec3(temp);
//		frustum[2].d = temp.w;
//
//		/* Extract the TOP plane */
//		temp = pvm[3] + pvm[1];
//		temp /= length(vec3(temp));
//
//		frustum[3].normal = vec3(temp);
//		frustum[3].d = temp.w;
//
//		/* Extract the FAR plane */
//		temp = pvm[3] - pvm[2];
//		temp /= length(vec3(temp));
//
//		frustum[4].normal = vec3(temp);
//		frustum[4].d = temp.w;
//
//		/* Extract the NEAR plane */
//		temp = pvm[3] + pvm[2];
//		temp /= length(vec3(temp));
//
//		frustum[5].normal = vec3(temp);
//		frustum[5].d = temp.w;
    }

    bool Camera::PointInFrustum(const vec3 &point, float radius) const
    {
        for (auto &plane : frustum)
        {
            const float dist = dot(plane.normal, point) + plane.d;

            if (dist < -radius)
                return false;

            if (fabs(dist) < radius)
                return true;
        }
        return true;
    }

    bool Camera::AABBInFrustum(const AABB &aabb) const
    {
        const vec3 &min = aabb.min;
        const vec3 &max = aabb.max;

        vec3 center = (max - min) * 0.5f;
        if (PointInFrustum(center, 0.0f))
            return true;

        vec3 corners[8];
        corners[0] = min;                           // blb
        corners[1] = vec3(min.x, min.y, max.z);     // blf
        corners[2] = vec3(min.x, max.y, min.z);     // tlb
        corners[3] = vec3(min.x, max.y, max.z);     // tlf
        corners[4] = vec3(max.x, min.y, min.z);     // brb
        corners[5] = vec3(max.x, min.y, max.z);     // brf
        corners[6] = vec3(max.x, max.y, min.z);     // trb
        corners[7] = max;                           // trf

        for (int i = 0; i < 8; i++)
        {
            if (PointInFrustum(corners[i], 0.0f))
                return true;
        }
        return false;
    }
}
