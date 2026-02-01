#include "Models.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Scene/Model.h"

namespace pe
{
    void Models::Update()
    {
        if (!m_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        ui::SetInitialWindowSizeFraction(1.0f / 6.0f, 1.0f / 3.0f);
        ImGui::Begin("Models", &m_open);
        ImGui::TextDisabled("Double click an asset to load it into the scene");

        static ImGuiTextFilter modelFilter;
        modelFilter.Draw("Filter##models", ImGui::GetFontSize() * 14.0f);
        ImGui::Separator();

        const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::BeginChild("##models_child", ImVec2(0, 0), true))
        {
            if (gSettings.model_list.empty())
            {
                ImGui::TextDisabled("No model assets were found under Assets/Objects");
            }

            for (const auto &entry : gSettings.model_list)
            {
                if (!modelFilter.PassFilter(entry.c_str()))
                    continue;

                const bool selected = (GUIState::s_assetPreview.type == AssetPreviewType::Model && GUIState::s_assetPreview.label == entry);
                if (ImGui::Selectable(entry.c_str(), selected, selectFlags))
                {
                    std::string fullPath = Path::Assets + "Objects/" + entry;
                    GUIState::UpdateAssetPreview(AssetPreviewType::Model, entry, fullPath);
                    if (ImGui::IsMouseDoubleClicked(0) && !GUIState::s_modelLoading)
                    {
                        auto loadTask = [fullPath]()
                        {
                            GUIState::s_modelLoading = true;
                            // Use u8path to properly interpret UTF-8 string as path on Windows
                            std::filesystem::path filePath(reinterpret_cast<const char8_t *>(fullPath.c_str()));
                            Model::Load(filePath);
                            GUIState::s_modelLoading = false;
                        };
                        ThreadPool::GUI.Enqueue(loadTask);
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }
} // namespace pe
