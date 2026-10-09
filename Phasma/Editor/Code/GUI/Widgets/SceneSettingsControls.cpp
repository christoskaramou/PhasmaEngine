#include "SceneSettingsControls.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "GUI/GUIState.h"
#include "Systems/NavigationSystem.h"
#include "Systems/PhysicsSystem.h"

namespace pe
{
    bool DrawSceneSettingsControls()
    {
        auto &gSettings = Settings::Get<SceneSettings>();
        bool changed = false;
        auto Track = [&changed](bool c)
        {
            changed = changed || c;
            return c;
        };

        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static float rtScale = gSettings.render_scale;
            static float lastCommittedScale = gSettings.render_scale;
            if (gSettings.render_scale != lastCommittedScale)
            {
                rtScale = gSettings.render_scale;
                lastCommittedScale = gSettings.render_scale;
            }
            ImGui::Text("Resolution: %d x %d", static_cast<int>(RHII.GetWidthf() * gSettings.render_scale),
                        static_cast<int>(RHII.GetHeightf() * gSettings.render_scale));
            ImGui::DragFloat(ui::LabelAbove("Quality"), &rtScale, 0.01f, kMinRenderScale, kMaxRenderScale);
            ui::ItemTooltip("Render-scale preview; click Apply to recreate render targets.");
            if (ImGui::Button("Apply"))
            {
                gSettings.render_scale = ClampRenderScale(rtScale);
                lastCommittedScale = gSettings.render_scale;
                changed = true;
            }
            ui::ItemTooltip("Apply the current quality scale and resize render targets.");
            ImGui::Separator();

            // Present mode (scene setting is authoritative; apply via swapchain recreate).
            PePresentMode currentPresentMode = RHII.GetSurface()->GetPresentMode();
            if (ImGui::BeginCombo(ui::LabelAbove("Present Mode"), RHII.PresentModeToString(currentPresentMode)))
            {
                const auto &presentModes = RHII.GetSurface()->GetSupportedPresentModes();
                for (uint32_t i = 0; i < static_cast<uint32_t>(presentModes.size()); i++)
                {
                    const bool isSelected = (currentPresentMode == presentModes[i]);
                    if (ImGui::Selectable(RHII.PresentModeToString(presentModes[i]), isSelected) &&
                        currentPresentMode != presentModes[i])
                    {
                        gSettings.preferred_present_mode = presentModes[i];
                        EventSystem::PushEvent(EventType::PresentMode);
                        changed = true;
                    }
                    ui::ItemTooltip("Presentation mode for the current preview or scene.");
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ui::ItemTooltip("Swapchain present mode from scene settings (FIFO, mailbox, immediate, …).");
        }

        if (ImGui::CollapsingHeader("Atmosphere"))
        {
            Track(ImGui::Checkbox("Distance Haze", &gSettings.fog));
            ui::ItemTooltip("Exponential fog toward the skybox color past the start distance. Softens the horizon "
                            "and masks far voxel LOD transitions.");
            if (gSettings.fog)
            {
                ImGui::Indent(16.0f);
                Track(ImGui::DragFloat(ui::LabelAbove("Density##Fog"), &gSettings.fog_density, 0.0005f, 0.0f, 0.1f, "%.4f"));
                ui::ItemTooltip("Exponential falloff rate per world unit past the start distance.");
                Track(ImGui::DragFloat(ui::LabelAbove("Start##Fog"), &gSettings.fog_start, 1.0f, 0.0f, 100000.0f));
                ui::ItemTooltip("World-unit distance where the haze begins.");
                ImGui::Unindent(16.0f);
            }
        }

        if (ImGui::CollapsingHeader("Debug & Overlays"))
        {
            Track(ImGui::Checkbox("Freeze Culling Camera", &gSettings.freeze_frustum_culling));
            ui::ItemTooltip("Freeze the culling camera to inspect culling behavior.");
            Track(ImGui::Checkbox("Draw AABBs", &gSettings.draw_aabbs));
            ui::ItemTooltip("Draw debug axis-aligned bounding boxes for scene nodes.");
            if (gSettings.draw_aabbs)
            {
                ImGui::Indent(16.0f);
                Track(ImGui::Checkbox("Depth Aware", &gSettings.aabbs_depth_aware));
                ui::ItemTooltip("Depth-test debug AABBs against the scene.");
                ImGui::Unindent(16.0f);
            }
            Track(ImGui::Checkbox("Selection Outline", &gSettings.selection_outline));
            ui::ItemTooltip("Draw a screen-space outline around selected scene nodes.");
            if (gSettings.selection_outline)
            {
                ImGui::Indent(16.0f);
                float outlineColor[4] = {
                    gSettings.selection_outline_color_r,
                    gSettings.selection_outline_color_g,
                    gSettings.selection_outline_color_b,
                    gSettings.selection_outline_color_a,
                };
                Track(ImGui::ColorEdit4(ui::LabelAbove("Color##SelectionOutline"), outlineColor));
                gSettings.selection_outline_color_r = outlineColor[0];
                gSettings.selection_outline_color_g = outlineColor[1];
                gSettings.selection_outline_color_b = outlineColor[2];
                gSettings.selection_outline_color_a = outlineColor[3];
                ui::ItemTooltip("Tint and opacity for selected-object outlines.");
                Track(ImGui::SliderFloat(ui::LabelAbove("Thickness##SelectionOutline"), &gSettings.selection_outline_thickness, 0.0f, 32.0f, "%.1f px"));
                ui::ItemTooltip("Solid outer outline width in pixels.");
                Track(ImGui::SliderFloat(ui::LabelAbove("Inner Fade##SelectionOutline"), &gSettings.selection_outline_inner_fade, 0.0f, 32.0f, "%.1f px"));
                ui::ItemTooltip("Distance the outline fades inward over the selected object.");
                Track(ImGui::SliderFloat(ui::LabelAbove("Outer Fade##SelectionOutline"), &gSettings.selection_outline_outer_fade, 0.0f, 32.0f, "%.1f px"));
                ui::ItemTooltip("Distance the outline fades outward from the selected object.");
                ImGui::Unindent(16.0f);
            }
        }

        if (ImGui::CollapsingHeader("Simulation & Physics"))
        {
            Track(ImGui::DragFloat(ui::LabelAbove("Time Scale"), &gSettings.time_scale, 0.05f, 0.2f));
            ui::ItemTooltip("Scale simulation time used by editor-updated systems.");
            static constexpr uint32_t kPhysicsRateMin = 10, kPhysicsRateMax = 240;
            Track(ImGui::SliderScalar(ui::LabelAbove("Physics Rate"), ImGuiDataType_U32, &gSettings.physics_rate, &kPhysicsRateMin,
                                      &kPhysicsRateMax, "%u Hz"));
            ui::ItemTooltip("Fixed 3D physics steps per second. Higher is smoother and more accurate for fast bodies, and costs more CPU.");

            const bool layersOpen = ImGui::TreeNode("Physics Layers");
            ui::ItemTooltip("Name the 3D physics layers and tick which pairs collide. Unticked pairs pass through each "
                            "other and trigger no overlap events. Bodies pick their layer in the Physics component.");
            if (layersOpen)
            {
                auto &names = gSettings.physics_layer_names;
                auto &ignore = gSettings.physics_layer_ignore;
                uint32_t used[SceneSettings::kPhysicsLayerCount];
                int count = 0;
                for (uint32_t i = 0; i < SceneSettings::kPhysicsLayerCount; ++i)
                    if (i == 0 || !names[i].empty())
                        used[count++] = i;
                uint32_t removeLayer = 0; // set by a row's remove button; applied after the table

                // Rows are layers (name editable), columns the same layers under angled headers; the lower triangle
                // shows each pair once.
                if (ImGui::BeginTable("##physics_layers", count + 1, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV))
                {
                    ImGui::TableSetupColumn("##layer", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
                    for (int c = 0; c < count; ++c)
                        ImGui::TableSetupColumn(names[used[c]].c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableAngledHeadersRow();
                    for (int r = 0; r < count; ++r)
                    {
                        const uint32_t i = used[r];
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        const float removeSize = ImGui::GetFrameHeight();
                        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
                        ImGui::SetNextItemWidth(i == 0 ? -FLT_MIN : -(removeSize + gap));
                        char name[64];
                        snprintf(name, sizeof(name), "%s", names[i].c_str());
                        // A name never goes empty here: an empty name drops the row, mid-edit included.
                        if (ImGui::InputText("##name", name, sizeof(name), i == 0 ? ImGuiInputTextFlags_ReadOnly : 0) && name[0])
                        {
                            names[i] = name;
                            changed = true;
                        }
                        if (i != 0)
                        {
                            ImGui::SameLine(0.0f, gap);
                            if (ui::CenteredIconButton("##remove", ICON_FA_MINUS, ImVec2(removeSize, removeSize)))
                                removeLayer = i;
                            ui::ItemTooltip("Remove this layer. Bodies on it in this scene move to Default.");
                        }
                        for (int c = 0; c <= r; ++c)
                        {
                            ImGui::TableNextColumn();
                            const uint32_t j = used[c];
                            bool collide = (((ignore[i] >> j) | (ignore[j] >> i)) & 1u) == 0;
                            ImGui::PushID(static_cast<int>(j));
                            if (Track(ImGui::Checkbox("##collide", &collide)))
                            {
                                const uint32_t bitI = 1u << i, bitJ = 1u << j;
                                ignore[i] = collide ? ignore[i] & ~bitJ : ignore[i] | bitJ;
                                ignore[j] = collide ? ignore[j] & ~bitI : ignore[j] | bitI;
                            }
                            ui::ItemTooltip("Do these two layers collide?");
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (removeLayer != 0)
                {
                    // Free the slot completely, so a later Add Layer reuses it with no old pairs or bodies attached.
                    names[removeLayer].clear();
                    ignore[removeLayer] = 0;
                    for (uint32_t &bits : ignore)
                        bits &= ~(1u << removeLayer);
#ifdef PE_PHYSICS
                    PhysicsSystem *physics = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    for (uint32_t n = 0; physics && scene && n < scene->GetNodeCount(); ++n)
                    {
                        NodeId *node = scene->GetNodeId(n);
                        if (!(scene->GetComponentFlags(node) & Component_Physics))
                            continue;
                        if (const PhysicsBodyDesc *desc = physics->GetBodyDesc(node); desc && desc->layer == removeLayer)
                            physics->SetBodyLayer(node, 0);
                    }
#endif
                    changed = true;
                }
                if (count < static_cast<int>(SceneSettings::kPhysicsLayerCount) && ImGui::Button("Add Layer"))
                {
                    for (uint32_t i = 1; i < SceneSettings::kPhysicsLayerCount; ++i)
                    {
                        if (names[i].empty())
                        {
                            names[i] = "Layer " + std::to_string(i);
                            changed = true;
                            break;
                        }
                    }
                }
                ui::ItemTooltip("Name the next free layer (up to 32). Rename it in its row.");
                ImGui::TreePop();
            }
        }

        // The Animator shows scene settings without a navigation system: no section there.
        const bool hasNav = GetGlobalSystem<NavigationSystem>() != nullptr;
        const bool navOpen = hasNav && ImGui::CollapsingHeader("Navigation");
        if (hasNav)
            ui::ItemTooltip("Bake a navigation mesh from the scene's static physics colliders, for nav.find_path and agents. "
                            "The agent size decides how close to walls and how high a step the mesh allows.");
        if (navOpen)
        {
            Track(ImGui::DragFloat(ui::LabelAbove("Agent Radius"), &gSettings.nav_agent_radius, 0.01f, 0.05f, 5.0f, "%.2f m"));
            ui::ItemTooltip("Walls and props are kept this far from the mesh edge.");
            Track(ImGui::DragFloat(ui::LabelAbove("Agent Height"), &gSettings.nav_agent_height, 0.01f, 0.1f, 10.0f, "%.2f m"));
            ui::ItemTooltip("Ceilings lower than this block the way.");
            Track(ImGui::DragFloat(ui::LabelAbove("Max Climb"), &gSettings.nav_agent_climb, 0.01f, 0.0f, 5.0f, "%.2f m"));
            ui::ItemTooltip("Steps up to this height are walkable.");
            Track(ImGui::DragFloat(ui::LabelAbove("Max Slope"), &gSettings.nav_agent_slope, 0.5f, 0.0f, 89.0f, "%.0f deg"));
            Track(ImGui::DragFloat(ui::LabelAbove("Cell Size"), &gSettings.nav_cell_size, 0.005f, 0.05f, 2.0f, "%.3f m"));
            ui::ItemTooltip("Horizontal voxel size: smaller follows edges more closely and bakes slower.");
            Track(ImGui::DragFloat(ui::LabelAbove("Cell Height"), &gSettings.nav_cell_height, 0.005f, 0.02f, 1.0f, "%.3f m"));
            static std::string s_navStatus;
            if (ImGui::Button("Bake"))
            {
                std::string error = "navigation is unavailable";
                auto *nav = GetGlobalSystem<NavigationSystem>();
                if (nav && nav->Bake(NavigationSystem::SceneSettingsForBake(), error))
                {
                    const NavMeshStats &stats = nav->Mesh()->Stats();
                    s_navStatus = std::to_string(stats.polygons) + " polygons in " +
                                  std::to_string(static_cast<int>(stats.bakeMs + 0.5f)) + " ms";
                    GUIState::s_useNavMeshGizmos = true;
                }
                else
                {
                    s_navStatus = error;
                }
            }
            ui::ItemTooltip("Bake now (held in memory; bake again after moving colliders, or from Lua with nav.bake()).");
            ImGui::SameLine();
            ImGui::TextUnformatted(s_navStatus.c_str());
            ImGui::Checkbox("Show NavMesh", &GUIState::s_useNavMeshGizmos);
        }

        return changed;
    }
} // namespace pe
