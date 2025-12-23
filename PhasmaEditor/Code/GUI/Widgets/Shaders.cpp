#include "Shaders.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"

namespace pe
{
    void Shaders::Update()
    {
        if (!m_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        ui::SetInitialWindowSizeFraction(1.0f / 5.5f, 1.0f / 3.5f);
        ImGui::Begin("Shaders Folder", &m_open);
        if (ImGui::Button("Compile Shaders"))
        {
            EventSystem::PushEvent(EventType::CompileShaders);
        }
        const ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        for (uint32_t i = 0; i < gSettings.shader_list.size(); i++)
        {
            const std::string &relative = gSettings.shader_list[i];
            std::string label = relative + "##shader_" + std::to_string(i);
            const bool selected = (GUIState::s_assetPreview.type == AssetPreviewType::Shader && GUIState::s_assetPreview.label == relative);
            if (ImGui::Selectable(label.c_str(), selected, flags))
            {
                std::string fullPath = Path::Executable + "Assets/Shaders/" + relative;
                GUIState::UpdateAssetPreview(AssetPreviewType::Shader, relative, fullPath);
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    GUIState::OpenExternalPath(fullPath);
                }
            }
        }
        ImGui::End();
    }
} // namespace pe
