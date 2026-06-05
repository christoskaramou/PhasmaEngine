#include "LightWidget.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    LightWidget::LightWidget() : Widget("LightWidget")
    {
    }

    void LightWidget::DrawEmbed(Scene *scene, LightType type, int index)
    {
        if (!scene)
            return;

        auto DrawControl = [](const char *label, const char *icon, const char *tooltip, auto drawWork)
        {
            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  %s", icon, label);
            ImGui::NextColumn();
            ImGui::PushItemWidth(-1);
            drawWork();
            if (tooltip && tooltip[0])
                ui::ItemTooltip(tooltip);
            ImGui::PopItemWidth();
            ImGui::NextColumn();
            ImGui::PopID();
        };

        if (type == LightType::Directional)
        {
            if (scene->GetDirectionalLights().empty())
                return;

            ImGui::Text(ICON_FA_SUN "  Directional Light");
            ImGui::Separator();

            ImGui::Columns(2, "DirLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            DirectionalLight &light = scene->GetDirectionalLights()[index >= 0 ? index : 0];
            DrawControl("Color", ICON_FA_PALETTE, "Tint emitted by this directional light.", [&]()
                        { ImGui::ColorEdit3("##Color", &light.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, "Brightness of the directional light.", [&]()
                        { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 100.0f); });

            ImGui::Columns(1);
        }
        else if (type == LightType::Point)
        {
            if (index < 0 || index >= (int)scene->GetPointLights().size())
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Point Light");
            ImGui::Separator();

            ImGui::Columns(2, "PointLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            PointLight &light = scene->GetPointLights()[index];
            DrawControl("Color", ICON_FA_PALETTE, "Tint emitted by this point light.", [&]()
                        { ImGui::ColorEdit3("##Color", &light.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, "Brightness of the point light.", [&]()
                        { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 1000.0f); });
            DrawControl("Radius", ICON_FA_RULER_HORIZONTAL, "Maximum influence radius for the point light.", [&]()
                        { ImGui::DragFloat("##Radius", &light.position.w, 0.1f, 0.0f, 1000.0f); });

            ImGui::Columns(1);
        }
        else if (type == LightType::Spot)
        {
            if (index < 0 || index >= (int)scene->GetSpotLights().size())
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Spot Light");
            ImGui::Separator();

            ImGui::Columns(2, "SpotLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            SpotLight &light = scene->GetSpotLights()[index];
            DrawControl("Color", ICON_FA_PALETTE, "Tint emitted by this spot light.", [&]()
                        { ImGui::ColorEdit3("##Color", &light.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, "Brightness of the spot light.", [&]()
                        { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 1000.0f); });
            DrawControl("Range", ICON_FA_EXPAND, "Maximum distance reached by the cone.", [&]()
                        { ImGui::DragFloat("##Range", &light.position.w, 0.1f, 0.0f, 1000.0f, "%.2f"); });
            DrawControl("Angle", ICON_FA_EYE, "Outer cone angle in degrees.", [&]()
                        { ImGui::DragFloat("##Angle", &light.params.x, 0.1f, 0.0f, 180.0f, "%.2f"); });
            DrawControl("Falloff", ICON_FA_CIRCLE_DOT, "Soft-edge falloff angle for the spot cone.", [&]()
                        { ImGui::DragFloat("##Falloff", &light.params.y, 0.1f, 0.0f, 180.0f, "%.2f"); });

            ImGui::Columns(1);
        }
        else if (type == LightType::Area)
        {
            if (index < 0 || index >= (int)scene->GetAreaLights().size())
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Area Light");
            ImGui::Separator();

            ImGui::Columns(2, "AreaLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            AreaLight &light = scene->GetAreaLights()[index];
            DrawControl("Color", ICON_FA_PALETTE, "Tint emitted by this area light.", [&]()
                        { ImGui::ColorEdit3("##Color", &light.color[0]); });
            DrawControl("Intensity", ICON_FA_BOLT, "Brightness of the area light.", [&]()
                        { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 1000.0f); });
            DrawControl("Range", ICON_FA_EXPAND, "Maximum influence distance for the area light.", [&]()
                        { ImGui::DragFloat("##Range", &light.position.w, 0.1f, 0.0f, 1000.0f, "%.2f"); });
            DrawControl("Size", ICON_FA_RULER_HORIZONTAL, nullptr, [&]()
                        {
                ImGui::PushMultiItemsWidths(2, ImGui::GetContentRegionAvail().x);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                ImGui::DragFloat("##W", &light.size.x, 0.1f, 0.0f, 1000.0f, "W: %.2f");
                ui::ItemTooltip("Width of the rectangular light emitter.");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::DragFloat("##H", &light.size.y, 0.1f, 0.0f, 1000.0f, "H: %.2f");
                ui::ItemTooltip("Height of the rectangular light emitter.");
                ImGui::PopItemWidth();
                ImGui::PopStyleVar(); });

            ImGui::Columns(1);
        }
    }
} // namespace pe
