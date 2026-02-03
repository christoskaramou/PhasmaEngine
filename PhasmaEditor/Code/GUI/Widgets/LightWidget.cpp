#include "LightWidget.h"
#include "GUI/IconsFontAwesome.h"
#include "Systems/LightSystem.h"
#include "glm/gtx/matrix_decompose.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    LightWidget::LightWidget() : Widget("LightWidget")
    {
    }

    void LightWidget::DrawEmbed(LightsUBO *lights, LightType type, int index)
    {
        if (!lights)
            return;

        auto DrawControl = [](const char *label, const char *icon, auto drawWork)
        {
            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  %s", icon, label);
            ImGui::NextColumn();
            ImGui::PushItemWidth(-1);
            drawWork();
            ImGui::PopItemWidth();
            ImGui::NextColumn();
            ImGui::PopID();
        };

        auto DrawVec3Inputs = [](float *values, float resetValue = 0.0f)
        {
            ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0}); // Small spacing between fields

            // X
            ImGui::DragFloat("##X", &values[0], 0.1f, 0.0f, 0.0f, "X: %.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // Y
            ImGui::DragFloat("##Y", &values[1], 0.1f, 0.0f, 0.0f, "Y: %.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // Z
            ImGui::DragFloat("##Z", &values[2], 0.1f, 0.0f, 0.0f, "Z: %.2f");
            ImGui::PopItemWidth();

            ImGui::PopStyleVar();
        };

        if (type == LightType::Directional)
        {
            ImGui::Text(ICON_FA_SUN "  Directional Light");
            ImGui::Separator();

            ImGui::Columns(2, "DirLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            DirectionalLight &sun = lights->sun;
            DrawControl("Color", ICON_FA_PALETTE, [&]()
                        { ImGui::ColorEdit3("##Color", &sun.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, [&]()
                        { ImGui::DragFloat("##Intensity", &sun.color.w, 0.1f, 0.0f, 100.0f); });
            DrawControl("Direction", ICON_FA_LOCATION_ARROW, [&]()
                        { DrawVec3Inputs(&sun.direction[0]); });

            ImGui::Columns(1);
        }
        else if (type == LightType::Point)
        {
            if (index < 0 || index >= MAX_POINT_LIGHTS)
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Point Light %d", index);
            ImGui::Separator();

            ImGui::Columns(2, "PointLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            PointLight &light = lights->pointLights[index];
            DrawControl("Color", ICON_FA_PALETTE, [&]()
                        { ImGui::ColorEdit3("##Color", &light.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, [&]()
                        { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 1000.0f); });
            DrawControl("Position", ICON_FA_ARROWS_ALT, [&]()
                        { DrawVec3Inputs(&light.position[0]); });
            DrawControl("Radius", ICON_FA_RULER_HORIZONTAL, [&]()
                        { ImGui::DragFloat("##Radius", &light.position.w, 0.1f, 0.0f, 1000.0f); });

            ImGui::Columns(1);
        }
        else if (type == LightType::Spot)
        {
            if (index < 0 || index >= MAX_SPOT_LIGHTS)
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Spot Light %d", index);
            ImGui::Separator();

            ImGui::Columns(2, "SpotLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            SpotLight &light = lights->spotLights[index];

            // Spot Light Controls
            DrawControl("Color", ICON_FA_PALETTE, [&]()
                        { ImGui::ColorEdit3("##Color", &light.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, [&]()
                        { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 1000.0f); });

            DrawControl("Position", ICON_FA_ARROWS_ALT, [&]()
                        {
                ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                ImGui::DragFloat("##X", &light.position.x, 0.1f, 0.0f, 0.0f, "X: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                ImGui::DragFloat("##Y", &light.position.y, 0.1f, 0.0f, 0.0f, "Y: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                ImGui::DragFloat("##Z", &light.position.z, 0.1f, 0.0f, 0.0f, "Z: %.2f"); ImGui::PopItemWidth();
                ImGui::PopStyleVar(); });

            DrawControl("Rotation", ICON_FA_SYNC_ALT, [&]()
                        {
                ImGui::PushMultiItemsWidths(2, ImGui::GetContentRegionAvail().x);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                ImGui::DragFloat("##RX", &light.rotation.x, 0.1f, 0.0f, 0.0f, "P: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                ImGui::DragFloat("##RY", &light.rotation.y, 0.1f, 0.0f, 0.0f, "Y: %.2f"); ImGui::PopItemWidth();
                ImGui::PopStyleVar(); });

            DrawControl("Range", ICON_FA_EXPAND, [&]()
                        { ImGui::DragFloat("##Range", &light.position.w, 0.1f, 0.0f, 1000.0f, "%.2f"); });

            if (light.rotation.z == 0.0f)
                light.rotation.z = 30.0f;
            DrawControl("Angle", ICON_FA_EYE, [&]()
                        { ImGui::DragFloat("##Angle", &light.rotation.z, 0.1f, 0.0f, 180.0f, "%.2f"); });

            if (light.rotation.w == 0.0f)
                light.rotation.w = 5.0f;
            DrawControl("Falloff", ICON_FA_CIRCLE_DOT, [&]()
                        { ImGui::DragFloat("##Falloff", &light.rotation.w, 0.1f, 0.0f, 180.0f, "%.2f"); });

            ImGui::Columns(1);
        }
    }
} // namespace pe
