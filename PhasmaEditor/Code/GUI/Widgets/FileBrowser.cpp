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
        if (m_txtIcon)
            Image::Destroy(m_txtIcon);
        if (m_shaderIcon)
            Image::Destroy(m_shaderIcon);
        if (m_modelIcon)
            Image::Destroy(m_modelIcon);
        if (m_scriptIcon)
            Image::Destroy(m_scriptIcon);
        if (m_imageIcon)
            Image::Destroy(m_imageIcon);
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
        m_txtIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/txt_icon.png");
        m_shaderIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/shader_icon.png");
        m_modelIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/model_icon.png");
        m_scriptIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/script_icon.png");
        m_imageIcon = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Icons/image_icon.png");

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        if (m_folderIcon && m_folderIcon->GetSampler() && m_folderIcon->GetSRV())
            m_folderIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_folderIcon->GetSampler()->ApiHandle(), m_folderIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_fileIcon && m_fileIcon->GetSampler() && m_fileIcon->GetSRV())
            m_fileIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_fileIcon->GetSampler()->ApiHandle(), m_fileIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            
        if (m_txtIcon && m_txtIcon->GetSampler() && m_txtIcon->GetSRV())
            m_txtIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_txtIcon->GetSampler()->ApiHandle(), m_txtIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_shaderIcon && m_shaderIcon->GetSampler() && m_shaderIcon->GetSRV())
            m_shaderIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_shaderIcon->GetSampler()->ApiHandle(), m_shaderIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_modelIcon && m_modelIcon->GetSampler() && m_modelIcon->GetSRV())
            m_modelIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_modelIcon->GetSampler()->ApiHandle(), m_modelIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_scriptIcon && m_scriptIcon->GetSampler() && m_scriptIcon->GetSRV())
            m_scriptIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_scriptIcon->GetSampler()->ApiHandle(), m_scriptIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_imageIcon && m_imageIcon->GetSampler() && m_imageIcon->GetSRV())
            m_imageIconDS = (void *)ImGui_ImplVulkan_AddTexture(m_imageIcon->GetSampler()->ApiHandle(), m_imageIcon->GetSRV(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
                if (parent.find("PhasmaEngine") != std::string::npos || parent.find("PhasmaEditor") != std::string::npos)
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
                                else if (ext == ".peh" || ext == ".pecpp")
                                    type = AssetPreviewType::Script;
                                else if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".glsl" || ext == ".hlsl")
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
                                else if (!isDir)
                                {
                                    void *iconID = m_fileIconDS;
                                    std::string ext = entry.path().extension().string();
                                    if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml")
                                        iconID = m_txtIconDS ? m_txtIconDS : m_fileIconDS;
                                    else if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".glsl" || ext == ".hlsl")
                                        iconID = m_shaderIconDS ? m_shaderIconDS : m_fileIconDS;
                                    else if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae" || ext == ".stl" || ext == ".ply" || ext == ".3ds" || ext == ".blend")
                                        iconID = m_modelIconDS ? m_modelIconDS : m_fileIconDS;
                                    else if (ext == ".peh" || ext == ".pecpp")
                                        iconID = m_scriptIconDS ? m_scriptIconDS : m_fileIconDS;
                                    else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".psd" || ext == ".gif" || ext == ".hdr" || ext == ".pic")
                                        iconID = m_imageIconDS ? m_imageIconDS : m_fileIconDS;

                                    if (iconID)
                                    {
                                        ImGui::Image((ImTextureID)iconID, ImVec2(20, 20));
                                        ImGui::SameLine();
                                    }
                                }

                                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                                {
                                    HandleItemConnect(entry);
                                }
                            }
                        }
                        else // Grid
                        {
                            float panelWidth = ImGui::GetContentRegionAvail().x;
                            float iconSize = m_gridIconSize;
                            float pad = ImGui::GetStyle().FramePadding.x;
                            float buttonSize = iconSize + pad * 2.0f;
                            float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
                            
                            // Calculate visible right edge for wrapping
                            float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

                            for (size_t i = 0; i < entries.size(); ++i)
                            {
                                const auto &entry = entries[i];
                                std::string filename = entry.path().filename().string();
                                bool isDir = entry.is_directory();
                                bool isSelected = (m_selectedEntry == entry.path());

                                ImGui::PushID(filename.c_str());
                                ImGui::BeginGroup(); // Group Icon + Text

                                // Selection Style
                                if (isSelected)
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                                else
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

                                void *iconID = nullptr;
                                if (isDir) {
                                    iconID = m_folderIconDS;
                                } else {
                                    iconID = m_fileIconDS;
                                    std::string ext = entry.path().extension().string();
                                    if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml")
                                        iconID = m_txtIconDS ? m_txtIconDS : m_fileIconDS;
                                    else if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".glsl" || ext == ".hlsl")
                                        iconID = m_shaderIconDS ? m_shaderIconDS : m_fileIconDS;
                                    else if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae" || ext == ".stl" || ext == ".ply" || ext == ".3ds" || ext == ".blend")
                                        iconID = m_modelIconDS ? m_modelIconDS : m_fileIconDS;
                                    else if (ext == ".peh" || ext == ".pecpp")
                                        iconID = m_scriptIconDS ? m_scriptIconDS : m_fileIconDS;
                                    else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".psd" || ext == ".gif" || ext == ".hdr" || ext == ".pic")
                                        iconID = m_imageIconDS ? m_imageIconDS : m_fileIconDS;
                                }
                                
                                bool clicked = false;
                                if (iconID)
                                {
                                    if (ImGui::ImageButton("##icon", (ImTextureID)iconID, ImVec2(iconSize, iconSize)))
                                        clicked = true;
                                }
                                else
                                {
                                    if (ImGui::Button("##icon", ImVec2(buttonSize, buttonSize)))
                                        clicked = true;
                                }

                                ImGui::PopStyleColor();

                                // Text
                                float groupX = ImGui::GetCursorPosX();
                                float textW = ImGui::CalcTextSize(filename.c_str()).x;
                                if (textW < buttonSize)
                                    ImGui::SetCursorPosX(groupX + (buttonSize - textW) * 0.5f);

                                ImGui::PushTextWrapPos(groupX + buttonSize); 
                                ImGui::TextWrapped("%s", filename.c_str());
                                ImGui::PopTextWrapPos();

                                ImGui::EndGroup();

                                // Interaction
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                                {
                                    HandleItemConnect(entry);
                                }
                                else if (clicked || ImGui::IsItemClicked()) // Check both Button and Group click
                                {
                                    m_selectedEntry = entry.path();
                                }

                                ImGui::PopID();

                                // Flow Control
                                float last_x2 = ImGui::GetItemRectMax().x;
                                float next_x2 = last_x2 + itemSpacingX + buttonSize;

                                // If next item fits, same line
                                if (i + 1 < entries.size() && next_x2 < windowVisibleX2)
                                    ImGui::SameLine();
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
