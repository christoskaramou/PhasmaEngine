#include "PostProcessControls.h"
#include "GUI/Helpers.h"

namespace pe
{
    bool DrawPostProcessControls(PostProcessProfile &pp)
    {
        bool changed = false;
        auto T = [&](bool c)
        {
            changed = changed || c;
        };

        T(ImGui::Checkbox("IBL", &pp.IBL));
        ui::ItemTooltip("Image-based lighting from the active sky environment.");
        if (pp.IBL)
        {
            ImGui::Indent(16.0f);
            T(ImGui::DragFloat(ui::LabelAbove("IBL Intensity"), &pp.IBL_intensity, 0.01f, 0.0f, 10.0f));
            ImGui::Unindent(16.0f);
        }

        T(ImGui::Checkbox("SSAO", &pp.ssao));
        if (pp.ssao)
        {
            ImGui::Indent(16.0f);
            T(ImGui::SliderFloat(ui::LabelAbove("Radius##SSAO"), &pp.ssao_radius, 0.05f, 3.0f));
            T(ImGui::SliderFloat(ui::LabelAbove("Intensity##SSAO"), &pp.ssao_intensity, 0.0f, 2.0f));
            T(ImGui::SliderFloat(ui::LabelAbove("Bias##SSAO"), &pp.ssao_bias, 0.0f, 0.2f));
            T(ImGui::SliderFloat(ui::LabelAbove("Power##SSAO"), &pp.ssao_power, 0.25f, 4.0f));
            T(ImGui::SliderInt(ui::LabelAbove("Samples##SSAO"), &pp.ssao_samples, 4, 64));
            ImGui::Unindent(16.0f);
        }

        T(ImGui::Checkbox("SSR", &pp.ssr));

        T(ImGui::Checkbox("FXAA", &pp.fxaa));
        T(ImGui::Checkbox("TAA", &pp.taa));
        if (pp.taa)
        {
            ImGui::Indent(16.0f);
            T(ImGui::Checkbox("CAS Sharpening", &pp.cas_sharpening));
            if (pp.cas_sharpening)
                T(ImGui::SliderFloat(ui::LabelAbove("Sharpness##CAS"), &pp.cas_sharpness, 0.0f, 1.0f));
            ImGui::Unindent(16.0f);
        }

        T(ImGui::Checkbox("Tonemapping", &pp.tonemapping));

        T(ImGui::Checkbox("Color Grading", &pp.color_grading));
        if (pp.color_grading)
        {
            // lift/gamma/gain r,g,b are stored contiguously so DragFloat3 edits each triplet.
            ImGui::Indent(16.0f);
            T(ImGui::DragFloat3(ui::LabelAbove("Lift##CG"), &pp.color_grading_lift_r, 0.01f, -1.0f, 1.0f));
            T(ImGui::DragFloat3(ui::LabelAbove("Gamma##CG"), &pp.color_grading_gamma_r, 0.01f, 0.05f, 4.0f));
            T(ImGui::DragFloat3(ui::LabelAbove("Gain##CG"), &pp.color_grading_gain_r, 0.01f, 0.0f, 4.0f));
            T(ImGui::SliderFloat(ui::LabelAbove("Saturation##CG"), &pp.color_grading_saturation, 0.0f, 2.0f));
            T(ImGui::SliderFloat(ui::LabelAbove("Contrast##CG"), &pp.color_grading_contrast, 0.0f, 2.0f));
            T(ImGui::SliderFloat(ui::LabelAbove("Intensity##CG"), &pp.color_grading_intensity, 0.0f, 1.0f));
            char maskBuf[256];
            snprintf(maskBuf, sizeof(maskBuf), "%s", pp.color_grading_mask.c_str());
            if (ImGui::InputText(ui::LabelAbove("Strength Mask##CG"), maskBuf, sizeof(maskBuf)))
            {
                pp.color_grading_mask = maskBuf;
                T(true);
            }
            ImGui::Unindent(16.0f);
        }

        T(ImGui::Checkbox("Bloom", &pp.bloom));
        if (pp.bloom)
        {
            ImGui::Indent(16.0f);
            T(ImGui::SliderFloat(ui::LabelAbove("Strength##Bloom"), &pp.bloom_strength, 0.01f, 10.f));
            T(ImGui::SliderFloat(ui::LabelAbove("Range##Bloom"), &pp.bloom_range, 0.1f, 20.f));
            ImGui::Unindent(16.0f);
        }

        T(ImGui::Checkbox("Depth of Field", &pp.dof));
        if (pp.dof)
        {
            ImGui::Indent(16.0f);
            T(ImGui::DragFloat(ui::LabelAbove("Scale##DOF"), &pp.dof_focus_scale, 0.05f, 0.5f));
            T(ImGui::DragFloat(ui::LabelAbove("Range##DOF"), &pp.dof_blur_range, 0.05f, 0.5f));
            ImGui::Unindent(16.0f);
        }

        T(ImGui::Checkbox("Motion Blur", &pp.motion_blur));
        if (pp.motion_blur)
        {
            ImGui::Indent(16.0f);
            T(ImGui::SliderFloat(ui::LabelAbove("Strength##MB"), &pp.motion_blur_strength, 0.01f, 1.f));
            T(ImGui::SliderInt(ui::LabelAbove("Samples##MB"), &pp.motion_blur_samples, 2, 32));
            ImGui::Unindent(16.0f);
        }

        return changed;
    }
} // namespace pe
