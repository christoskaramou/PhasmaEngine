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

        // Removes a trailing directory separator so that parent_path() correctly navigates up.
        std::filesystem::path StripTrailingSep(const std::filesystem::path &p)
        {
            return p.filename().empty() ? p.parent_path() : p;
        }

        // Returns a UTF-8 string showing the last 2 path components, prefixed with "…/" if deeper.
        std::string ShortDisplayPath(const std::filesystem::path &p)
        {
            auto toStr = [](const std::filesystem::path &q)
            {
                auto u8 = q.u8string();
                return std::string(reinterpret_cast<const char *>(u8.c_str()));
            };

            std::filesystem::path name = p.filename();
            std::filesystem::path parent = p.parent_path();

            if (parent.empty() || parent == p.root_path())
                return toStr(p);

            std::filesystem::path grandparent = parent.parent_path();
            if (grandparent.empty() || grandparent == p.root_path())
                return toStr(p);

            return ".../" + toStr(parent.filename()) + "/" + toStr(name);
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

        struct CopyTask
        {
            std::filesystem::path src;
            std::filesystem::path dst;
        };

        // Expands sources (files and/or directories) into a flat list of file copy tasks.
        // Directory trees are expanded recursively; the dest mirrors the source structure.
        std::vector<CopyTask> BuildCopyTasks(const std::vector<std::filesystem::path> &sources,
                                             const std::filesystem::path &destDir)
        {
            std::vector<CopyTask> tasks;
            for (const auto &src : sources)
            {
                if (std::filesystem::is_regular_file(src))
                {
                    tasks.push_back({src, destDir / src.filename()});
                }
                else if (std::filesystem::is_directory(src))
                {
                    auto dirDest = destDir / src.filename();
                    for (const auto &entry : std::filesystem::recursive_directory_iterator(
                             src, std::filesystem::directory_options::skip_permission_denied))
                    {
                        if (!entry.is_regular_file())
                            continue;
                        auto rel = std::filesystem::relative(entry.path(), src);
                        tasks.push_back({entry.path(), dirDest / rel});
                    }
                }
            }
            return tasks;
        }

        uintmax_t TotalBytes(const std::vector<CopyTask> &tasks)
        {
            uintmax_t total = 0;
            for (const auto &t : tasks)
                total += std::filesystem::file_size(t.src);
            return total;
        }

        // Copies a file in 1MB chunks. Reports bytes written to onBytesWritten after each chunk.
        // Returns true on success.
        bool CopyFileChunked(const std::filesystem::path &src,
                             const std::filesystem::path &dst,
                             const std::function<void(uintmax_t)> &onBytesWritten)
        {
            constexpr size_t kChunkSize = 1 * 1024 * 1024;

            std::filesystem::create_directories(dst.parent_path());

            std::ifstream in(src, std::ios::binary);
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (!in.is_open() || !out.is_open())
                return false;

            auto fileSize = std::filesystem::file_size(src);
            if (fileSize == 0)
            {
                onBytesWritten(0);
                return true;
            }

            std::vector<char> buf(std::min(kChunkSize, static_cast<size_t>(fileSize)));
            while (in)
            {
                in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                auto n = static_cast<size_t>(in.gcount());
                if (n == 0)
                    break;
                out.write(buf.data(), static_cast<std::streamsize>(n));
                onBytesWritten(static_cast<uintmax_t>(n));
            }

            return !out.bad();
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
        m_currentPath = StripTrailingSep(std::filesystem::path(Path::Assets).lexically_normal());
        m_navHistory.push_back(m_currentPath);
        m_navHistoryIndex = 0;

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

        EventSystem::RegisterCallback(EventType::FileDrop, [this](const std::any &data)
                                      {
            const auto &paths = std::any_cast<const std::vector<std::string> &>(data);
            std::lock_guard lock(m_pendingDropsMutex);
            for (const auto &p : paths)
                m_pendingOsDrops.emplace_back(p); });
    }

    void FileBrowser::NavigateTo(const std::filesystem::path &path)
    {
        // Drop any forward history beyond the current position
        if (m_navHistoryIndex + 1 < static_cast<int>(m_navHistory.size()))
            m_navHistory.erase(m_navHistory.begin() + m_navHistoryIndex + 1, m_navHistory.end());

        m_navHistory.push_back(path);
        m_navHistoryIndex = static_cast<int>(m_navHistory.size()) - 1;
        m_currentPath = StripTrailingSep(path.lexically_normal());
        m_cachePath.clear();
    }

    void FileBrowser::SetCurrentPath(const std::filesystem::path &path)
    {
        NavigateTo(path);
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

    void FileBrowser::TryStartCopy(const std::vector<std::filesystem::path> &sources)
    {
        if (m_copyState->active)
            return;

        auto tasks = BuildCopyTasks(sources, m_currentPath);
        if (tasks.empty())
            return;

        // Collect conflicts
        m_conflictNames.clear();
        for (const auto &task : tasks)
            if (std::filesystem::exists(task.dst))
                m_conflictNames.push_back(task.dst.filename().string());

        if (m_conflictNames.empty())
        {
            CopyDroppedFiles(sources, false);
            m_pendingOpenCopyProgress = true;
        }
        else
        {
            m_pendingCopySources = sources;
            m_pendingOpenOverwritePrompt = true;
        }
    }

    void FileBrowser::CopyDroppedFiles(const std::vector<std::filesystem::path> &sources, bool skipExisting)
    {
        if (m_copyState->active)
            return;

        auto tasks = BuildCopyTasks(sources, m_currentPath);

        if (skipExisting)
        {
            tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                       [](const CopyTask &t)
                                       { return std::filesystem::exists(t.dst); }),
                        tasks.end());
        }

        if (tasks.empty())
            return;

        auto state = m_copyState;
        auto totalBytes = TotalBytes(tasks);
        int totalFiles = static_cast<int>(tasks.size());

        state->active = true;
        state->progress = 0.0f;
        state->needsRefresh = false;

        ThreadPool::GUI.Enqueue([state, tasks, totalBytes, totalFiles]()
                                {
            uintmax_t bytesCopied = 0;

            for (int i = 0; i < totalFiles; ++i)
            {
                const auto &task = tasks[i];

                {
                    std::lock_guard lock(state->labelMutex);
                    state->label = task.src.filename().string() +
                                   " (" + std::to_string(i + 1) + "/" + std::to_string(totalFiles) + ")";
                }

                auto onBytesWritten = [&](uintmax_t n)
                {
                    bytesCopied += n;
                    if (totalBytes > 0)
                        state->progress = static_cast<float>(bytesCopied) / static_cast<float>(totalBytes);
                };

                if (!CopyFileChunked(task.src, task.dst, onBytesWritten))
                    PE_WARN("[FileBrowser] Failed to copy '%s'", task.src.filename().string().c_str());
            }

            state->active = false;
            state->needsRefresh = true; });
    }

    void FileBrowser::DrawCopyProgress()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(360, 80), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Copying Files##filebrowser", nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoTitleBar))
        {
            std::string label;
            {
                std::lock_guard lock(m_copyState->labelMutex);
                label = m_copyState->label;
            }
            ImGui::TextUnformatted(label.c_str());
            ImGui::ProgressBar(m_copyState->progress, ImVec2(-1.0f, 0.0f));

            if (!m_copyState->active)
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void FileBrowser::DrawOverwritePrompt()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Overwrite?##filebrowser", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
            return;

        ImGui::Text("%d file(s) already exist in this location:", static_cast<int>(m_conflictNames.size()));
        ImGui::Spacing();

        constexpr int kMaxShown = 5;
        int shown = std::min(static_cast<int>(m_conflictNames.size()), kMaxShown);
        for (int i = 0; i < shown; ++i)
            ImGui::BulletText("%s", m_conflictNames[i].c_str());
        if (static_cast<int>(m_conflictNames.size()) > kMaxShown)
            ImGui::Text("  ...and %d more", static_cast<int>(m_conflictNames.size()) - kMaxShown);

        ImGui::Spacing();

        if (ImGui::Button("Overwrite"))
        {
            CopyDroppedFiles(m_pendingCopySources, false);
            m_pendingOpenCopyProgress = true;
            m_pendingCopySources.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip Existing"))
        {
            CopyDroppedFiles(m_pendingCopySources, true);
            m_pendingOpenCopyProgress = true;
            m_pendingCopySources.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_pendingCopySources.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void FileBrowser::OpenWithSystem(const std::filesystem::path &path)
    {
#if defined(PE_WIN32)
        ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOW);
#elif defined(PE_LINUX)
        std::string cmd = "xdg-open \"" + path.string() + "\" &";
        std::system(cmd.c_str());
#endif
    }

    void FileBrowser::OpenInFileManager(const std::filesystem::path &path)
    {
#if defined(PE_WIN32)
        if (std::filesystem::is_regular_file(path))
        {
            // Open Explorer with the file selected
            std::wstring params = L"/select,\"" + path.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOW);
        }
        else
        {
            ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOW);
        }
#elif defined(PE_LINUX)
        auto target = std::filesystem::is_directory(path) ? path : path.parent_path();
        std::string cmd = "xdg-open \"" + target.string() + "\" &";
        std::system(cmd.c_str());
#endif
    }

    void FileBrowser::DrawNavBar(bool showViewMode)
    {
        // Thumb mouse buttons for back / forward navigation
        if (ImGui::IsWindowHovered(ImGuiFocusedFlags_RootAndChildWindows))
        {
            if (ImGui::IsMouseClicked(3) && m_navHistoryIndex > 0)
            {
                --m_navHistoryIndex;
                m_currentPath = m_navHistory[m_navHistoryIndex];
                m_cachePath.clear();
            }
            if (ImGui::IsMouseClicked(4) && m_navHistoryIndex < static_cast<int>(m_navHistory.size()) - 1)
            {
                ++m_navHistoryIndex;
                m_currentPath = m_navHistory[m_navHistoryIndex];
                m_cachePath.clear();
            }
        }

        bool canBack = m_navHistoryIndex > 0;
        bool canForward = m_navHistoryIndex < static_cast<int>(m_navHistory.size()) - 1;
        bool canUp = m_currentPath.has_parent_path();

        if (!canBack)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_LEFT "##navback"))
        {
            --m_navHistoryIndex;
            m_currentPath = m_navHistory[m_navHistoryIndex];
            RefreshCache();
        }
        if (!canBack)
            ImGui::EndDisabled();

        ImGui::SameLine();

        if (!canForward)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_RIGHT "##navfwd"))
        {
            ++m_navHistoryIndex;
            m_currentPath = m_navHistory[m_navHistoryIndex];
            RefreshCache();
        }
        if (!canForward)
            ImGui::EndDisabled();

        ImGui::SameLine();

        if (!canUp)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_UP "##navup"))
            NavigateTo(m_currentPath.parent_path());
        if (!canUp)
            ImGui::EndDisabled();

        ImGui::SameLine();
        auto currentPathU8 = m_currentPath.u8string();
        ImGui::Text("%s", reinterpret_cast<const char *>(currentPathU8.c_str()));

        if (showViewMode)
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            if (ImGui::Button("List"))
                m_viewMode = ViewMode::List;
            ImGui::SameLine();
            if (ImGui::Button("Grid"))
                m_viewMode = ViewMode::Grid;
        }
    }

    void FileBrowser::DrawItemContextMenu(const std::function<void(const std::filesystem::path &)> &onOpen)
    {
        if (!ImGui::BeginPopup("ItemContext##filebrowser"))
            return;

        if (ImGui::MenuItem("Open"))
            onOpen(m_selectedEntry);

        if (ImGui::MenuItem("Open in File Manager"))
            OpenInFileManager(m_selectedEntry);

        ImGui::Separator();

        if (ImGui::MenuItem("Rename", "F2"))
        {
            auto filename = m_selectedEntry.filename().string();
            std::strncpy(m_renameBuffer, filename.c_str(), sizeof(m_renameBuffer) - 1);
            m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
            m_renamingEntry = m_selectedEntry;
            m_renameNeedsFocus = true;
            m_pendingOpenRename = true;
        }

        if (ImGui::MenuItem("Copy", "Ctrl+C"))
            m_clipboardPath = m_selectedEntry;

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboardPath.empty()))
            TryStartCopy({m_clipboardPath});

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del"))
            m_pendingOpenDelete = true;

        ImGui::EndPopup();
    }

    void FileBrowser::DrawBackgroundContextMenu()
    {
        // Do not open if the per-item context menu is already active
        if (ImGui::IsPopupOpen("ItemContext##filebrowser"))
            return;

        if (!ImGui::BeginPopupContextWindow("##BgContext_filebrowser",
                                            ImGuiPopupFlags_MouseButtonRight |
                                                ImGuiPopupFlags_NoOpenOverItems))
            return;

        if (ImGui::MenuItem("New Folder"))
        {
            auto newFolder = m_currentPath / "New Folder";
            for (int i = 1; std::filesystem::exists(newFolder); ++i)
                newFolder = m_currentPath / ("New Folder " + std::to_string(i));

            try
            {
                std::filesystem::create_directory(newFolder);
                RefreshCache();
                m_selectedEntry = newFolder;
                auto filename = newFolder.filename().string();
                std::strncpy(m_renameBuffer, filename.c_str(), sizeof(m_renameBuffer) - 1);
                m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
                m_renamingEntry = newFolder;
                m_renameNeedsFocus = true;
                m_pendingOpenRename = true;
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                PE_WARN("[FileBrowser] Failed to create folder: %s", e.what());
            }
        }

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboardPath.empty()))
            TryStartCopy({m_clipboardPath});

        ImGui::Separator();

        if (ImGui::MenuItem("Refresh"))
            RefreshCache();

        ImGui::EndPopup();
    }

    void FileBrowser::DrawRenamePopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Rename##filebrowser", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
            return;

        ImGui::Text("New name:");
        if (m_renameNeedsFocus)
        {
            ImGui::SetKeyboardFocusHere();
            m_renameNeedsFocus = false;
        }

        bool confirmed = ImGui::InputText("##renameinput", m_renameBuffer, sizeof(m_renameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        if (confirmed || ImGui::Button("OK"))
        {
            if (m_renameBuffer[0] != '\0' && !m_renamingEntry.empty())
            {
                auto newPath = m_renamingEntry.parent_path() / m_renameBuffer;
                try
                {
                    std::filesystem::rename(m_renamingEntry, newPath);
                    if (m_selectedEntry == m_renamingEntry)
                        m_selectedEntry = newPath;
                    if (m_clipboardPath == m_renamingEntry)
                        m_clipboardPath = newPath;
                    RefreshCache();
                }
                catch (const std::filesystem::filesystem_error &e)
                {
                    PE_WARN("[FileBrowser] Rename failed: %s", e.what());
                }
            }
            m_renamingEntry.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_renamingEntry.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void FileBrowser::DrawDeleteConfirmPopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Delete##filebrowser", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
            return;

        ImGui::TextWrapped("Delete \"%s\"?", m_selectedEntry.filename().string().c_str());
        ImGui::Spacing();

        if (ImGui::Button("Delete"))
        {
            try
            {
                std::filesystem::remove_all(m_selectedEntry);
                if (m_clipboardPath == m_selectedEntry)
                    m_clipboardPath.clear();
                m_selectedEntry.clear();
                RefreshCache();
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                PE_WARN("[FileBrowser] Delete failed: %s", e.what());
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void FileBrowser::Update()
    {
        ProcessLoadedImages();

        if (m_copyState->needsRefresh.exchange(false))
            RefreshCache();

        if (m_open && !m_wasOpen)
            RefreshCache();
        m_wasOpen = m_open;

        if (!m_open)
            return;

        ui::SetInitialWindowSizeFraction(0.2f, 0.4f);
        if (ImGui::Begin("File Browser", &m_open))
        {
            // Process OS-level file drops when this window is focused
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                std::vector<std::filesystem::path> drops;
                {
                    std::lock_guard lock(m_pendingDropsMutex);
                    drops.swap(m_pendingOsDrops);
                }
                if (!drops.empty())
                    TryStartCopy(drops);
            }

            DrawCopyProgress();

            // Ctrl+C / Ctrl+V clipboard copy (only when window focused, not while a copy is running)
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !m_copyState->active)
            {
                const bool ctrlHeld = ImGui::GetIO().KeyCtrl;

                if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_C, false) && !m_selectedEntry.empty())
                    m_clipboardPath = m_selectedEntry;

                if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_V, false) && !m_clipboardPath.empty())
                    TryStartCopy({m_clipboardPath});
            }

            DrawNavBar(true);
            ImGui::Separator();

            auto onNormalActionCaptured = [this](const std::filesystem::path &path)
            {
                if (IsDirectory(path))
                {
                    NavigateTo(path);
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
                            try
                            {
                                if (Model *m = Model::Load(path))
                                    EventSystem::PushEvent(EventType::ModelLoaded, m);
                            }
                            catch (const std::exception &e)
                            {
                                PE_WARN("[Scene] Failed to load model: %s", e.what());
                            }
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
                    else
                    {
                        OpenWithSystem(path);
                    }
                }
            };

            DrawDirectoryContent(m_currentPath, onNormalActionCaptured);

            // Keyboard shortcuts (F2 = rename, Del = delete)
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !m_selectedEntry.empty())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
                {
                    auto filename = m_selectedEntry.filename().string();
                    std::strncpy(m_renameBuffer, filename.c_str(), sizeof(m_renameBuffer) - 1);
                    m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
                    m_renamingEntry = m_selectedEntry;
                    m_renameNeedsFocus = true;
                    m_pendingOpenRename = true;
                }

                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                    m_pendingOpenDelete = true;
            }

            // Open modals deferred from context menus / shortcuts
            if (m_pendingOpenRename)
            {
                ImGui::OpenPopup("Rename##filebrowser");
                m_pendingOpenRename = false;
            }
            if (m_pendingOpenDelete)
            {
                ImGui::OpenPopup("Delete##filebrowser");
                m_pendingOpenDelete = false;
            }
            if (m_pendingOpenCopyProgress)
            {
                ImGui::OpenPopup("Copying Files##filebrowser");
                m_pendingOpenCopyProgress = false;
            }
            if (m_pendingOpenOverwritePrompt)
            {
                ImGui::OpenPopup("Overwrite?##filebrowser");
                m_pendingOpenOverwritePrompt = false;
            }

            DrawRenamePopup();
            DrawDeleteConfirmPopup();
            DrawOverwritePrompt();
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
                PE_ERROR("[FileBrowser] Error accessing directory: %s", e.what());
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

                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            m_selectedEntry = entry.path;
                            ImGui::OpenPopup("ItemContext##filebrowser");
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

                            bool isSelected = (m_selectedEntry == entry.path);

                            ImGui::BeginGroup();
                            float groupStartY = ImGui::GetCursorPosY();

                            if (isSelected)
                                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

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

                            if (isSelected)
                                ImGui::PopStyleColor();

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

                            if (isSelected)
                            {
                                ImGui::GetWindowDrawList()->AddRectFilled(
                                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                    IM_COL32(100, 140, 220, 60));
                            }

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                            {
                                m_selectedEntry = entry.path;
                                onDoubleClick(entry.path);
                            }
                            else if (clicked || ImGui::IsItemClicked())
                            {
                                m_selectedEntry = entry.path;
                            }

                            bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                            if (rightClicked)
                                m_selectedEntry = entry.path;

                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                std::string pathStr = entry.path.string();
                                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr.c_str(), pathStr.length() + 1);
                                ImGui::Text("%s", entry.filename.c_str());
                                ImGui::EndDragDropSource();
                            }

                            ImGui::PopID();

                            // OpenPopup must be called outside PushID scope so the ID matches BeginPopup
                            if (rightClicked)
                                ImGui::OpenPopup("ItemContext##filebrowser");
                        }
                    }
                }
            }

            DrawItemContextMenu(onDoubleClick);
            DrawBackgroundContextMenu();
        }
        ImGui::EndChild();
    }
} // namespace pe
