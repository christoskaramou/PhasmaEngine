#include "FileBrowser.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Scene/Model.h"
#include "imgui/imgui_impl_vulkan.h"


namespace pe
{
    namespace
    {
        void DestroyIcon(Image *&icon)
        {
            if (icon)
            {
                Image::Destroy(icon);
                icon = nullptr;
            }
        }

        void *LoadAndRegisterIcon(CommandBuffer *cmd, const std::string &path, Image *&outIcon)
        {
            outIcon = Image::LoadRGBA8(cmd, path);
            if (outIcon && outIcon->GetSampler() && outIcon->GetSRV())
            {
                return (void *)ImGui_ImplVulkan_AddTexture(
                    outIcon->GetSampler()->ApiHandle(),
                    outIcon->GetSRV(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            return nullptr;
        }
    } // namespace

    std::unordered_set<std::string> FileBrowser::s_textExtensions = {
        ".json", ".md", ".txt", ".xml"};
    std::unordered_set<std::string> FileBrowser::s_shaderExtensions = {
        ".comp", ".frag", ".glsl", ".hlsl", ".vert"};
    std::unordered_set<std::string> FileBrowser::s_scriptExtensions = {
        ".pecpp", ".peh"};
    std::unordered_set<std::string> FileBrowser::s_imageExtensions = {
        ".bmp", ".gif", ".hdr", ".jpeg", ".jpg", ".pic", ".png", ".psd", ".tga"};
    std::unordered_set<std::string> FileBrowser::s_modelExtensions = {
        ".3d", ".3ds", ".3mf", ".amf", ".ase", ".assbin", ".b3d", ".blend", ".bvh", ".cob",
        ".collada", ".csm", ".dae", ".dxf", ".fbx", ".glb", ".gltf", ".hmp", ".ifc", ".iqm",
        ".irr", ".irrmesh", ".lwo", ".lws", ".m3d", ".md2", ".md3", ".md5", ".mdc", ".mdl",
        ".mmd", ".ms3d", ".ndo", ".nff", ".obj", ".off", ".ogre", ".opengex", ".ply",
        ".q3bsp", ".q3d", ".raw", ".sib", ".smd", ".stl", ".terragen", ".x", ".x3d", ".xgl"};
    std::vector<const char *> FileBrowser::s_modelExtensionsVec = {
        ".3d", ".3ds", ".3mf", ".amf", ".ase", ".assbin", ".b3d", ".blend", ".bvh", ".cob",
        ".collada", ".csm", ".dae", ".dxf", ".fbx", ".glb", ".gltf", ".hmp", ".ifc", ".iqm",
        ".irr", ".irrmesh", ".lwo", ".lws", ".m3d", ".md2", ".md3", ".md5", ".mdc", ".mdl",
        ".mmd", ".ms3d", ".ndo", ".nff", ".obj", ".off", ".ogre", ".opengex", ".ply",
        ".q3bsp", ".q3d", ".raw", ".sib", ".smd", ".stl", ".terragen", ".x", ".x3d", ".xgl"};

    FileBrowser::~FileBrowser()
    {
        Image::Destroy(m_folderIcon);
        Image::Destroy(m_fileIcon);
        Image::Destroy(m_txtIcon);
        Image::Destroy(m_shaderIcon);
        Image::Destroy(m_modelIcon);
        Image::Destroy(m_scriptIcon);
        Image::Destroy(m_imageIcon);
    }

    void FileBrowser::Init(GUI *gui)
    {
        Widget::Init(gui);
        m_currentPath = std::filesystem::path(Path::Assets);

        // Load Icons
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        m_folderIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/folder_icon.png", m_folderIcon);
        m_fileIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/file_icon.png", m_fileIcon);
        m_txtIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/txt_icon.png", m_txtIcon);
        m_shaderIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/shader_icon.png", m_shaderIcon);
        m_modelIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/model_icon.png", m_modelIcon);
        m_scriptIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/script_icon.png", m_scriptIcon);
        m_imageIconDS = LoadAndRegisterIcon(cmd, Path::Assets + "Icons/image_icon.png", m_imageIcon);

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    void *FileBrowser::GetIconForFile(const std::filesystem::path &path) const
    {
        if (IsDirectory(path))
            return m_folderIconDS;
        if (IsTextFile(path))
            return m_txtIconDS ? m_txtIconDS : m_fileIconDS;
        if (IsShaderFile(path))
            return m_shaderIconDS ? m_shaderIconDS : m_fileIconDS;
        if (IsModelFile(path))
            return m_modelIconDS ? m_modelIconDS : m_fileIconDS;
        if (IsScriptFile(path))
            return m_scriptIconDS ? m_scriptIconDS : m_fileIconDS;
        if (IsImageFile(path))
            return m_imageIconDS ? m_imageIconDS : m_fileIconDS;

        return m_fileIconDS;
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

                            if (IsDirectory(m_selectedEntry))
                            {
                                if (ImGui::IsMouseDoubleClicked(0))
                                    m_currentPath = m_selectedEntry;
                            }
                            else
                            {
                                AssetPreviewType type = AssetPreviewType::None;
                                if (IsModelFile(m_selectedEntry))
                                    type = AssetPreviewType::Model;
                                else if (IsScriptFile(m_selectedEntry))
                                    type = AssetPreviewType::Script;
                                else if (IsShaderFile(m_selectedEntry))
                                    type = AssetPreviewType::Shader;

                                if (type != AssetPreviewType::None)
                                    GUIState::UpdateAssetPreview(type, m_selectedEntry.filename().string(), m_selectedEntry.string());

                                if (ImGui::IsMouseDoubleClicked(0))
                                {
                                    if (type == AssetPreviewType::Model && !GUIState::s_modelLoading)
                                    {
                                        ThreadPool::GUI.Enqueue([path = m_selectedEntry]()
                                                                {
                                                                    GUIState::s_modelLoading = true;
                                                                    Model::Load(path);
                                                                    GUIState::s_modelLoading = false; });
                                    }
                                    else if (type == AssetPreviewType::Script || type == AssetPreviewType::Shader)
                                    {
                                        GUIState::OpenExternalPath(m_selectedEntry.string());
                                    }
                                }
                            }
                        };

                        std::vector<std::filesystem::directory_entry> entries;
                        for (const auto &entry : std::filesystem::directory_iterator(m_currentPath))
                            entries.push_back(entry);

                        // Sort: Directories first
                        std::sort(entries.begin(), entries.end(),
                                  [](const auto &a, const auto &b)
                                  {
                                      const bool aDir = IsDirectory(a.path());
                                      const bool bDir = IsDirectory(b.path());

                                      if (aDir != bDir)
                                          return aDir; // true comes first => directories first

                                      return a.path().filename().string() < b.path().filename().string();
                                  });

                        if (m_viewMode == ViewMode::List)
                        {
                            for (const auto &entry : entries)
                            {
                                std::string filename = entry.path().filename().string();
                                std::string label = filename;
                                bool isSelected = (m_selectedEntry == entry.path());

                                void *iconID = GetIconForFile(entry.path());
                                if (iconID)
                                {
                                    ImGui::Image((ImTextureID)iconID, ImVec2(20, 20));
                                    ImGui::SameLine();
                                }

                                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                                    HandleItemConnect(entry);
                            }
                        }
                        else // Grid
                        {
                            float panelWidth = ImGui::GetContentRegionAvail().x;
                            float iconSize = m_gridIconSize;
                            float pad = ImGui::GetStyle().FramePadding.x;
                            float buttonSize = iconSize + pad * 2.0f;
                            float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
                            float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

                            for (size_t i = 0; i < entries.size(); ++i)
                            {
                                const auto &entry = entries[i];
                                std::string filename = entry.path().filename().string();
                                bool isSelected = (m_selectedEntry == entry.path());

                                ImGui::PushID(filename.c_str());
                                ImGui::BeginGroup(); // Group Icon + Text

                                // Selection Style
                                if (isSelected)
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                                else
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

                                void *iconID = GetIconForFile(entry.path());

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
