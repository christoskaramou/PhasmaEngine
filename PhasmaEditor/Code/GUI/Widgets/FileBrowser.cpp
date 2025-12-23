#include "FileBrowser.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Scene/Model.h"

#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "imgui/imgui_impl_vulkan.h"

namespace pe
{
    FileBrowser::~FileBrowser()
    {
        if (m_folderIcon)
            Image::Destroy(m_folderIcon);
        if (m_fileIcon)
            Image::Destroy(m_fileIcon);
    }

    void FileBrowser::Init(GUI *gui)
    {
        Widget::Init(gui);
        m_currentPath = std::filesystem::path(Path::Executable + "Assets/");

        // Load Icons
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        m_folderIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/folder_icon.png");
        m_fileIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/file_icon.png");

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        if (m_folderIcon && m_folderIcon->GetSampler() && m_folderIcon->GetSRV())
            m_folderIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_folderIcon->GetSampler()->ApiHandle(), m_folderIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_fileIcon && m_fileIcon->GetSampler() && m_fileIcon->GetSRV())
            m_fileIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_fileIcon->GetSampler()->ApiHandle(), m_fileIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            if (ImGui::Button("List"))
                m_viewMode = ViewMode::List;
            ImGui::SameLine();
            if (ImGui::Button("Grid"))
                m_viewMode = ViewMode::Grid;

            ImGui::Separator();

            // File List
            float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
            if (ImGui::BeginChild("##file_browser_list", ImVec2(0, -footerHeight), true))
            {
                try
                {
                    if (std::filesystem::exists(m_currentPath) && std::filesystem::is_directory(m_currentPath))
                    {
                        auto HandleItemConnect = [&](const std::filesystem::directory_entry &entry)
                        {
                            m_selectedEntry = entry.path();

                            if (entry.is_directory())
                            {
                                if (ImGui::IsMouseDoubleClicked(0))
                                {
                                    m_currentPath = entry.path();
                                }
                            }
                            else
                            {
                                std::string filename = entry.path().filename().string();
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
                        };

                        std::vector<std::filesystem::directory_entry> entries;
                        for (const auto &entry : std::filesystem::directory_iterator(m_currentPath))
                            entries.push_back(entry);

                        // Sort: Directories first
                        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b)
                                  {
                            if (a.is_directory() != b.is_directory())
                                return a.is_directory() > b.is_directory();
                            return a.path().filename().string() < b.path().filename().string(); });

                        if (m_viewMode == ViewMode::List)
                        {
                            for (const auto &entry : entries)
                            {
                                std::string filename = entry.path().filename().string();
                                bool isDir = entry.is_directory();
                                std::string label = filename;
                                bool isSelected = (m_selectedEntry == entry.path());

                                if (isDir && m_folderIconDS)
                                {
                                    ImGui::Image((ImTextureID)m_folderIconDS, ImVec2(20, 20));
                                    ImGui::SameLine();
                                }
                                else if (!isDir && m_fileIconDS)
                                {
                                    ImGui::Image((ImTextureID)m_fileIconDS, ImVec2(20, 20));
                                    ImGui::SameLine();
                                }

                                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                                {
                                    HandleItemConnect(entry);
                                }
                            }
                        }
                        else // Grid
                        {
                            float padding = 10.0f;
                            float cellSize = m_gridIconSize + padding;
                            float panelWidth = ImGui::GetContentRegionAvail().x;
                            int columnCount = (int)(panelWidth / cellSize);
                            if (columnCount < 1)
                                columnCount = 1;

                            ImGui::Columns(columnCount, 0, false);

                            for (const auto &entry : entries)
                            {
                                std::string filename = entry.path().filename().string();
                                bool isDir = entry.is_directory();
                                bool isSelected = (m_selectedEntry == entry.path());

                                ImGui::PushID(filename.c_str());
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                                void *iconID = (isDir && m_folderIconDS) ? m_folderIconDS : ((!isDir && m_fileIconDS) ? m_fileIconDS : nullptr);

                                if (iconID)
                                {
                                    ImGui::ImageButton(filename.c_str(), (ImTextureID)iconID, ImVec2(m_gridIconSize, m_gridIconSize));
                                }
                                else
                                {
                                    ImGui::Button(filename.c_str(), ImVec2(m_gridIconSize, m_gridIconSize));
                                }
                                ImGui::PopStyleColor();

                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                                {
                                    HandleItemConnect(entry);
                                }
                                else if (ImGui::IsItemClicked())
                                {
                                    m_selectedEntry = entry.path();
                                    // Handle single click selection logic if needed,
                                    // although HandleItemConnect handles selection too.
                                    // But HandleItemConnect does extra work (previews).
                                    // Let's just update selection for now.
                                }

                                ImGui::TextWrapped("%s", filename.c_str());
                                ImGui::NextColumn();
                                ImGui::PopID();
                            }
                            ImGui::Columns(1);
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
