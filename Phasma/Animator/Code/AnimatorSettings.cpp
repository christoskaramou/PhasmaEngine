#include "AnimatorApp.h"
#include "Camera/Camera.h"
#include "GUI/Helpers.h"
#include "GUI/Widgets/PostProcessControls.h"
#include "GUI/Widgets/SceneSettingsControls.h"

namespace pe
{
    void AnimatorApp::DrawSettings()
    {
        if (!m_showSettings)
            return;
        const float em = ImGui::GetFontSize();
        ImGui::SetNextWindowSize({em * 43.f, em * 45.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({em * 30.f, em * 16.f}, ImGui::GetIO().DisplaySize);
        if (m_settingsTab >= 0)
            ImGui::SetNextWindowFocus();
        if (!ImGui::Begin("Animator Settings", &m_showSettings, ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::End();
            return;
        }
        ImGui::TextDisabled("Preview settings apply to this Animator session.");
        auto &gs = Settings::Get<SceneSettings>();
        if (ImGui::BeginTabBar("SettingsTabs"))
        {
            const char *tabs[] = {"Rendering", "Post Processing", "Environment & Camera"};
            for (int i = 0; i < IM_ARRAYSIZE(tabs); ++i)
            {
                if (!ImGui::BeginTabItem(tabs[i], nullptr, m_settingsTab == i ? ImGuiTabItemFlags_SetSelected : 0))
                    continue;
                ImGui::BeginChild("Controls", {0.f, 0.f});
                ImGui::PushItemWidth(em * 17.f);
                if (i == 0)
                {
                    if (DrawSceneSettingsControls())
                        m_renderer.ResetTAAHistory();
                    ImGui::SeparatorText("Shadow Quality");
                    static int resolution = static_cast<int>(gs.shadow_map_size);
                    static int cascades = static_cast<int>(gs.num_cascades);
                    const int sizes[] = {512, 1024, 2048, 4096};
                    if (ImGui::BeginCombo("Map Size", std::to_string(resolution).c_str()))
                    {
                        for (int size : sizes)
                            if (ImGui::Selectable(std::to_string(size).c_str(), resolution == size))
                                resolution = size;
                        ImGui::EndCombo();
                    }
                    ImGui::SliderInt("Cascades", &cascades, 1, 4, "%d", ImGuiSliderFlags_AlwaysClamp);
                    if (ImGui::Button("Apply Shadow Quality"))
                        m_pendingShadowQuality = {static_cast<uint32_t>(resolution), static_cast<uint32_t>(cascades)};
                    ImGui::SameLine();
                    ImGui::TextDisabled("Current: %u px, %u cascades", gs.shadow_map_size, gs.num_cascades);
                    ImGui::DragFloat("Shadow LOD Bias", &gs.shadow_lod_bias, 0.01f, 0.1f, 10.f);
                    ImGui::SeparatorText("Render Target Precision");
                    auto formatControl = [](const char *label, std::string &value, const char *const *formats, int count)
                    {
                        if (ImGui::BeginCombo(label, value.empty() ? "Default" : value.c_str()))
                        {
                            if (ImGui::Selectable("Default", value.empty()))
                                value.clear();
                            for (int j = 0; j < count; ++j)
                                if (ImGui::Selectable(formats[j], value == formats[j]))
                                    value = formats[j];
                            ImGui::EndCombo();
                        }
                    };
                    const char *normalFormats[] = {"rgba16f", "rgba32f"};
                    const char *velocityFormats[] = {"rg16f", "rgba16f", "rgba32f"};
                    formatControl("Normals", gs.normal_format, normalFormats, IM_ARRAYSIZE(normalFormats));
                    formatControl("Velocity", gs.velocity_format, velocityFormats, IM_ARRAYSIZE(velocityFormats));
                }
                else if (i == 1)
                {
                    if (DrawPostProcessControls(gs))
                        m_renderer.ResetTAAHistory();
                    ImGui::Separator();
                    ImGui::TextWrapped("Motion blur uses a dense, centered filter. The sample budget also limits the blur span in pixels (up to 32).");
                    if (ImGui::Button("Reset Post Processing"))
                    {
                        static_cast<PostProcessProfile &>(gs) = PostProcessProfile{};
                        m_renderer.ResetTAAHistory();
                    }
                }
                else
                {
                    ImGui::SeparatorText("Environment");
                    ImGui::TextWrapped("Skybox: %s", gs.skybox_path.empty() ? "Solid background" : gs.skybox_path.c_str());
                    if (ImGui::Button("Choose Skybox..."))
                    {
                        std::string path;
                        if (PickFile("Choose skybox", "Environment texture\0*.hdr;*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All files\0*.*\0", path))
                        {
                            gs.skybox_path = path;
                            m_scene.EnsureSkyboxNodeFromSettings(false);
                            m_renderer.ReloadSkyFromSettings();
                            m_renderer.ResetTAAHistory();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Solid Background"))
                    {
                        gs.skybox_path.clear();
                        m_scene.EnsureSkyboxNodeFromSettings(false);
                        m_renderer.ReloadSkyFromSettings();
                        m_renderer.ResetTAAHistory();
                    }
                    ImGui::Checkbox("Grid", &gs.draw_grid);
                    bool ground = m_ground;
                    if (ImGui::Checkbox("Ground", &ground))
                        SetGroundVisible(ground);
                    ImGui::SeparatorText("Camera");
                    Camera *camera = m_scene.GetCameras().empty() ? nullptr : m_scene.GetActiveCamera();
                    if (camera)
                    {
                        bool ortho = camera->IsOrthographic();
                        if (ImGui::Checkbox("Orthographic", &ortho))
                            m_timeline->SetOrthographic(ortho);
                        bool changed = false;
                        if (ortho)
                        {
                            OrbitView view;
                            if (m_timeline->GetOrbitView(view) && ImGui::DragFloat("Orthographic Size", &view.orthographicSize, 0.05f, 0.01f, 10000.f))
                            {
                                m_timeline->SetOrbitView(view);
                                changed = true;
                            }
                        }
                        else
                        {
                            float fov = glm::degrees(camera->Fovy());
                            if (ImGui::SliderFloat("Vertical FOV", &fov, 15.f, 120.f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
                            {
                                camera->SetFovx(camera->FovyToFovx(glm::radians(fov)));
                                changed = true;
                            }
                        }
                        float nearPlane = camera->GetNearPlane();
                        if (ImGui::DragFloat("Near Clip", &nearPlane, 0.001f, 0.001f, 1.f, "%.3f"))
                        {
                            camera->SetNearPlane(std::clamp(nearPlane, 0.001f, 1.f));
                            changed = true;
                        }
                        if (ortho)
                        {
                            float farPlane = camera->GetFarPlane();
                            if (ImGui::DragFloat("Far Clip", &farPlane, 1.f, nearPlane + 0.01f, 100000.f))
                            {
                                camera->SetFarPlane(std::max(farPlane, nearPlane + 0.01f));
                                changed = true;
                            }
                        }
                        if (changed)
                            m_renderer.ResetTAAHistory();
                    }
                }
                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            m_settingsTab = -1;
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
} // namespace pe
