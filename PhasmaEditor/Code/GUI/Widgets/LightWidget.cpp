#include "LightWidget.h"
#include "GUI/IconsFontAwesome.h"
#include "Systems/LightSystem.h"
#include "glm/gtx/matrix_decompose.hpp"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"


namespace pe
{
    LightWidget::LightWidget() : Widget("LightWidget")
    {
    }

    void LightWidget::DrawEmbed(LightSystem *ls, LightType type, int index)
    {
        if (!ls)
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

            if (!ls->GetDirectionalLights().empty())
            {
                DirectionalLight &light = ls->GetDirectionalLights()[0];
                DrawControl("Color", ICON_FA_PALETTE, [&]()
                            { ImGui::ColorEdit3("##Color", &light.color[0]); });
                DrawControl("Intensity", ICON_FA_BOLT, [&]()
                            { ImGui::DragFloat("##Intensity", &light.color.w, 0.1f, 0.0f, 100.0f); });
                DrawControl("Position", ICON_FA_ARROWS_ALT, [&]()
                            { DrawVec3Inputs(&light.position[0]); });
                DrawControl("Rotation", ICON_FA_SYNC_ALT, [&]()
                            {
                                mat4 m = translate(mat4(1.0f), vec3(light.position)) * mat4_cast(quat(light.rotation.w, light.rotation.x, light.rotation.y, light.rotation.z));
                                float t[3], r[3], s[3];
                                ImGuizmo::DecomposeMatrixToComponents(value_ptr(m), t, r, s);
                                vec3 euler = vec3(r[0], r[1], r[2]);
                                vec3 oldEuler = euler;

                                ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
                                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                                ImGui::DragFloat("##RX", &euler.x, 0.1f, 0.0f, 0.0f, "P: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                                ImGui::DragFloat("##RY", &euler.y, 0.1f, 0.0f, 0.0f, "Y: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                                ImGui::DragFloat("##RZ", &euler.z, 0.1f, 0.0f, 0.0f, "R: %.2f"); ImGui::PopItemWidth();
                                ImGui::PopStyleVar(); 

                                if (glm::any(glm::notEqual(euler, oldEuler, 0.001f)))
                                {
                                    float matrix[16];
                                    ImGuizmo::RecomposeMatrixFromComponents(t, value_ptr(euler), s, matrix);
                                    mat4 newMatrix = make_mat4(matrix);
                                    quat q = quat_cast(newMatrix);
                                    light.rotation = vec4(q.x, q.y, q.z, q.w);
                                } });
            }

            ImGui::Columns(1);
        }
        else if (type == LightType::Point)
        {
            if (index < 0 || index >= (int)ls->GetPointLights().size())
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Point Light %d", index);
            ImGui::Separator();

            ImGui::Columns(2, "PointLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            PointLight &light = ls->GetPointLights()[index];
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
            if (index < 0 || index >= (int)ls->GetSpotLights().size())
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Spot Light %d", index);
            ImGui::Separator();

            ImGui::Columns(2, "SpotLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            SpotLight &light = ls->GetSpotLights()[index];

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
                            mat4 m = translate(mat4(1.0f), vec3(light.position)) * mat4_cast(quat(light.rotation.w, light.rotation.x, light.rotation.y, light.rotation.z));
                            float t[3], r[3], s[3];
                            ImGuizmo::DecomposeMatrixToComponents(value_ptr(m), t, r, s);
                            vec3 euler = vec3(r[0], r[1], r[2]);
                            vec3 oldEuler = euler;

                            ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
                            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                            ImGui::DragFloat("##RX", &euler.x, 0.1f, 0.0f, 0.0f, "P: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                            ImGui::DragFloat("##RY", &euler.y, 0.1f, 0.0f, 0.0f, "Y: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                            ImGui::DragFloat("##RZ", &euler.z, 0.1f, 0.0f, 0.0f, "R: %.2f"); ImGui::PopItemWidth();
                            ImGui::PopStyleVar(); 

                            if (glm::any(glm::notEqual(euler, oldEuler, 0.001f)))
                            {
                                float matrix[16];
                                ImGuizmo::RecomposeMatrixFromComponents(t, value_ptr(euler), s, matrix);
                                mat4 newMatrix = make_mat4(matrix);
                                quat q = quat_cast(newMatrix);
                                light.rotation = vec4(q.x, q.y, q.z, q.w);
                            } });

            DrawControl("Range", ICON_FA_EXPAND, [&]()
                        { ImGui::DragFloat("##Range", &light.position.w, 0.1f, 0.0f, 1000.0f, "%.2f"); });

            DrawControl("Angle", ICON_FA_EYE, [&]()
                        { ImGui::DragFloat("##Angle", &light.params.x, 0.1f, 0.0f, 180.0f, "%.2f"); });

            DrawControl("Falloff", ICON_FA_CIRCLE_DOT, [&]()
                        { ImGui::DragFloat("##Falloff", &light.params.y, 0.1f, 0.0f, 180.0f, "%.2f"); });

            ImGui::Columns(1);
        }
        else if (type == LightType::Area)
        {
            if (index < 0 || index >= (int)ls->GetAreaLights().size())
                return;

            ImGui::Text(ICON_FA_LIGHTBULB "  Area Light %d", index);
            ImGui::Separator();

            ImGui::Columns(2, "AreaLightCols", false);
            ImGui::SetColumnWidth(0, 120.0f);

            AreaLight &light = ls->GetAreaLights()[index];

            // Area Light Controls
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
                            mat4 m = translate(mat4(1.0f), vec3(light.position)) * mat4_cast(quat(light.rotation.w, light.rotation.x, light.rotation.y, light.rotation.z));
                            float t[3], r[3], s[3];
                            ImGuizmo::DecomposeMatrixToComponents(value_ptr(m), t, r, s);
                            vec3 euler = vec3(r[0], r[1], r[2]);
                            vec3 oldEuler = euler;

                            ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
                            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                            ImGui::DragFloat("##RX", &euler.x, 0.1f, 0.0f, 0.0f, "P: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                            ImGui::DragFloat("##RY", &euler.y, 0.1f, 0.0f, 0.0f, "Y: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                            ImGui::DragFloat("##RZ", &euler.z, 0.1f, 0.0f, 0.0f, "R: %.2f"); ImGui::PopItemWidth();
                            ImGui::PopStyleVar(); 

                            if (glm::any(glm::notEqual(euler, oldEuler, 0.001f)))
                            {
                                float matrix[16];
                                ImGuizmo::RecomposeMatrixFromComponents(t, value_ptr(euler), s, matrix);
                                mat4 newMatrix = make_mat4(matrix);
                                quat q = quat_cast(newMatrix);
                                light.rotation = vec4(q.x, q.y, q.z, q.w);
                            } });

            DrawControl("Range", ICON_FA_EXPAND, [&]()
                        { ImGui::DragFloat("##Range", &light.position.w, 0.1f, 0.0f, 1000.0f, "%.2f"); });

            DrawControl("Size", ICON_FA_RULER_HORIZONTAL, [&]()
                        {
                ImGui::PushMultiItemsWidths(2, ImGui::GetContentRegionAvail().x);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});
                ImGui::DragFloat("##W", &light.size.x, 0.1f, 0.0f, 1000.0f, "W: %.2f"); ImGui::PopItemWidth(); ImGui::SameLine();
                ImGui::DragFloat("##H", &light.size.y, 0.1f, 0.0f, 1000.0f, "H: %.2f"); ImGui::PopItemWidth();
                ImGui::PopStyleVar(); });

            ImGui::Columns(1);
        }
    }
} // namespace pe
