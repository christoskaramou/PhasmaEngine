#include "Loading.h"
#include "GUI/GUIState.h"
#include "Base/Settings.h"
#include "API/RHI.h"
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
                                 ImGuiWindowFlags_NoBackground;

        auto &gSettings = Settings::Get<GlobalSettings>();
        if (GUIState::s_modelLoading)
        {
            int x, y;
            const float topBarHeight = 16.f;
            SDL_GetWindowPosition(RHII.GetWindow(), &x, &y);
            ImVec2 size(-0.001f, 25.f);
            ImGui::SetNextWindowPos(ImVec2(x + RHII.GetWidthf() / 2 - 200.f, y + topBarHeight + RHII.GetHeightf() / 2 - size.y / 2));
            ImGui::SetNextWindowSize(ImVec2(400.f, 100.f));
            
            static bool open = true;
            ImGui::Begin(gSettings.loading_name.c_str(), &open, flags);
            
            const float progress = gSettings.loading_current / static_cast<float>(gSettings.loading_total);
            ImGui::ProgressBar(progress, size);
            ImGui::End();
        }
    }
} // namespace pe
