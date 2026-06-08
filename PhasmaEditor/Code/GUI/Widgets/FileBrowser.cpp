#include "FileBrowser.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/ModelAsset.h"
#if defined(PE_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

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

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::string PathToUtf8(const std::filesystem::path &path)
        {
            auto u8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(u8.c_str()));
        }

        bool IsPathAncestorOf(const std::filesystem::path &ancestor, const std::filesystem::path &path)
        {
            auto ancestorIt = ancestor.begin();
            auto pathIt = path.begin();
            for (; ancestorIt != ancestor.end(); ++ancestorIt, ++pathIt)
            {
                if (pathIt == path.end() || *ancestorIt != *pathIt)
                    return false;
            }

            return true;
        }

        std::filesystem::file_time_type SafeLastWriteTime(const std::filesystem::path &path)
        {
            std::error_code ec;
            auto time = std::filesystem::last_write_time(path, ec);
            return ec ? std::filesystem::file_time_type{} : time;
        }

        uintmax_t SafeFileSize(const std::filesystem::path &path)
        {
            std::error_code ec;
            auto size = std::filesystem::file_size(path, ec);
            return ec ? 0 : size;
        }

        void LoadIcon(CommandBuffer *cmd, const std::string &path, Image *&outIcon)
        {
            outIcon = Image::LoadRGBA8(cmd, path);
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
        ".json", ".mat", ".md", ".txt", ".xml"};
    std::unordered_set<std::string> FileBrowser::s_shaderExtensions = {
        ".comp", ".frag", ".glsl", ".hlsl", ".vert"};
    std::unordered_set<std::string> FileBrowser::s_scriptExtensions = {
        ".pecpp", ".peh"};
    std::unordered_set<std::string> FileBrowser::s_imageExtensions = {
        ".bmp", ".gif", ".hdr", ".jpeg", ".jpg", ".pic", ".png", ".psd", ".tga"};
    // A "loadable model" in the editor is a cooked ".pemesh"; source formats (glTF/FBX/OBJ/...) are
    // import-only and live in s_modelExtensionsVec (the File > Import picker). This set drives the mesh
    // icon, the browser model preview, and every scene-load affordance.
    std::unordered_set<std::string> FileBrowser::s_modelExtensions = {".pemesh"};
    std::vector<const char *> FileBrowser::s_modelExtensionsVec = {
        ".3d", ".3ds", ".3mf", ".amf", ".ase", ".assbin", ".b3d", ".blend", ".bvh", ".cob",
        ".collada", ".csm", ".dae", ".dxf", ".fbx", ".glb", ".gltf", ".hmp", ".ifc", ".iqm",
        ".irr", ".irrmesh", ".lwo", ".lws", ".m3d", ".md2", ".md3", ".md5", ".mdc", ".mdl",
        ".mmd", ".ms3d", ".ndo", ".nff", ".obj", ".off", ".ogre", ".opengex", ".ply",
        ".q3bsp", ".q3d", ".raw", ".sib", ".smd", ".stl", ".terragen", ".x", ".x3d", ".xgl"};

    bool FileBrowser::IsCookedModelFile(const std::filesystem::path &path)
    {
        auto u8ext = path.extension().u8string();
        std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return ext == ".pemesh";
    }

    bool FileBrowser::IsSourceModelFile(const std::filesystem::path &path)
    {
        auto u8ext = path.extension().u8string();
        std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        for (const char *me : s_modelExtensionsVec)
            if (ext == me)
                return true;
        return false;
    }

    FileBrowser::~FileBrowser()
    {
        EventSystem::UnregisterCallback(EventType::FileDrop, m_fileDropToken);

        ReleaseImGuiTexture(m_folderIconDS);
        ReleaseImGuiTexture(m_fileIconDS);
        ReleaseImGuiTexture(m_txtIconDS);
        ReleaseImGuiTexture(m_shaderIconDS);
        ReleaseImGuiTexture(m_modelIconDS);
        ReleaseImGuiTexture(m_scriptIconDS);
        ReleaseImGuiTexture(m_imageIconDS);

        for (auto &pair : m_fileDescriptors)
            ReleaseImGuiTexture(pair.second);
        m_fileDescriptors.clear();

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
    }

    void FileBrowser::Init(GUI *gui)
    {
        Widget::Init(gui);
        m_folderTreeRoot = StripTrailingSep(std::filesystem::path(Path::Assets).lexically_normal());
        m_currentPath = m_folderTreeRoot;
        m_navHistory.push_back(m_currentPath);
        m_navHistoryIndex = 0;

        // Load Icons
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        LoadIcon(cmd, Path::EditorAssets + "Icons/folder_icon.png", m_folderIcon);
        LoadIcon(cmd, Path::EditorAssets + "Icons/file_icon.png", m_fileIcon);
        LoadIcon(cmd, Path::EditorAssets + "Icons/txt_icon.png", m_txtIcon);
        LoadIcon(cmd, Path::EditorAssets + "Icons/shader_icon.png", m_shaderIcon);
        LoadIcon(cmd, Path::EditorAssets + "Icons/model_icon.png", m_modelIcon);
        LoadIcon(cmd, Path::EditorAssets + "Icons/script_icon.png", m_scriptIcon);
        LoadIcon(cmd, Path::EditorAssets + "Icons/image_icon.png", m_imageIcon);

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        m_folderIconDS = RegisterImageForImGui(m_folderIcon);
        m_fileIconDS = RegisterImageForImGui(m_fileIcon);
        m_txtIconDS = RegisterImageForImGui(m_txtIcon);
        m_shaderIconDS = RegisterImageForImGui(m_shaderIcon);
        m_modelIconDS = RegisterImageForImGui(m_modelIcon);
        m_scriptIconDS = RegisterImageForImGui(m_scriptIcon);
        m_imageIconDS = RegisterImageForImGui(m_imageIcon);

        m_fileDropToken = EventSystem::RegisterCallbackWithToken(EventType::FileDrop, [this](const std::any &data)
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

    void *FileBrowser::GetIconForFile(const std::filesystem::path &path, bool loadImageThumbnail)
    {
        FileEntry entry;
        entry.path = path;
        entry.isDirectory = IsDirectory(path);
        return GetIconForEntry(entry, loadImageThumbnail);
    }

    void *FileBrowser::GetIconForEntry(const FileEntry &entry, bool loadImageThumbnail)
    {
        if (entry.isDirectory)
            return m_folderIconDS;
        if (IsTextFile(entry.path))
            return m_txtIconDS ? m_txtIconDS : m_fileIconDS;
        if (IsShaderFile(entry.path))
            return m_shaderIconDS ? m_shaderIconDS : m_fileIconDS;
        // The mesh icon denotes the engine's cooked ".pemesh" asset; source models (glTF/FBX/OBJ/...)
        // are import inputs only and fall through to the generic file icon.
        if (IsCookedModelFile(entry.path))
            return m_modelIconDS ? m_modelIconDS : m_fileIconDS;
        if (IsScriptFile(entry.path))
            return m_scriptIconDS ? m_scriptIconDS : m_fileIconDS;
        if (IsImageFile(entry.path))
        {
            auto u8str = entry.path.u8string();
            std::string pathStr(reinterpret_cast<const char *>(u8str.c_str()));

            // Check cache
            auto it = m_fileDescriptors.find(pathStr);
            if (it != m_fileDescriptors.end())
                return it->second;

            // Check if already pending
            if (loadImageThumbnail && m_pendingFiles.find(pathStr) == m_pendingFiles.end())
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
                void *ds = RegisterImageForImGui(pair.second);
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

    void *FileBrowser::RegisterImageForImGui(Image *image)
    {
        if (!m_gui)
            return nullptr;

        return m_gui->RegisterImageTexture(image);
    }

    void FileBrowser::ReleaseImGuiTexture(void *&textureID)
    {
        if (!textureID)
            return;

        if (m_gui)
            m_gui->ReleaseImageTexture(textureID);
        else
            textureID = nullptr;
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
        ui::ItemTooltip("Copy all dropped files and replace existing files.");
        ImGui::SameLine();
        if (ImGui::Button("Skip Existing"))
        {
            CopyDroppedFiles(m_pendingCopySources, true);
            m_pendingOpenCopyProgress = true;
            m_pendingCopySources.clear();
            ImGui::CloseCurrentPopup();
        }
        ui::ItemTooltip("Copy only files that do not already exist here.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_pendingCopySources.clear();
            ImGui::CloseCurrentPopup();
        }
        ui::ItemTooltip("Cancel the pending file copy.");

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
            RefreshCache(false);
        }
        ui::ItemTooltip("Go back to the previous folder.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canBack)
            ImGui::EndDisabled();

        ImGui::SameLine();

        if (!canForward)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_RIGHT "##navfwd"))
        {
            ++m_navHistoryIndex;
            m_currentPath = m_navHistory[m_navHistoryIndex];
            RefreshCache(false);
        }
        ui::ItemTooltip("Go forward in folder history.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!canForward)
            ImGui::EndDisabled();

        ImGui::SameLine();

        if (!canUp)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_UP "##navup"))
            NavigateTo(m_currentPath.parent_path());
        ui::ItemTooltip("Go to the parent folder.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
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
            ui::ItemTooltip("Show files in a compact list.");
            ImGui::SameLine();
            if (ImGui::Button("Grid"))
                m_viewMode = ViewMode::Grid;
            ui::ItemTooltip("Show files as thumbnail tiles.");
        }
    }

    void FileBrowser::DrawFilterSortBar()
    {
        ImGui::PushItemWidth(220.0f);
        ImGui::InputTextWithHint("##filebrowser_search", "Search files", m_searchBuffer, sizeof(m_searchBuffer));
        ui::ItemTooltip("Filter visible files and folders by name.");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        static const char *sortLabels[] = {"Name", "Date", "Size"};
        int sortIndex = static_cast<int>(m_sortMode);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("Sort##filebrowser_sort", &sortIndex, sortLabels, IM_ARRAYSIZE(sortLabels)))
        {
            m_sortMode = static_cast<SortMode>(sortIndex);
            SortCache();
        }
        ui::ItemTooltip("Choose the column used to sort entries.");

        ImGui::SameLine();
        if (ImGui::Button(m_sortDescending ? "Desc##filebrowser_sortdir" : "Asc##filebrowser_sortdir"))
        {
            m_sortDescending = !m_sortDescending;
            SortCache();
        }
        ui::ItemTooltip("Toggle ascending or descending sort order.");
    }

    void FileBrowser::DrawFolderTree(float height)
    {
        if (m_folderTreeRoot.empty())
            m_folderTreeRoot = StripTrailingSep(std::filesystem::path(Path::Assets).lexically_normal());

        ImGui::BeginChild("##filebrowser_folder_tree", ImVec2(m_folderTreeWidth, height), true);
        ImGui::TextUnformatted("Folders");
        ImGui::Separator();

        const ImGuiStyle &style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, style.IndentSpacing * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * 0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(style.ItemInnerSpacing.x * 0.5f, 0.0f));

        std::filesystem::path treeSelectionPath = StripTrailingSep(m_currentPath.lexically_normal());
        if (!m_selectedEntry.empty())
        {
            const std::filesystem::path selectedPath = StripTrailingSep(m_selectedEntry.lexically_normal());
            const std::filesystem::path selectedParent = StripTrailingSep(selectedPath.parent_path().lexically_normal());
            if (selectedParent == treeSelectionPath)
            {
                const auto selectedIt = std::find_if(m_cache.begin(), m_cache.end(), [&selectedPath](const FileEntry &cacheEntry)
                                                     { return cacheEntry.isDirectory &&
                                                              StripTrailingSep(cacheEntry.path.lexically_normal()) == selectedPath; });
                if (selectedIt != m_cache.end())
                    treeSelectionPath = selectedPath;
            }
        }

        const DirectoryCacheEntry &rootEntry = GetDirectoryCacheEntry(m_folderTreeRoot, false, false);
        m_expandedFolderTreePaths.insert(rootEntry.node.pathId);

        const std::string treeSelectionPathId = PathToUtf8(treeSelectionPath);
        if (m_folderTreeRows.empty() ||
            m_folderTreeRowsDirectoryGeneration != m_directoryCacheGeneration ||
            m_folderTreeRowsExpansionGeneration != m_folderTreeExpansionGeneration ||
            m_folderTreeRowsSelectionPathId != treeSelectionPathId)
        {
            m_folderTreeRows.clear();
            m_folderTreeRows.reserve(128);
            BuildFolderTreeRows(rootEntry.node, 0, treeSelectionPath);
            m_folderTreeRowsDirectoryGeneration = m_directoryCacheGeneration;
            m_folderTreeRowsExpansionGeneration = m_folderTreeExpansionGeneration;
            m_folderTreeRowsSelectionPathId = treeSelectionPathId;
        }

        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_folderTreeRows.size()), rowHeight);
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const FolderTreeRow &row = m_folderTreeRows[i];
                const FolderTreeChild &node = *row.node;
                const bool selected = node.normalizedPath == treeSelectionPath;

                ImGui::PushID(node.pathId.c_str());
                ImGui::Selectable("##folder_tree_row", selected, ImGuiSelectableFlags_SpanAvailWidth,
                                  ImVec2(0.0f, rowHeight));

                const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool rowHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const float contentX = itemMin.x + static_cast<float>(row.depth) * style.IndentSpacing;
                const float textY = itemMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                constexpr float arrowWidth = 14.0f;
                const char *arrow = row.hasChildFolders ? (row.expanded ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT) : " ";

                ImDrawList *drawList = ImGui::GetWindowDrawList();
                drawList->AddText(ImVec2(contentX, textY), ImGui::GetColorU32(ImGuiCol_Text), arrow);
                drawList->AddText(ImVec2(contentX + arrowWidth, textY), ImGui::GetColorU32(ImGuiCol_Text),
                                  node.label.c_str());

                const float mouseX = ImGui::GetMousePos().x;
                if ((clicked || doubleClicked) && row.hasChildFolders &&
                    mouseX >= contentX && mouseX < contentX + arrowWidth)
                {
                    if (row.expanded)
                        m_expandedFolderTreePaths.erase(node.pathId);
                    else
                        m_expandedFolderTreePaths.insert(node.pathId);
                    ++m_folderTreeExpansionGeneration;
                }
                else if (clicked || doubleClicked)
                {
                    NavigateTo(node.normalizedPath);
                }
                if (rowHovered)
                    ui::TooltipText(row.hasChildFolders ? "Open this folder; click the arrow to expand or collapse it." : "Open this folder.");

                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar(4);
        ImGui::EndChild();
    }

    const FileBrowser::DirectoryCacheEntry &FileBrowser::GetDirectoryCacheEntry(const std::filesystem::path &path,
                                                                                bool forceRefresh,
                                                                                bool loadEntries)
    {
        const std::filesystem::path normalizedPath = StripTrailingSep(path.lexically_normal());
        const std::string pathStr = PathToUtf8(normalizedPath);

        auto cached = m_directoryCache.find(pathStr);
        if (!forceRefresh && cached != m_directoryCache.end())
        {
            if ((loadEntries && cached->second.entriesLoaded) || (!loadEntries && cached->second.childFoldersLoaded))
                return cached->second;
        }

        DirectoryCacheEntry entry;
        entry.generation = ++m_directoryCacheGeneration;
        entry.node.path = normalizedPath;
        entry.node.normalizedPath = normalizedPath;
        entry.node.pathId = pathStr;
        entry.node.label = normalizedPath.filename().empty() ? pathStr : PathToUtf8(normalizedPath.filename());

        std::error_code ec;
        entry.isDirectory = std::filesystem::is_directory(normalizedPath, ec);
        if (entry.isDirectory)
        {
            entry.childFoldersLoaded = true;
            entry.entriesLoaded = loadEntries;

            std::error_code itEc;
            auto it = std::filesystem::directory_iterator(
                normalizedPath, std::filesystem::directory_options::skip_permission_denied, itEc);
            const std::filesystem::directory_iterator end;
            for (; it != end; it.increment(itEc))
            {
                if (itEc)
                {
                    itEc.clear();
                    continue;
                }

                std::error_code dirEc;
                const bool isDirectory = it->is_directory(dirEc);
                if (isDirectory)
                {
                    FolderTreeChild child;
                    child.path = it->path();
                    child.normalizedPath = StripTrailingSep(child.path.lexically_normal());
                    child.pathId = PathToUtf8(child.normalizedPath);
                    child.label = child.normalizedPath.filename().empty() ? child.pathId : PathToUtf8(child.normalizedPath.filename());
                    entry.childFolders.push_back(std::move(child));
                }

                if (loadEntries)
                {
                    FileEntry fileEntry;
                    fileEntry.path = it->path();

                    auto filenameU8 = fileEntry.path.filename().u8string();
                    fileEntry.filename = std::string(reinterpret_cast<const char *>(filenameU8.c_str()));

                    fileEntry.isDirectory = isDirectory;
                    fileEntry.size = fileEntry.isDirectory ? 0 : SafeFileSize(fileEntry.path);
                    fileEntry.lastWriteTime = SafeLastWriteTime(fileEntry.path);

                    entry.entries.push_back(std::move(fileEntry));
                }
            }

            std::sort(entry.childFolders.begin(), entry.childFolders.end(),
                      [](const FolderTreeChild &a, const FolderTreeChild &b)
                      { return ToLower(a.label) < ToLower(b.label); });
        }

        auto [inserted, _] = m_directoryCache.insert_or_assign(pathStr, std::move(entry));
        return inserted->second;
    }

    void FileBrowser::BuildFolderTreeRows(const FolderTreeChild &node,
                                          int depth,
                                          const std::filesystem::path &treeSelectionPath)
    {
        auto cached = m_directoryCache.find(node.pathId);
        const DirectoryCacheEntry *entry = cached != m_directoryCache.end() ? &cached->second : nullptr;
        if (entry && !entry->isDirectory)
            return;

        const bool childFoldersKnown = entry && entry->childFoldersLoaded;
        const bool hasChildFolders = !childFoldersKnown || !entry->childFolders.empty();
        const bool selected = node.normalizedPath == treeSelectionPath;
        const bool ancestor = !selected && IsPathAncestorOf(node.normalizedPath, treeSelectionPath);
        const bool expanded = depth == 0 || ancestor || m_expandedFolderTreePaths.find(node.pathId) != m_expandedFolderTreePaths.end();

        m_folderTreeRows.push_back({&node, depth, expanded, hasChildFolders});

        if (!expanded || !hasChildFolders)
            return;

        const DirectoryCacheEntry &openEntry = GetDirectoryCacheEntry(node.normalizedPath, false, false);
        for (const auto &folder : openEntry.childFolders)
            BuildFolderTreeRows(folder, depth + 1, treeSelectionPath);
    }

    void FileBrowser::DrawItemContextMenu(const std::function<void(const std::filesystem::path &)> &onOpen)
    {
        if (!ImGui::BeginPopup("ItemContext##filebrowser"))
            return;

        if (ImGui::MenuItem("Open"))
            onOpen(m_selectedEntry);
        ui::ItemTooltip("Open this entry with the editor's default action.");

        if (ImGui::MenuItem("Open in File Manager"))
            OpenInFileManager(m_selectedEntry);
        ui::ItemTooltip("Reveal this entry in the operating system file manager.");

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
        ui::ItemTooltip("Rename this file or folder.");

        if (ImGui::MenuItem("Copy", "Ctrl+C"))
            m_clipboardPath = m_selectedEntry;
        ui::ItemTooltip("Copy this entry for pasting into another folder.");

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboardPath.empty()))
            TryStartCopy({m_clipboardPath});
        ui::ItemTooltip("Paste the copied entry into this folder.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del"))
            m_pendingOpenDelete = true;
        ui::ItemTooltip("Delete this file or folder.");

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
        ui::ItemTooltip("Create a new folder in the current directory.");

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboardPath.empty()))
            TryStartCopy({m_clipboardPath});
        ui::ItemTooltip("Paste the copied entry into the current folder.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Separator();

        if (ImGui::MenuItem("Refresh"))
            RefreshCache();
        ui::ItemTooltip("Refresh this folder from disk.");

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
        ui::ItemTooltip("New name for the selected file or folder.");
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
        ui::ItemTooltip("Apply the rename.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_renamingEntry.clear();
            ImGui::CloseCurrentPopup();
        }
        ui::ItemTooltip("Close without renaming.");

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
        ui::ItemTooltip("Permanently delete this file or folder.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ui::ItemTooltip("Close without deleting.");

        ImGui::EndPopup();
    }

    void FileBrowser::Update()
    {
        ProcessLoadedImages();

        if (m_copyState->needsRefresh.exchange(false))
            RefreshCache();

        if (m_open && !m_wasOpen)
            RefreshCache(false);
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
            DrawFilterSortBar();
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
                        type = AssetPreviewType::ModelAsset;
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

                    // Only cooked .pemesh loads into the scene; source models are import-only (File > Import).
                    if (type == AssetPreviewType::ModelAsset && IsCookedModelFile(path) && !GUIState::s_modelLoading)
                    {
                        auto loadAsync = [path]()
                        {
                            GUIState::s_modelLoading = true;
                            try
                            {
                                if (ModelAsset *m = ModelAsset::Load(path))
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

    bool FileBrowser::MatchesBrowserFilters(
        const FileEntry &entry, const std::function<bool(const std::filesystem::path &)> &externalFilter) const
    {
        if (externalFilter && !externalFilter(entry.path))
            return false;

        if (m_searchBuffer[0] != '\0')
        {
            std::string filename = ToLower(entry.filename);
            std::string search = ToLower(m_searchBuffer);
            if (filename.find(search) == std::string::npos)
                return false;
        }

        return true;
    }

    std::vector<const FileBrowser::FileEntry *> FileBrowser::BuildVisibleEntries(
        const std::function<bool(const std::filesystem::path &)> &externalFilter) const
    {
        std::vector<const FileEntry *> visible;
        visible.reserve(m_cache.size());
        for (const auto &entry : m_cache)
            if (MatchesBrowserFilters(entry, externalFilter))
                visible.push_back(&entry);
        return visible;
    }

    void FileBrowser::SortCache()
    {
        std::sort(m_cache.begin(), m_cache.end(), [this](const FileEntry &a, const FileEntry &b)
                  {
                      if (a.isDirectory != b.isDirectory)
                          return a.isDirectory > b.isDirectory;

                      int result = 0;
                      switch (m_sortMode)
                      {
                      case SortMode::Date:
                          if (a.lastWriteTime < b.lastWriteTime)
                              result = -1;
                          else if (b.lastWriteTime < a.lastWriteTime)
                              result = 1;
                          break;
                      case SortMode::Size:
                          if (a.size < b.size)
                              result = -1;
                          else if (a.size > b.size)
                              result = 1;
                          break;
                      case SortMode::Name:
                      default:
                          break;
                      }

                      if (result == 0)
                      {
                          std::string aName = ToLower(a.filename);
                          std::string bName = ToLower(b.filename);
                          if (aName < bName)
                              result = -1;
                          else if (aName > bName)
                              result = 1;
                      }

                      return m_sortDescending ? result > 0 : result < 0; });
    }

    void FileBrowser::RefreshCache(bool forceRefresh)
    {
        const std::filesystem::path cachePath = StripTrailingSep(m_currentPath.lexically_normal());
        if (forceRefresh)
        {
            m_directoryCache.clear();
            m_folderTreeRows.clear();
            m_cacheGeneration = 0;
            ++m_directoryCacheGeneration;
        }

        const DirectoryCacheEntry &directory = GetDirectoryCacheEntry(cachePath, false, true);

        if (!directory.isDirectory)
        {
            m_cache.clear();
            m_cachePath = cachePath;
            m_cacheGeneration = directory.generation;
            return;
        }

        if (!forceRefresh && m_cachePath == cachePath && m_cacheGeneration == directory.generation)
            return;

        m_cache = directory.entries;
        m_cachePath = cachePath;
        m_cacheGeneration = directory.generation;
        SortCache();
    }

    void FileBrowser::DrawDirectoryContent(const std::filesystem::path &path,
                                           std::function<void(const std::filesystem::path &)> onDoubleClick,
                                           std::function<bool(const std::filesystem::path &)> filter,
                                           float footerHeight)
    {
        m_currentPath = StripTrailingSep(path.lexically_normal());
        RefreshCache(false);

        if (footerHeight == 0.0f)
            footerHeight = ImGui::GetStyle().ItemSpacing.y;

        auto visibleEntries = BuildVisibleEntries(filter);
        const bool showFolderTree = (m_name == "File Browser");
        if (showFolderTree)
        {
            const float splitterWidth = 6.0f;
            const float minFolderTreeWidth = 140.0f;
            const float minContentWidth = 220.0f;
            const float contentHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
            const float maxFolderTreeWidth = std::max(minFolderTreeWidth,
                                                      ImGui::GetContentRegionAvail().x - splitterWidth - minContentWidth);
            m_folderTreeWidth = std::min(std::max(m_folderTreeWidth, minFolderTreeWidth), maxFolderTreeWidth);

            DrawFolderTree(contentHeight);
            ImGui::SameLine(0.0f, 0.0f);

            ImGui::InvisibleButton("##filebrowser_folder_tree_splitter", ImVec2(splitterWidth, contentHeight));
            if (ImGui::IsItemActive())
            {
                m_folderTreeWidth = std::min(std::max(m_folderTreeWidth + ImGui::GetIO().MouseDelta.x,
                                                      minFolderTreeWidth),
                                             maxFolderTreeWidth);
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ui::ItemTooltip("Drag to resize the folder tree.");

            const ImU32 splitterColor = ImGui::GetColorU32(
                ImGui::IsItemActive() ? ImGuiCol_SeparatorActive
                                      : (ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), splitterColor);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
        }

        if (ImGui::BeginChild("##file_browser_list", ImVec2(0, -footerHeight), true))
        {
            bool isList = (m_viewMode == ViewMode::List);
            int count = static_cast<int>(visibleEntries.size());

            if (isList)
            {
                ImGuiListClipper clipper;
                clipper.Begin(count);

                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                    {
                        const auto &entry = *visibleEntries[i];

                        bool isSelected = (m_selectedEntry == entry.path);

                        void *iconID = GetIconForEntry(entry, true);
                        if (iconID)
                        {
                            ImGui::Image((ImTextureID)iconID, ImVec2(20, 20));
                            ImGui::SameLine();
                        }

                        if (ImGui::Selectable(entry.filename.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            m_selectedEntry = entry.path;
                            if (ImGui::IsMouseDoubleClicked(0))
                                onDoubleClick(entry.path);
                        }
                        const bool entryHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);

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
                        if (entryHovered)
                            ui::TooltipText("Select this entry; double-click to open it or drag it into the scene.");
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

                            const auto &entry = *visibleEntries[index];

                            ImGui::PushID(index);

                            if (col > 0)
                                ImGui::SameLine();

                            bool isSelected = (m_selectedEntry == entry.path);

                            ImGui::BeginGroup();
                            float groupStartY = ImGui::GetCursorPosY();

                            if (isSelected)
                                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

                            bool clicked = false;
                            void *iconID = GetIconForEntry(entry, true);
                            if (iconID)
                            {
                                if (ImGui::ImageButton("##icon", (ImTextureID)iconID, ImVec2(buttonSize, buttonSize)))
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
                            const bool entryHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                            if (rightClicked)
                                m_selectedEntry = entry.path;

                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                std::string pathStr = entry.path.string();
                                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr.c_str(), pathStr.length() + 1);
                                ImGui::Text("%s", entry.filename.c_str());
                                ImGui::EndDragDropSource();
                            }
                            if (entryHovered)
                                ui::TooltipText("Select this entry; double-click to open it or drag it into the scene.");

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
