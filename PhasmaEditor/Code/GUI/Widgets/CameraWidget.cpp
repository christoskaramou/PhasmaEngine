#include "CameraWidget.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"
#include <imgui.h>

namespace pe
{
    CameraWidget::CameraWidget() : Widget("Camera")
    {
    }

    void CameraWidget::Update()
    {
        if (!m_open)
            return;

        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();
        if (!camera)
            return;

        ImGui::Begin("Camera", &m_open);

        vec3 pos = camera->GetPosition();
        if (ImGui::DragFloat3("Position", &pos[0], 0.05f))
        {
            camera->SetPosition(pos);
        }

        vec3 euler = degrees(camera->GetEuler());
        if (ImGui::DragFloat3("Rotation", &euler[0], 0.1f))
        {
            camera->SetEuler(radians(euler));
        }

        float fov = degrees(camera->Fovy());
        if (ImGui::SliderFloat("FOV", &fov, 1.0f, 179.0f))
        {
            camera->SetFovx(camera->FovyToFovx(radians(fov)));
        }

        float nearPlane = camera->GetNearPlane();
        if (ImGui::DragFloat("Near Plane", &nearPlane, 0.001f, 0.001f))
        {
            camera->SetNearPlane(nearPlane);
        }

        float farPlane = camera->GetFarPlane();
        if (ImGui::DragFloat("Far Plane", &farPlane, 0.1f, 1.0f))
        {
            camera->SetFarPlane(farPlane);
        }

        float rotSpeed = degrees(camera->GetRotationSpeed());
        if (ImGui::DragFloat("Rotation Speed", &rotSpeed, 0.1f, 0.1f, 10.0f))
        {
            camera->SetRotationSpeed(radians(rotSpeed));
        }

        float camSpeed = camera->GetSpeed();
        if (ImGui::DragFloat("Camera Speed", &camSpeed, 0.1f, 0.1f))
        {
            camera->SetSpeed(camSpeed);
        }

        ImGui::End();
    }
} // namespace pe
