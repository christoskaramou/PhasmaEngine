#include "FileBrowser.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
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
                    outIcon->GetSRV()->ApiHandle(),
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

        for (auto &pair : m_fileCache)
            Image::Destroy(pair.second);
        m_fileCache.clear();
        m_fileDescriptors.clear();
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

    void FileBrowser::SetCurrentPath(const std::filesystem::path &path)
    {
        m_currentPath = path;
        RefreshCache();
    }

    void *FileBrowser::GetIconForFile(const std::filesystem::path &path)
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
        {
            auto u8str = path.u8string();
            std::string pathStr(reinterpret_cast<const char *>(u8str.c_str()));

            // Check cache
            auto it = m_fileDescriptors.find(pathStr);
            if (it != m_fileDescriptors.end())
                return it->second;

            // Check if already pending
            if (m_pendingFiles.find(pathStr) == m_pendingFiles.end())
            {
                m_pendingFiles.insert(pathStr);

                ThreadPool::GUI.Enqueue([this, pathStr]()
                                        {
                    Queue *queue = RHII.GetMainQueue();
                    // Acquire CB from thread-local pool
                    CommandBuffer *cmd = queue->AcquireCommandBuffer();
                    
                    cmd->Begin();
                    Image* img = Image::LoadRGBA8(cmd, pathStr);
                    cmd->End();

                    queue->Submit(1, &cmd, nullptr, nullptr);
                    cmd->Wait();
                    cmd->Return();

                    if (img)
                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_loadedQueue.push_back({pathStr, img});
                    } });
            }

            return m_imageIconDS ? m_imageIconDS : m_fileIconDS;
        }

        return m_fileIconDS;
    }

    void FileBrowser::ProcessLoadedImages()
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_loadedQueue.empty())
            return;

        for (auto &pair : m_loadedQueue)
        {
            if (pair.second)
            {
                void *ds = nullptr;
                if (pair.second->GetSampler() && pair.second->GetSRV())
                {
                    ds = (void *)ImGui_ImplVulkan_AddTexture(
                        pair.second->GetSampler()->ApiHandle(),
                        pair.second->GetSRV()->ApiHandle(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }

                m_fileCache[pair.first] = pair.second;
                m_fileDescriptors[pair.first] = ds;
            }
            else
            {
                Image::Destroy(pair.second);
            }
            m_pendingFiles.erase(pair.first);
        }
        m_loadedQueue.clear();
    }

    void FileBrowser::Update()
    {
        ProcessLoadedImages();

        if (m_open && !m_wasOpen)
        {
            RefreshCache();
        }
        m_wasOpen = m_open;

        if (!m_open)
            return;

        ui::SetInitialWindowSizeFraction(0.2f, 0.4f);
        if (ImGui::Begin("File Browser", &m_open))
        {
            // Top Bar: Navigation
            if (ImGui::Button("..") && m_currentPath.has_parent_path())
            {
                auto parentU8 = m_currentPath.parent_path().u8string();
                std::string parent(reinterpret_cast<const char *>(parentU8.c_str()));
                if (parent.find("PhasmaEngine") != std::string::npos || parent.find("PhasmaEditor") != std::string::npos)
                    m_currentPath = m_currentPath.parent_path();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_SYNC_ALT) || ImGui::Button("Refresh"))
                RefreshCache();

            ImGui::SameLine();
            auto currentPathU8 = m_currentPath.u8string();
            ImGui::Text("%s", reinterpret_cast<const char *>(currentPathU8.c_str()));

            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            if (ImGui::Button("List"))
                m_viewMode = ViewMode::List;
            ImGui::SameLine();
            if (ImGui::Button("Grid"))
                m_viewMode = ViewMode::Grid;

            ImGui::Separator();

            auto onNormalActionCaptured = [this](const std::filesystem::path &path)
            {
                if (IsDirectory(path))
                {
                    m_currentPath = path;
                }
                else
                {
                    AssetPreviewType type = AssetPreviewType::None;
                    if (IsModelFile(path))
                        type = AssetPreviewType::Model;
                    else if (IsScriptFile(path))
                        type = AssetPreviewType::Script;
                    else if (IsShaderFile(path))
                        type = AssetPreviewType::Shader;

                    if (type != AssetPreviewType::None)
                    {
                        auto filenameU8 = path.filename().u8string();
                        auto pathU8 = path.u8string();
                        std::string filenameStr(reinterpret_cast<const char *>(filenameU8.c_str()));
                        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                        GUIState::UpdateAssetPreview(type, filenameStr, pathStr);
                    }

                    if (type == AssetPreviewType::Model && !GUIState::s_modelLoading)
                    {
                        auto loadAsync = [path]()
                        {
                            GUIState::s_modelLoading = true;
                            Model::Load(path);
                            GUIState::s_modelLoading = false;
                        };
                        ThreadPool::GUI.Enqueue(loadAsync);
                    }
                    else if (type == AssetPreviewType::Script || type == AssetPreviewType::Shader)
                    {
                        auto pathU8 = path.u8string();
                        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                        GUIState::OpenExternalPath(pathStr);
                    }
                }
            };

            DrawDirectoryContent(m_currentPath, onNormalActionCaptured);
        }
        ImGui::End();
    }

    void FileBrowser::RefreshCache()
    {
        m_cache.clear();
        m_cachePath = m_currentPath;

        if (std::filesystem::exists(m_cachePath) && std::filesystem::is_directory(m_cachePath))
        {
            try
            {
                m_cache.reserve(100); // Reserve some potential space
                for (const auto &entry : std::filesystem::directory_iterator(m_cachePath))
                {
                    FileEntry e;
                    e.path = entry.path();

                    // Cache UTF-8 filename
                    auto filenameU8 = e.path.filename().u8string();
                    e.filename = std::string(reinterpret_cast<const char *>(filenameU8.c_str()));

                    e.isDirectory = IsDirectory(e.path);
                    e.iconID = GetIconForFile(e.path);

                    m_cache.push_back(e);
                }

                // Sort: Directories first, then alphabetical
                std::sort(m_cache.begin(), m_cache.end(), [](const FileEntry &a, const FileEntry &b)
                          {
                              if (a.isDirectory != b.isDirectory)
                                  return a.isDirectory > b.isDirectory;
                              return a.filename < b.filename; });
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                PE_ERROR("Error accessing directory: %s", e.what());
            }
        }
    }

    void FileBrowser::DrawDirectoryContent(const std::filesystem::path &path,
                                           std::function<void(const std::filesystem::path &)> onDoubleClick,
                                           std::function<bool(const std::filesystem::path &)> filter,
                                           float footerHeight)
    {
        // Re-cache if path changed
        if (m_cachePath != path)
        {
            m_currentPath = path;
            RefreshCache();
        }

        if (footerHeight == 0.0f)
            footerHeight = ImGui::GetStyle().ItemSpacing.y;

        if (ImGui::BeginChild("##file_browser_list", ImVec2(0, -footerHeight), true))
        {
            bool isList = (m_viewMode == ViewMode::List);
            int count = static_cast<int>(m_cache.size());

            if (isList)
            {
                ImGuiListClipper clipper;
                clipper.Begin(count);

                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                    {
                        const auto &entry = m_cache[i];

                        // Filter
                        if (filter && !filter(entry.path))
                            continue;

                        bool isSelected = (m_selectedEntry == entry.path);

                        if (entry.iconID)
                        {
                            ImGui::Image((ImTextureID)entry.iconID, ImVec2(20, 20));
                            ImGui::SameLine();
                        }

                        if (ImGui::Selectable(entry.filename.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            m_selectedEntry = entry.path;
                            if (ImGui::IsMouseDoubleClicked(0))
                                onDoubleClick(entry.path);
                        }

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            std::string pathStr = entry.path.string();
                            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr.c_str(), pathStr.length() + 1);
                            ImGui::Text("%s", entry.filename.c_str());
                            ImGui::EndDragDropSource();
                        }
                    }
                }
            }
            else // Grid
            {
                float buttonSize = m_gridIconSize;
                ImGuiStyle &style = ImGui::GetStyle();
                float itemSpacingX = style.ItemSpacing.x;
                float itemSpacingY = style.ItemSpacing.y;

                // ImageButton arguments are "image size", so widget size is image_size + padding * 2
                float buttonFullWidth = buttonSize + style.FramePadding.x * 2.0f;
                float buttonFullHeight = buttonSize + style.FramePadding.y * 2.0f;

                // Reserve space for ~2 lines of text (approx) to keep grid stable
                float textLineHeight = ImGui::GetTextLineHeight();
                float maxTextHeight = textLineHeight * 2.0f;

                float cellHeight = buttonFullHeight + style.ItemSpacing.y + maxTextHeight;
                float cellWidth = buttonFullWidth;

                // Calculate columns
                int columns = static_cast<int>((ImGui::GetContentRegionAvail().x + itemSpacingX) / (cellWidth + itemSpacingX));
                if (columns < 1)
                    columns = 1;

                int rows = (count + columns - 1) / columns;

                ImGuiListClipper clipper;
                clipper.Begin(rows, cellHeight + itemSpacingY);

                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                    {
                        for (int col = 0; col < columns; col++)
                        {
                            int index = row * columns + col;
                            if (index >= count)
                                break;

                            const auto &entry = m_cache[index];

                            // Note: Filter currently breaks grid alignment if used here without pre-filtering the cache.
                            if (filter && !filter(entry.path))
                                continue;

                            ImGui::PushID(index);

                            if (col > 0)
                                ImGui::SameLine();

                            ImGui::BeginGroup();
                            float groupStartY = ImGui::GetCursorPosY();

                            bool clicked = false;
                            if (entry.iconID)
                            {
                                if (ImGui::ImageButton("##icon", (ImTextureID)entry.iconID, ImVec2(buttonSize, buttonSize)))
                                    clicked = true;
                            }
                            else
                            {
                                // Match ImageButton total size
                                if (ImGui::Button("##file", ImVec2(buttonFullWidth, buttonFullHeight)))
                                    clicked = true;
                            }

                            // Text
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cellWidth);
                            ImGui::TextWrapped("%s", entry.filename.c_str());
                            ImGui::PopTextWrapPos();

                            // Force consistent height for clipper stability
                            float targetGroupHeight = cellHeight - style.ItemSpacing.y;
                            float currentY = ImGui::GetCursorPosY();
                            if (currentY - groupStartY < targetGroupHeight)
                            {
                                ImGui::Dummy(ImVec2(0.0f, targetGroupHeight - (currentY - groupStartY)));
                            }

                            ImGui::EndGroup();

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                            {
                                m_selectedEntry = entry.path;
                                onDoubleClick(entry.path);
                            }
                            else if (clicked || ImGui::IsItemClicked())
                            {
                                m_selectedEntry = entry.path;
                            }

                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                std::string pathStr = entry.path.string();
                                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr.c_str(), pathStr.length() + 1);
                                ImGui::Text("%s", entry.filename.c_str());
                                ImGui::EndDragDropSource();
                            }

                            ImGui::PopID();
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
    }
} // namespace pe
