#include "SceneSettingsControls.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "GUI/Helpers.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/RayTracingPass.h"

namespace pe
{
    static void UpdateLightingDescriptorSets()
    {
        if (auto *pass = GetGlobalComponent<LightOpaquePass>())
            pass->UpdateDescriptorSets();
        if (auto *pass = GetGlobalComponent<LightTransparentPass>())
            pass->UpdateDescriptorSets();
        if (auto *pass = GetGlobalComponent<RayTracingPass>())
            pass->UpdateDescriptorSets();
    }

    bool DrawSceneSettingsControls()
    {
        auto &gSettings = Settings::Get<SceneSettings>();
        bool changed = false;
        auto Track = [&changed](bool c)
        {
            changed = changed || c;
            return c;
        };

        // Render scale (preview + Apply -> recreate render targets)
        static float rtScale = gSettings.render_scale;
        static float lastCommittedScale = gSettings.render_scale;
        if (gSettings.render_scale != lastCommittedScale)
        {
            rtScale = gSettings.render_scale;
            lastCommittedScale = gSettings.render_scale;
        }
        ImGui::Text("Resolution: %d x %d", static_cast<int>(RHII.GetWidthf() * gSettings.render_scale),
                    static_cast<int>(RHII.GetHeightf() * gSettings.render_scale));
        ImGui::DragFloat("Quality", &rtScale, 0.01f, 0.05f, 1.0f);
        ui::ItemTooltip("Render-scale preview; click Apply to recreate render targets.");
        if (ImGui::Button("Apply"))
        {
            gSettings.render_scale = std::clamp(rtScale, 0.1f, 4.0f);
            lastCommittedScale = gSettings.render_scale;
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventType::Resize);
            changed = true;
        }
        ui::ItemTooltip("Apply the current quality scale and resize render targets.");
        ImGui::Separator();

