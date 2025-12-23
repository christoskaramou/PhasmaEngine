#include "Scripts.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"

namespace pe
{
    void Scripts::Update()
    {
        if (!m_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        ui::SetInitialWindowSizeFraction(1.0f / 6.0f, 1.0f / 3.0f);
        ImGui::Begin("Scripts", &m_open);
        ImGui::TextDisabled("Double click a script to open it externally");

        static ImGuiTextFilter scriptFilter;
        scriptFilter.Draw("Filter##scripts", ImGui::GetFontSize() * 14.0f);
        ImGui::Separator();

        const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::BeginChild("##scripts_child", ImVec2(0, 0), true))
        {
            if (gSettings.file_list.empty())
            {
                ImGui::TextDisabled("No .cs scripts found under Assets/Scripts");
            }

            for (const auto &entry : gSettings.file_list)
            {
                if (!scriptFilter.PassFilter(entry.c_str()))
                    continue;

                const bool selected = (GUIState::s_assetPreview.type == AssetPreviewType::Script && GUIState::s_assetPreview.label == entry);
                if (ImGui::Selectable(entry.c_str(), selected, selectFlags))
                {
                    std::string fullPath = Path::Executable + "Assets/Scripts/" + entry;
                    GUIState::UpdateAssetPreview(AssetPreviewType::Script, entry, fullPath);
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        GUIState::OpenExternalPath(fullPath);
                    }
                }
            }

        }
        ImGui::EndChild();

        ImGui::End();
    }
} // namespace pe
