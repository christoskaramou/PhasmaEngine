#include "FileBrowser.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Scene/Model.h"

namespace pe
{
    void FileBrowser::Init(GUI *gui)
    {
        Widget::Init(gui);
        m_currentPath = std::filesystem::path(Path::Executable + "Assets/");
    }

    void FileBrowser::Update()
    {
        if (!m_open)
            return;

        ui::SetInitialWindowSizeFraction(0.2f, 0.4f);
        if (ImGui::Begin("File Browser", &m_open))
        {
            // Top Bar: Navigation
            if (ImGui::Button("..") && m_currentPath.has_parent_path())
            {
                // For safety/relevance, keep it within Project root folders
                std::string parent = m_currentPath.parent_path().string();
                if (parent.find("PhrasmaEngine") != std::string::npos || parent.find("PhasmaEditor") != std::string::npos)
                    m_currentPath = m_currentPath.parent_path();
            }
            ImGui::SameLine();
            ImGui::Text("%s", m_currentPath.string().c_str());
            ImGui::Separator();

            // File List
            float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
            if (ImGui::BeginChild("##file_browser_list", ImVec2(0, -footerHeight), true))
            {
                try
                {
                    if (std::filesystem::exists(m_currentPath) && std::filesystem::is_directory(m_currentPath))
                    {
                        // Folders first
                        for (const auto &entry : std::filesystem::directory_iterator(m_currentPath))
                        {
                            if (entry.is_directory())
                            {
                                std::string label = "[Dir] " + entry.path().filename().string();
                                bool isSelected = (m_selectedEntry == entry.path());
                                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                                {
                                    m_selectedEntry = entry.path();
                                    if (ImGui::IsMouseDoubleClicked(0))
                                    {
                                        m_currentPath = entry.path();
                                    }
                                }
                            }
                        }

                        // Then Files
                        for (const auto &entry : std::filesystem::directory_iterator(m_currentPath))
                        {
                            if (!entry.is_directory())
                            {
                                std::string filename = entry.path().filename().string();
                                std::string label = "      " + filename;
                                bool isSelected = (m_selectedEntry == entry.path());

                                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                                {
                                    m_selectedEntry = entry.path();

                                    // Identify type for preview
                                    AssetPreviewType type = AssetPreviewType::None;
                                    std::string ext = entry.path().extension().string();
                                    if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae" || ext == ".stl" || ext == ".ply" || ext == ".3ds" || ext == ".blend")
                                        type = AssetPreviewType::Model;
                                    else if (ext == ".cpp" && entry.path().string().find("Scripts") != std::string::npos)
                                        type = AssetPreviewType::Script;
                                    else if ((ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".glsl" || ext == ".hlsl") && entry.path().string().find("Shaders") != std::string::npos)
                                        type = AssetPreviewType::Shader;

                                    if (type != AssetPreviewType::None)
                                    {
                                        GUIState::UpdateAssetPreview(type, filename, entry.path().string());
                                    }

                                    if (ImGui::IsMouseDoubleClicked(0))
                                    {
                                        if (type == AssetPreviewType::Model && !GUIState::s_modelLoading)
                                        {
                                            auto path = entry.path();
                                            ThreadPool::GUI.Enqueue([path]()
                                                                    {
                                                GUIState::s_modelLoading = true;
                                                Model::Load(path);
                                                GUIState::s_modelLoading = false; });
                                        }
                                        else if (type == AssetPreviewType::Script || type == AssetPreviewType::Shader)
                                        {
                                            GUIState::OpenExternalPath(entry.path().string());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (const std::filesystem::filesystem_error &e)
                {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error accessing directory: %s", e.what());
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (ImGui::Button("Select"))
            {
                // Generic select action if needed
            }
        }
        ImGui::End();
    }
} // namespace pe
