#include "GlobalWidget.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "GUI/Helpers.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/RayTracingPass.h"

namespace pe
{
    GlobalWidget::GlobalWidget() : Widget("Global")
    {
    }

    void GlobalWidget::Update()
    {
        if (!m_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        static float rtScale = gSettings.render_scale;

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
        if (ImGui::Button("Apply"))
        {
            gSettings.render_scale = std::clamp(rtScale, 0.1f, 4.0f);
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventType::Resize);
        }
        ImGui::Separator();

        ImGui::Text("Present Mode");
        static vk::PresentModeKHR currentPresentMode = RHII.GetSurface()->GetPresentMode();

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
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();

        static bool dynamic_rendering = gSettings.dynamic_rendering;
        if (ImGui::Checkbox("Dynamic Rendering", &dynamic_rendering))
            EventSystem::PushEvent(EventType::DynamicRendering, dynamic_rendering);
        ImGui::Separator();

        if (gSettings.render_mode != RenderMode::RayTracing)
        {
            ImGui::Checkbox("IBL", &gSettings.IBL);
            if (gSettings.IBL)
            {
                ImGui::Indent(16.0f);
                ImGui::DragFloat("IBL Intensity", &gSettings.IBL_intensity, 0.01f, 0.0f, 10.0f);
                ImGui::Unindent(16.0f);
            }
            ImGui::Checkbox("SSAO", &gSettings.ssao);
            ImGui::Checkbox("SSR", &gSettings.ssr);
        }
        ImGui::Checkbox("Disney PBR", &gSettings.use_Disney_PBR);
        ImGui::Checkbox("FXAA", &gSettings.fxaa);
        ImGui::Checkbox("TAA", &gSettings.taa);
        if (gSettings.taa)
        {
            ImGui::Indent(16.0f);
            ImGui::Checkbox("CAS Sharpening", &gSettings.cas_sharpening);
            if (gSettings.cas_sharpening)
            {
                ImGui::SliderFloat("Sharpness##CAS", &gSettings.cas_sharpness, 0.0f, 1.0f);
            }
            ImGui::Unindent(16.0f);
        }
        ImGui::Checkbox("Tonemapping", &gSettings.tonemapping);
        ImGui::Checkbox("Bloom", &gSettings.bloom);
        if (gSettings.bloom)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Strength##Bloom", &gSettings.bloom_strength, 0.01f, 10.f);
            ImGui::SliderFloat("Range##Bloom", &gSettings.bloom_range, 0.1f, 20.f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Depth of Field", &gSettings.dof);
        if (gSettings.dof)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Scale##DOF", &gSettings.dof_focus_scale, 0.05f, 0.5f);
            ImGui::DragFloat("Range##DOF", &gSettings.dof_blur_range, 0.05f, 0.5f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Motion Blur", &gSettings.motion_blur);
        if (gSettings.motion_blur)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Strength##MB", &gSettings.motion_blur_strength, 0.01f, 1.f);
            ImGui::SliderInt("Samples##MB", &gSettings.motion_blur_samples, 2, 32);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }

        if (ImGui::Checkbox("Day/Night", &gSettings.day))
        {
            if (gSettings.render_mode == RenderMode::Raster)
                gSettings.shadows = gSettings.day;

            LightOpaquePass &lightOpaquePass = *GetGlobalComponent<LightOpaquePass>();
            lightOpaquePass.UpdateDescriptorSets();
            LightTransparentPass &lightTransparentPass = *GetGlobalComponent<LightTransparentPass>();
            lightTransparentPass.UpdateDescriptorSets();
            RayTracingPass &rayTracingPass = *GetGlobalComponent<RayTracingPass>();
            rayTracingPass.UpdateDescriptorSets();
        }

        if (gSettings.render_mode != RenderMode::RayTracing)
        {
            if (ImGui::Checkbox("Cast Shadows", &gSettings.shadows))
            {
                LightOpaquePass &lightOpaquePass = *GetGlobalComponent<LightOpaquePass>();
                lightOpaquePass.UpdateDescriptorSets();
                LightTransparentPass &lightTransparentPass = *GetGlobalComponent<LightTransparentPass>();
                lightTransparentPass.UpdateDescriptorSets();
                RayTracingPass &rayTracingPass = *GetGlobalComponent<RayTracingPass>();
                rayTracingPass.UpdateDescriptorSets();
            }
        }
        else
        {
            gSettings.shadows = gSettings.day;
        }

        if (gSettings.day)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Intst", &gSettings.sun_intensity, 0.1f, 50.f);
            ImGui::DragFloat3("Dir", gSettings.sun_direction.data(), 0.01f, -1.f, 1.f);
            ImGui::DragFloat("Slope", &gSettings.depth_bias[2], 0.15f, 0.5f);
            ImGui::Separator();
            {
                vec3 direction = normalize(make_vec3(&gSettings.sun_direction[0]));
                gSettings.sun_direction[0] = direction.x;
                gSettings.sun_direction[1] = direction.y;
                gSettings.sun_direction[2] = direction.z;
            }
            ImGui::Unindent(16.0f);
        }

        ImGui::DragFloat("TimeScale", &gSettings.time_scale, 0.05f, 0.2f);
        ImGui::Separator();
        if (ImGui::Button("Randomize Lights"))
            gSettings.randomize_lights = true;
        ImGui::SliderFloat("Light Intst", &gSettings.lights_intensity, 0.01f, 30.f);
        ImGui::Checkbox("Frustum Culling", &gSettings.frustum_culling);
        ImGui::Checkbox("FreezeCamCull", &gSettings.freeze_frustum_culling);
        ImGui::Checkbox("Draw AABBs", &gSettings.draw_aabbs);
        if (gSettings.draw_aabbs)
        {
            bool depthAware = gSettings.aabbs_depth_aware;
            ImGui::Indent(16.0f);
            ImGui::Checkbox("Depth Aware", &gSettings.aabbs_depth_aware);
            ImGui::Unindent(16.0f);
        }

        {
            const char *rtModeNames[] = {"Raster", "Hybrid", "Ray Tracing"};
            int currentMode = static_cast<int>(gSettings.render_mode);

            ImGui::Text("Render Mode");
            if (ImGui::Combo("##RenderMode", &currentMode, rtModeNames, 3))
            {
                if (currentMode != static_cast<int>(gSettings.render_mode))
                    EventSystem::PushEvent(EventType::SetRenderMode, static_cast<RenderMode>(currentMode));
            }
        }

        ImGui::End();
    }
} // namespace pe
