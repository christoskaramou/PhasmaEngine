#include "Loading.h"
#include "GUI/GUIState.h"
#include "imgui/imgui.h"
#include <SDL2/SDL.h>

namespace pe
{
    void Loading::Update()
    {
        static const float radius = 25.f;
        static const int flags = ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoDocking;

        auto &gSettings = Settings::Get<GlobalSettings>();
        if (GUIState::s_modelLoading)
        {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(400.f, 70.f));
            ImGui::SetNextWindowFocus();

            static bool open = true;
            const uint32_t total = gSettings.loading.total.load(std::memory_order_relaxed);
            const uint32_t current = gSettings.loading.current.load(std::memory_order_relaxed);
            const float progress = total > 0 ? (current / static_cast<float>(total)) : 0.0f;
            const std::string loadingName = gSettings.loading.GetName();
            ImGui::Begin(loadingName.c_str(), &open, flags);
            ImGui::ProgressBar(progress, ImVec2(-0.001f, 25.f));
            ImGui::End();
        }
    }
} // namespace pe
