#include "GlobalWidget.h"
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

    GlobalWidget::GlobalWidget() : Widget("Global")
    {
    }

    void GlobalWidget::Update()
    {
        if (!m_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        static float rtScale = gSettings.render_scale;
        static float lastCommittedScale = gSettings.render_scale;
        // Sync slider when render_scale was changed externally (e.g. scene load)
        if (gSettings.render_scale != lastCommittedScale)
        {
            rtScale = gSettings.render_scale;
            lastCommittedScale = gSettings.render_scale;
        }

        // Use the helper we moved to Helpers.h
        static bool sized = false;
        if (!sized)
        {
            ui::SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, ImGuiCond_Always);
            sized = true;
        }
        else
        {
            ui::SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, ImGuiCond_Appearing);
        }

        ImGui::Begin("Global", &m_open);

        ImGui::Text("Resolution: %d x %d", static_cast<int>(RHII.GetWidthf() * gSettings.render_scale), static_cast<int>(RHII.GetHeightf() * gSettings.render_scale));
        ImGui::DragFloat("Quality", &rtScale, 0.01f, 0.05f, 1.0f);
        ui::ItemTooltip("Render-scale preview; click Apply to recreate render targets.");
        if (ImGui::Button("Apply"))
        {
            gSettings.render_scale = std::clamp(rtScale, 0.1f, 4.0f);
            lastCommittedScale = gSettings.render_scale;
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventType::Resize);
        }
        ui::ItemTooltip("Apply the current quality scale and resize render targets.");
        ImGui::Separator();

        ImGui::Text("Present Mode");
        PePresentMode currentPresentMode = RHII.GetSurface()->GetPresentMode();

        if (ImGui::BeginCombo("##present_mode", RHII.PresentModeToString(currentPresentMode)))
        {
            const auto &presentModes = RHII.GetSurface()->GetSupportedPresentModes();
            for (uint32_t i = 0; i < static_cast<uint32_t>(presentModes.size()); i++)
            {
                const bool isSelected = (currentPresentMode == presentModes[i]);
                if (ImGui::Selectable(RHII.PresentModeToString(presentModes[i]), isSelected))
                {
                    if (currentPresentMode != presentModes[i])
                    {
                        currentPresentMode = presentModes[i];
                        gSettings.preferred_present_mode = currentPresentMode;
                        EventSystem::PushEvent(EventType::PresentMode);
                    }
                }
                ui::ItemTooltip("Switch swapchain present behavior, such as FIFO vs immediate.");
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("Current swapchain present mode.");
        ImGui::Separator();

        static bool dynamic_rendering = gSettings.dynamic_rendering;
        if (ImGui::Checkbox("Dynamic Rendering", &dynamic_rendering))
            EventSystem::PushEvent(EventType::DynamicRendering, dynamic_rendering);
        ui::ItemTooltip("Toggle Vulkan dynamic rendering path when supported.");
        ImGui::Separator();

        if (gSettings.render_mode != RenderMode::RayTracing)
        {
            ImGui::Checkbox("IBL", &gSettings.IBL);
            ui::ItemTooltip("Use image-based lighting from the active sky environment.");
            if (gSettings.IBL)
            {
                ImGui::Indent(16.0f);
                ImGui::DragFloat("IBL Intensity", &gSettings.IBL_intensity, 0.01f, 0.0f, 10.0f);
                ui::ItemTooltip("Scale the strength of image-based lighting.");
                ImGui::Unindent(16.0f);
            }
            ImGui::Checkbox("SSAO", &gSettings.ssao);
            ui::ItemTooltip("Enable screen-space ambient occlusion in raster modes.");
            ImGui::Checkbox("SSR", &gSettings.ssr);
            ui::ItemTooltip("Enable screen-space reflections in raster modes.");
        }
        ImGui::Checkbox("Disney PBR", &gSettings.use_Disney_PBR);
        ui::ItemTooltip("Use the Disney-style PBR shading model.");
        ImGui::Checkbox("FXAA", &gSettings.fxaa);
        ui::ItemTooltip("Apply fast approximate anti-aliasing as a postprocess.");
        ImGui::Checkbox("TAA", &gSettings.taa);
        ui::ItemTooltip("Apply temporal anti-aliasing using history from previous frames.");
        if (gSettings.taa)
        {
            ImGui::Indent(16.0f);
            ImGui::Checkbox("CAS Sharpening", &gSettings.cas_sharpening);
            ui::ItemTooltip("Sharpen the TAA result with AMD CAS.");
            if (gSettings.cas_sharpening)
            {
                ImGui::SliderFloat("Sharpness##CAS", &gSettings.cas_sharpness, 0.0f, 1.0f);
                ui::ItemTooltip("Strength of the CAS sharpening pass.");
            }
            ImGui::Unindent(16.0f);
        }
        ImGui::Checkbox("Tonemapping", &gSettings.tonemapping);
        ui::ItemTooltip("Convert HDR lighting to display output.");
        ImGui::Checkbox("Bloom", &gSettings.bloom);
        ui::ItemTooltip("Add glow around bright pixels.");
        if (gSettings.bloom)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Strength##Bloom", &gSettings.bloom_strength, 0.01f, 10.f);
            ui::ItemTooltip("Intensity of the bloom composite.");
            ImGui::SliderFloat("Range##Bloom", &gSettings.bloom_range, 0.1f, 20.f);
            ui::ItemTooltip("Brightness range feeding the bloom blur.");
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Depth of Field", &gSettings.dof);
        ui::ItemTooltip("Blur pixels away from the camera focus distance.");
        if (gSettings.dof)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Scale##DOF", &gSettings.dof_focus_scale, 0.05f, 0.5f);
            ui::ItemTooltip("Controls how focus distance maps to blur amount.");
            ImGui::DragFloat("Range##DOF", &gSettings.dof_blur_range, 0.05f, 0.5f);
            ui::ItemTooltip("Width of the depth range that stays near focus.");
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Motion Blur", &gSettings.motion_blur);
        ui::ItemTooltip("Blur moving pixels using per-frame motion vectors.");
        if (gSettings.motion_blur)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Strength##MB", &gSettings.motion_blur_strength, 0.01f, 1.f);
            ui::ItemTooltip("Strength of the motion blur accumulation.");
            ImGui::SliderInt("Samples##MB", &gSettings.motion_blur_samples, 2, 32);
            ui::ItemTooltip("Number of taps used by the motion blur pass.");
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }

        if (gSettings.render_mode != RenderMode::RayTracing)
        {
            if (ImGui::Checkbox("Cast Shadows", &gSettings.shadows))
            {
                UpdateLightingDescriptorSets();
            }
            ui::ItemTooltip("Enable shadow maps for raster lighting.");
        }

        if (gSettings.render_mode != RenderMode::RayTracing && gSettings.shadows)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Distance##Shadow", &gSettings.shadow_distance, 5.0f, 10.0f, 1000.0f);
            ui::ItemTooltip("Maximum camera distance covered by directional shadows.");
            ImGui::SliderFloat("Cascade Split##Shadow", &gSettings.shadow_cascade_lambda, 0.0f, 1.0f);
            ui::ItemTooltip("Distribution bias between near and far shadow cascades.");
            ImGui::DragFloat("Slope", &gSettings.depth_bias[2], 0.15f, 0.5f);
            ui::ItemTooltip("Slope-scaled depth bias used to reduce shadow acne.");
            ImGui::DragFloat("Normal Bias##Shadow", &gSettings.shadow_normal_bias, 0.05f, 0.0f, 10.0f);
            ui::ItemTooltip("Normal-offset bias used to reduce self-shadowing.");
            ImGui::SliderFloat("Fade##Shadow", &gSettings.shadow_fade_fraction, 0.0f, 0.5f);
            ui::ItemTooltip("Fraction of the shadow distance used for fade-out.");
            ImGui::SliderFloat("Filter##Shadow", &gSettings.shadow_filter_radius, 0.0f, 3.0f);
            ui::ItemTooltip("Radius of the shadow filtering kernel.");
            const char *shadowDebugModes[] = {"Off", "Cascades", "Shadow Factor"};
            ImGui::Combo("Debug##Shadow", &gSettings.shadow_debug_mode, shadowDebugModes, IM_ARRAYSIZE(shadowDebugModes));
            ui::ItemTooltip("Visualize shadow cascade or shadow factor debug output.");
            ImGui::Separator();
            ImGui::Unindent(16.0f);
        }

        ImGui::DragFloat("TimeScale", &gSettings.time_scale, 0.05f, 0.2f);
        ui::ItemTooltip("Scale simulation time used by editor-updated systems.");
        ImGui::Separator();
        if (ImGui::Button("Randomize Lights"))
            gSettings.randomize_lights = true;
        ui::ItemTooltip("Request a one-shot randomization of scene light settings.");
        ImGui::SliderFloat("Light Intst", &gSettings.lights_intensity, 0.01f, 30.f);
        ui::ItemTooltip("Global multiplier for scene light intensity.");
        ImGui::Checkbox("Physical Falloff", &gSettings.physical_point_falloff);
        ui::ItemTooltip("Point lights attenuate by windowed inverse-square (raster path). Intensity is interpreted as luminance * distance^2, so expect to raise it massively.");
        ImGui::Checkbox("Frustum Culling", &gSettings.frustum_culling);
        ui::ItemTooltip("Cull renderables outside the active camera frustum.");
        ImGui::Checkbox("FreezeCamCull", &gSettings.freeze_frustum_culling);
        ui::ItemTooltip("Freeze the culling camera to inspect culling behavior.");
        ImGui::Checkbox("Draw AABBs", &gSettings.draw_aabbs);
        ui::ItemTooltip("Draw debug axis-aligned bounding boxes for scene nodes.");
        if (gSettings.draw_aabbs)
        {
            ImGui::Indent(16.0f);
            ImGui::Checkbox("Depth Aware", &gSettings.aabbs_depth_aware);
            ui::ItemTooltip("Depth-test debug AABBs against the scene.");
            ImGui::Unindent(16.0f);
        }

        {
            const char *rtModeNames[] = {"Raster", "Hybrid", "Ray Tracing"};
            const bool rtSupported = RHII.GetCaps().rayTracing;
            int currentMode = static_cast<int>(ClampRenderModeToRayTracingSupport(gSettings.render_mode, rtSupported));
            const int modeCount = rtSupported ? 3 : 1;

            ImGui::Text("Render Mode");
            if (ImGui::Combo("##RenderMode", &currentMode, rtModeNames, modeCount))
            {
                if (currentMode != static_cast<int>(gSettings.render_mode))
                    EventSystem::PushEvent(EventType::SetRenderMode, static_cast<RenderMode>(currentMode));
            }
            ui::ItemTooltip("Switch between raster, hybrid, and full ray-tracing render paths.");
        }

        ImGui::End();
    }
} // namespace pe
