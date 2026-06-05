#include "CameraWidget.h"
#include "Camera/Camera.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include <imgui.h>

namespace pe
{
    CameraWidget::CameraWidget() : Widget("Camera")
    {
    }

    static void DrawFloatControl(const char *label, float &value, const char *tooltip, float speed = 0.1f, float min = 0.0f, float max = 0.0f, const char *format = "%.3f", float columnWidth = 100.0f)
    {
        ImGui::PushID(label);

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);

        ImGui::Text("%s", label);

        ImGui::NextColumn();

        ImGui::DragFloat("##Value", &value, speed, min, max, format);
        ui::ItemTooltip(tooltip);

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

        const bool projectionOpen = ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen);
        ui::ItemTooltip("Camera lens and clipping settings.");
        if (projectionOpen)
        {
            int mode = camera->IsOrthographic() ? 1 : 0;
            if (ImGui::Combo("Mode", &mode, "Perspective\0Orthographic\0"))
                camera->SetProjectionMode(mode == 1 ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
            ui::ItemTooltip("Switch between perspective depth projection and orthographic projection.");

            if (camera->IsOrthographic())
            {
                float orthoSize = camera->GetOrthographicSize();
                float oldOrthoSize = orthoSize;
                DrawFloatControl(ICON_FA_EXPAND "  Size", orthoSize, "Visible vertical size for orthographic rendering.", 0.05f, 0.001f, 10000.0f, "%.3f");
                if (orthoSize != oldOrthoSize)
                    camera->SetOrthographicSize(orthoSize);
            }
            else
            {
                float fov = degrees(camera->Fovy());
                float oldFov = fov;
                DrawFloatControl(ICON_FA_EYE "  FOV", fov, "Vertical field of view in degrees.", 0.1f, 1.0f, 179.0f, "%.1f");
                if (fov != oldFov)
                    camera->SetFovx(camera->FovyToFovx(radians(fov)));
            }

            float nearPlane = camera->GetNearPlane();
            float oldNear = nearPlane;
            DrawFloatControl(ICON_FA_COMPRESS "  Near", nearPlane, "Closest distance rendered by the camera.", 0.001f, 0.001f, 1000.0f, "%.3f");
            if (nearPlane != oldNear)
                camera->SetNearPlane(nearPlane);

            float farPlane = camera->GetFarPlane();
            float oldFar = farPlane;
            DrawFloatControl(ICON_FA_EXPAND "  Far", farPlane, "Farthest distance rendered by the camera.", 0.1f, 1.0f, 10000.0f, "%.1f");
            if (farPlane != oldFar)
                camera->SetFarPlane(farPlane);
        }

        const bool controlOpen = ImGui::CollapsingHeader("Control");
        ui::ItemTooltip("Editor movement speeds for this camera.");
        if (controlOpen)
        {
            float rotSpeed = degrees(camera->GetRotationSpeed());
            DrawFloatControl(ICON_FA_TACHOMETER_ALT "  Rot Speed", rotSpeed, "Mouse-look rotation speed in degrees per input step.", 0.1f, 0.1f, 1000.0f, "%.2f", 120.0f);
            if (rotSpeed != degrees(camera->GetRotationSpeed()))
                camera->SetRotationSpeed(radians(rotSpeed));

            float camSpeed = camera->GetSpeed();
            DrawFloatControl(ICON_FA_RUNNING "  Mov Speed", camSpeed, "Keyboard movement speed for editor camera flight.", 0.1f, 0.1f, 1000.0f, "%.2f", 120.0f);
            if (camSpeed != camera->GetSpeed())
                camera->SetSpeed(camSpeed);
        }
    }
} // namespace pe
