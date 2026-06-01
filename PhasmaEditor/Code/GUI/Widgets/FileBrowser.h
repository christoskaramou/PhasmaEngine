#pragma once

#include "GUI/Widget.h"

namespace pe
{
    class FileBrowser : public Widget
    {
    public:
        FileBrowser(const std::string &name = "File Browser") : Widget(name) {}
        ~FileBrowser() override;
        void Init(GUI *gui) override;
        void Update() override;

        void SetCurrentPath(const std::filesystem::path &path);
        void RefreshCache();

        // Non-throwing: protected reparse points (e.g. C:\Users\All Users) make the throwing overloads
        // raise access-denied mid-navigation, which would crash the editor. Treat any error as "no".
        inline static bool IsRegularFile(const std::filesystem::path &path)
        {
            std::error_code ec;
            return std::filesystem::is_regular_file(path, ec);
        }
        inline static bool IsDirectory(const std::filesystem::path &path)
        {
            std::error_code ec;
            return std::filesystem::is_directory(path, ec);
        }
        inline static bool IsTextFile(const std::filesystem::path &path)
        {
            auto u8ext = path.extension().u8string();
            std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
            return s_textExtensions.find(ext) != s_textExtensions.end();
        }
        inline static bool IsShaderFile(const std::filesystem::path &path)
        {
            auto u8ext = path.extension().u8string();
            std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
            return s_shaderExtensions.find(ext) != s_shaderExtensions.end();
        }
        inline static bool IsScriptFile(const std::filesystem::path &path)
        {
            auto u8ext = path.extension().u8string();
            std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
            return s_scriptExtensions.find(ext) != s_scriptExtensions.end();
        }
        inline static bool IsImageFile(const std::filesystem::path &path)
        {
            auto u8ext = path.extension().u8string();
            std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
            return s_imageExtensions.find(ext) != s_imageExtensions.end();
        }
        inline static bool IsModelFile(const std::filesystem::path &path)
        {
            auto u8ext = path.extension().u8string();
            std::string ext(reinterpret_cast<const char *>(u8ext.c_str()));
            return s_modelExtensions.find(ext) != s_modelExtensions.end();
        }
        // Only cooked ".pemesh" assets load directly into the scene; source models (glTF/FBX/OBJ/...)
        // are import-only (File > Import cooks them to .pemesh first).
        static bool IsCookedModelFile(const std::filesystem::path &path);
        // True for an importable source model (an extension in s_modelExtensionsVec): glTF/FBX/OBJ/...
        // Drives the File > Import pickers and the folder-import cook/skip decision.
        static bool IsSourceModelFile(const std::filesystem::path &path);

    protected:
        friend class GUI;

        void *GetIconForFile(const std::filesystem::path &path);
        void ProcessLoadedImages();
        void *RegisterImageForImGui(Image *image);
        void ReleaseImGuiTexture(void *&textureID);
        void DrawDirectoryContent(const std::filesystem::path &path,
                                  std::function<void(const std::filesystem::path &)> onDoubleClick = nullptr,
                                  std::function<bool(const std::filesystem::path &)> filter = nullptr,
                                  float footerHeight = 0.0f);

        static std::unordered_set<std::string> s_textExtensions;
        static std::unordered_set<std::string> s_shaderExtensions;
        static std::unordered_set<std::string> s_scriptExtensions;
        static std::unordered_set<std::string> s_imageExtensions;
        static std::unordered_set<std::string> s_modelExtensions;
        static std::vector<const char *> s_modelExtensionsVec;

        std::filesystem::path m_currentPath;
        std::filesystem::path m_selectedEntry;
        Image *m_folderIcon = nullptr;
        Image *m_fileIcon = nullptr;
        void *m_folderIconDS = nullptr;
        void *m_fileIconDS = nullptr;
        Image *m_txtIcon = nullptr;
        void *m_txtIconDS = nullptr;
        Image *m_shaderIcon = nullptr;
        void *m_shaderIconDS = nullptr;
        Image *m_modelIcon = nullptr;
        void *m_modelIconDS = nullptr;
        Image *m_scriptIcon = nullptr;
        void *m_scriptIconDS = nullptr;
        Image *m_imageIcon = nullptr;
        void *m_imageIconDS = nullptr;

        // Thumbnail Cache
        std::unordered_map<std::string, Image *> m_fileCache;
        std::unordered_map<std::string, void *> m_fileDescriptors;
        std::unordered_set<std::string> m_pendingFiles;
        std::vector<std::pair<std::string, Image *>> m_loadedQueue;
        std::mutex m_queueMutex;

        enum class ViewMode
        {
            List,
            Grid
        };
        ViewMode m_viewMode = ViewMode::Grid;
        float m_gridIconSize = 64.0f;

        struct FileEntry
        {
            std::filesystem::path path;
            std::string filename; // Cached UTF-8 filename
            bool isDirectory;
            void *iconID;
        };
        std::vector<FileEntry> m_cache;
        std::filesystem::path m_cachePath; // Path currently cached
        bool m_wasOpen = false;
        std::filesystem::path m_clipboardPath; // set by Ctrl+C, consumed by Ctrl+V

        // Navigation history (back / forward / up)
        std::vector<std::filesystem::path> m_navHistory;
        int m_navHistoryIndex = -1;
        void NavigateTo(const std::filesystem::path &path);

        std::vector<std::filesystem::path> m_pendingOsDrops;
        std::mutex m_pendingDropsMutex;

        struct FileCopyState
        {
            std::atomic<bool> active{false};
            std::atomic<float> progress{0.0f};
            std::atomic<bool> needsRefresh{false};
            std::string label;
            std::mutex labelMutex;
        };
        std::shared_ptr<FileCopyState> m_copyState = std::make_shared<FileCopyState>();

        void TryStartCopy(const std::vector<std::filesystem::path> &sources);
        void CopyDroppedFiles(const std::vector<std::filesystem::path> &sources, bool skipExisting = false);
        void DrawCopyProgress();

        // Context menu / action state
        std::filesystem::path m_renamingEntry;
        char m_renameBuffer[256] = {};
        bool m_renameNeedsFocus = false;
        bool m_pendingOpenRename = false;
        bool m_pendingOpenDelete = false;
        bool m_pendingOpenCopyProgress = false;
        bool m_pendingOpenOverwritePrompt = false;
        std::vector<std::filesystem::path> m_pendingCopySources;
        std::vector<std::string> m_conflictNames;

        EventSystem::CallbackToken m_fileDropToken{0};

        void DrawNavBar(bool showViewMode = true);
        void DrawItemContextMenu(const std::function<void(const std::filesystem::path &)> &onOpen);
        void DrawBackgroundContextMenu();
        void DrawRenamePopup();
        void DrawDeleteConfirmPopup();
        void DrawOverwritePrompt();
        void OpenInFileManager(const std::filesystem::path &path);
        void OpenWithSystem(const std::filesystem::path &path);
    };
} // namespace pe
