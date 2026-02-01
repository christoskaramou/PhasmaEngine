#include "AssetViewer.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Scene/Model.h"

namespace pe
{
    static const char *AssetPreviewTypeToText(AssetPreviewType type)
    {
        switch (type)
        {
        case AssetPreviewType::Model:
            return "Model";
        case AssetPreviewType::Script:
            return "Script";
        case AssetPreviewType::Shader:
            return "Shader";
        default:
            return "None";
        }
    }

    void AssetViewer::Update()
    {
        if (!m_open)
            return;

        ui::SetInitialWindowSizeFraction(0.35f, 1.0f / 4.0f);
        ImGui::Begin("Asset Viewer", &m_open);

        if (GUIState::s_assetPreview.type == AssetPreviewType::None || GUIState::s_assetPreview.label.empty())
        {
            ImGui::TextDisabled("Select a model, script, or shader to inspect it here.");
            ImGui::End();
            return;
        }

        ImGui::Text("Type: %s", AssetPreviewTypeToText(GUIState::s_assetPreview.type));
        ImGui::Text("Asset: %s", GUIState::s_assetPreview.label.c_str());
        if (!GUIState::s_assetPreview.fullPath.empty())
        {
            ImGui::TextWrapped("Path: %s", GUIState::s_assetPreview.fullPath.c_str());
            if (ImGui::Button("Open File"))
                GUIState::OpenExternalPath(GUIState::s_assetPreview.fullPath);

            // Use u8path to properly interpret UTF-8 string as path on Windows
            std::filesystem::path p(reinterpret_cast<const char8_t *>(GUIState::s_assetPreview.fullPath.c_str()));
            auto folderU8 = p.parent_path().u8string();
            const std::string folder(reinterpret_cast<const char *>(folderU8.c_str()));
            if (!folder.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Reveal In Explorer"))
                    GUIState::OpenExternalPath(folder);
            }

            if (GUIState::s_assetPreview.type == AssetPreviewType::Model)
            {
                ImGui::SameLine();
                const bool canLoad = !GUIState::s_modelLoading;
                if (!canLoad)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Load Model"))
                {
                    // Use u8path to properly interpret UTF-8 string as path on Windows
                    std::filesystem::path path(reinterpret_cast<const char8_t *>(GUIState::s_assetPreview.fullPath.c_str()));
                    auto loadAsync = [path]()
                    {
                        GUIState::s_modelLoading = true;
                        Model::Load(path);
                        GUIState::s_modelLoading = false;
                    };
                    ThreadPool::GUI.Enqueue(loadAsync);
                }

                if (!canLoad)
                    ImGui::EndDisabled();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Preview");
        ImGui::BeginChild("##asset_viewer_canvas", ImVec2(0, 0), true);
        ImGui::TextDisabled("Visual previews will live here.");
        ImGui::EndChild();

        ImGui::End();
    }
} // namespace pe