        // Present mode (scene setting is authoritative; apply via swapchain recreate).
        ImGui::Text("Present Mode");
        PePresentMode currentPresentMode = RHII.GetSurface()->GetPresentMode();
        if (ImGui::BeginCombo("##present_mode", RHII.PresentModeToString(currentPresentMode)))
        {
            const auto &presentModes = RHII.GetSurface()->GetSupportedPresentModes();
            for (uint32_t i = 0; i < static_cast<uint32_t>(presentModes.size()); i++)
            {
                const bool isSelected = (currentPresentMode == presentModes[i]);
                if (ImGui::Selectable(RHII.PresentModeToString(presentModes[i]), isSelected) &&
                    currentPresentMode != presentModes[i])
                {
                    RHII.ChangePresentMode(presentModes[i]);
                    changed = true;
                }
                ui::ItemTooltip("Scene present mode — saved with the scene; overrides editor_config / PE_PRESENT_MODE.");
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("Swapchain present mode from scene settings (FIFO, mailbox, immediate, …).");

        static bool dynamic_rendering = gSettings.dynamic_rendering;
        if (Track(ImGui::Checkbox("Dynamic Rendering", &dynamic_rendering)))
            EventSystem::PushEvent(EventType::DynamicRendering, dynamic_rendering);
        ui::ItemTooltip("Toggle Vulkan dynamic rendering path when supported.");

        {
            const char *rtModeNames[] = {"Raster", "Hybrid", "Ray Tracing"};
            const bool rtSupported = RHII.GetCaps().rayTracing;
            int currentMode = static_cast<int>(ClampRenderModeToRayTracingSupport(gSettings.render_mode, rtSupported));
            const int modeCount = rtSupported ? 3 : 1;
            ImGui::Text("Render Mode");
            if (ImGui::Combo("##RenderMode", &currentMode, rtModeNames, modeCount) &&
                currentMode != static_cast<int>(gSettings.render_mode))
            {
                changed = true;
                EventSystem::PushEvent(EventType::SetRenderMode, static_cast<RenderMode>(currentMode));
            }
            ui::ItemTooltip("Switch between raster, hybrid, and full ray-tracing render paths.");
        }
        ImGui::Separator();

        // Lighting / shading (raster paths)
        if (gSettings.render_mode != RenderMode::RayTracing)
        {
            if (Track(ImGui::Checkbox("Forward+", &gSettings.forward_plus)))
                UpdateLightingDescriptorSets();
            ui::ItemTooltip("Cull point and spot lights into screen-space tiles before raster lighting.");
        }
        Track(ImGui::Checkbox("Disney PBR", &gSettings.use_Disney_PBR));
        ui::ItemTooltip("Use the Disney-style PBR shading model.");

        if (gSettings.render_mode != RenderMode::RayTracing)
        {
            if (Track(ImGui::Checkbox("Cast Shadows", &gSettings.shadows)))
                UpdateLightingDescriptorSets();
            ui::ItemTooltip("Enable shadow maps for raster lighting.");
            if (gSettings.shadows)
            {
                ImGui::Indent(16.0f);
                Track(ImGui::DragFloat("Distance##Shadow", &gSettings.shadow_distance, 5.0f, 10.0f, 1000.0f));
                ui::ItemTooltip("Maximum camera distance covered by directional shadows.");
                Track(ImGui::SliderFloat("Cascade Split##Shadow", &gSettings.shadow_cascade_lambda, 0.0f, 1.0f));
                ui::ItemTooltip("Distribution bias between near and far shadow cascades.");
                Track(ImGui::DragFloat("Slope", &gSettings.depth_bias[2], 0.15f, 0.5f));
                ui::ItemTooltip("Slope-scaled depth bias used to reduce shadow acne.");
                Track(ImGui::DragFloat("Normal Bias##Shadow", &gSettings.shadow_normal_bias, 0.05f, 0.0f, 10.0f));
                ui::ItemTooltip("Normal-offset bias used to reduce self-shadowing.");
                Track(ImGui::SliderFloat("Fade##Shadow", &gSettings.shadow_fade_fraction, 0.0f, 0.5f));
                ui::ItemTooltip("Fraction of the shadow distance used for fade-out.");
                Track(ImGui::SliderFloat("Filter##Shadow", &gSettings.shadow_filter_radius, 0.0f, 3.0f));
                ui::ItemTooltip("Radius of the shadow filtering kernel.");
                const char *shadowDebugModes[] = {"Off", "Cascades", "Shadow Factor"};
                Track(ImGui::Combo("Debug##Shadow", &gSettings.shadow_debug_mode, shadowDebugModes,
                                   IM_ARRAYSIZE(shadowDebugModes)));
                ui::ItemTooltip("Visualize shadow cascade or shadow factor debug output.");
                ImGui::Unindent(16.0f);
            }
        }
        ImGui::Separator();

        // Lights
        if (ImGui::Button("Randomize Lights"))
            gSettings.randomize_lights = true;
        ui::ItemTooltip("Request a one-shot randomization of scene light settings.");
        Track(ImGui::SliderFloat("Light Intst", &gSettings.lights_intensity, 0.01f, 30.f));
        ui::ItemTooltip("Global multiplier for scene light intensity.");
        Track(ImGui::Checkbox("Physical Falloff", &gSettings.physical_point_falloff));
        ui::ItemTooltip("Point lights attenuate by windowed inverse-square (raster path). Intensity is interpreted "
                        "as luminance * distance^2, so expect to raise it massively.");
        Track(ImGui::Checkbox("Distance Haze", &gSettings.fog));
        ui::ItemTooltip("Exponential fog toward the skybox color past the start distance. Softens the horizon "
                        "and masks far voxel LOD transitions.");
        if (gSettings.fog)
        {
            ImGui::Indent(16.0f);
            ImGui::SetNextItemWidth(120.0f);
            Track(ImGui::DragFloat("Density##Fog", &gSettings.fog_density, 0.0005f, 0.0f, 0.1f, "%.4f"));
            ui::ItemTooltip("Exponential falloff rate per world unit past the start distance.");
            ImGui::SetNextItemWidth(120.0f);
            Track(ImGui::DragFloat("Start##Fog", &gSettings.fog_start, 1.0f, 0.0f, 100000.0f));
            ui::ItemTooltip("World-unit distance where the haze begins.");
            ImGui::Unindent(16.0f);
        }
        ImGui::Separator();

        // Culling / debug
        Track(ImGui::Checkbox("Frustum Culling", &gSettings.frustum_culling));
        ui::ItemTooltip("Cull renderables outside the active camera frustum.");
        Track(ImGui::Checkbox("Occlusion Culling (Hi-Z)", &gSettings.occlusion_culling));
        ui::ItemTooltip("GPU Hi-Z occlusion culling: skip opaque draws hidden behind this frame's depth.");
        if (gSettings.occlusion_culling)
        {
            ImGui::Indent(16.0f);
            ImGui::SetNextItemWidth(120.0f);
            Track(ImGui::DragFloat("Occlusion Bias", &gSettings.occlusion_culling_bias, 0.0005f, 0.0f, 0.05f, "%.4f"));
            ui::ItemTooltip("Hi-Z slack as a FRACTION of occluder depth (0.002 = 0.2%). Higher = more conservative.");
            ImGui::Unindent(16.0f);
        }
        Track(ImGui::Checkbox("Mesh LOD", &gSettings.lod_enabled));
        ui::ItemTooltip("Discrete mesh level-of-detail: the GPU cull pass swaps each mesh to a simpler index "
                        "set chosen by camera distance. Levels are generated at load via meshopt.");
        if (gSettings.lod_enabled)
        {
            ImGui::Indent(16.0f);
            int lodCount = static_cast<int>(gSettings.lod_count);
            ImGui::SetNextItemWidth(120.0f);
            if (Track(ImGui::SliderInt("Levels (on load)", &lodCount, 1, 4))) // 4 == Mesh::kMaxLods
                gSettings.lod_count = static_cast<uint32_t>(lodCount);
            ui::ItemTooltip("LODs generated per mesh. Applies to newly loaded meshes — reload the scene to regenerate.");
            ImGui::SetNextItemWidth(200.0f);
            Track(ImGui::DragFloat3("Switch Distances", gSettings.lod_distances.data(), 1.0f, 0.0f, 100000.0f));
            ui::ItemTooltip("World-unit camera distances to switch LOD0->1, 1->2, 2->3 (live).");
            ImGui::SetNextItemWidth(120.0f);
            Track(ImGui::DragFloat("Distance Bias", &gSettings.lod_bias, 0.01f, 0.1f, 10.0f));
            ui::ItemTooltip("Multiplies measured distance before the switch test (>1 = drop detail sooner). Live.");
            ImGui::Unindent(16.0f);
        }
        Track(ImGui::Checkbox("FreezeCamCull", &gSettings.freeze_frustum_culling));
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
            Track(ImGui::ColorEdit4("Color##SelectionOutline", outlineColor));
            gSettings.selection_outline_color_r = outlineColor[0];
            gSettings.selection_outline_color_g = outlineColor[1];
            gSettings.selection_outline_color_b = outlineColor[2];
            gSettings.selection_outline_color_a = outlineColor[3];
            ui::ItemTooltip("Tint and opacity for selected-object outlines.");
            Track(ImGui::SliderFloat("Thickness##SelectionOutline", &gSettings.selection_outline_thickness, 0.0f, 32.0f, "%.1f px"));
            ui::ItemTooltip("Solid outer outline width in pixels.");
            Track(ImGui::SliderFloat("Inner Fade##SelectionOutline", &gSettings.selection_outline_inner_fade, 0.0f, 32.0f, "%.1f px"));
            ui::ItemTooltip("Distance the outline fades inward over the selected object.");
            Track(ImGui::SliderFloat("Outer Fade##SelectionOutline", &gSettings.selection_outline_outer_fade, 0.0f, 32.0f, "%.1f px"));
            ui::ItemTooltip("Distance the outline fades outward from the selected object.");
            ImGui::Unindent(16.0f);
        }
        ImGui::Separator();

        Track(ImGui::DragFloat("TimeScale", &gSettings.time_scale, 0.05f, 0.2f));
        ui::ItemTooltip("Scale simulation time used by editor-updated systems.");

        return changed;
    }
} // namespace pe
