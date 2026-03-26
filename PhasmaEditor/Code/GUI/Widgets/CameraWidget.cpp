#include "CameraWidget.h"
#include "Camera/Camera.h"
#include "GUI/IconsFontAwesome.h"
#include <imgui.h>

namespace pe
{
    CameraWidget::CameraWidget() : Widget("Camera")
    {
    }

    static void DrawFloatControl(const char *label, float &value, float speed = 0.1f, float min = 0.0f, float max = 0.0f, const char *format = "%.3f", float columnWidth = 100.0f)
    {
        ImGui::PushID(label);

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);

        ImGui::Text("%s", label);

        ImGui::NextColumn();

        ImGui::DragFloat("##Value", &value, speed, min, max, format);

        ImGui::Columns(1);
        ImGui::PopID();
    }

    void CameraWidget::Update()
    {
    }

    void CameraWidget::DrawEmbed(Camera *camera)
    {
        if (!camera)
            return;

        if (ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float fov = degrees(camera->Fovy());
            float oldFov = fov;
            DrawFloatControl(ICON_FA_EYE "  FOV", fov, 0.1f, 1.0f, 179.0f, "%.1f");
            if (fov != oldFov)
                camera->SetFovx(camera->FovyToFovx(radians(fov)));

            float nearPlane = camera->GetNearPlane();
            float oldNear = nearPlane;
            DrawFloatControl(ICON_FA_COMPRESS "  Near", nearPlane, 0.001f, 0.001f, 1000.0f, "%.3f");
            if (nearPlane != oldNear)
                camera->SetNearPlane(nearPlane);

            float farPlane = camera->GetFarPlane();
            float oldFar = farPlane;
            DrawFloatControl(ICON_FA_EXPAND "  Far", farPlane, 0.1f, 1.0f, 10000.0f, "%.1f");
            if (farPlane != oldFar)
                camera->SetFarPlane(farPlane);
        }

        if (ImGui::CollapsingHeader("Control"))
        {
            float rotSpeed = degrees(camera->GetRotationSpeed());
            DrawFloatControl(ICON_FA_TACHOMETER_ALT "  Rot Speed", rotSpeed, 0.1f, 0.1f, 1000.0f, "%.2f", 120.0f);
            if (rotSpeed != degrees(camera->GetRotationSpeed()))
                camera->SetRotationSpeed(radians(rotSpeed));

            float camSpeed = camera->GetSpeed();
            DrawFloatControl(ICON_FA_RUNNING "  Mov Speed", camSpeed, 0.1f, 0.1f, 1000.0f, "%.2f", 120.0f);
            if (camSpeed != camera->GetSpeed())
                camera->SetSpeed(camSpeed);
        }
    }
} // namespace pe
