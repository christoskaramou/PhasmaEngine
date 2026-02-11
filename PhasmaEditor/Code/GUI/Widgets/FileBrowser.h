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

        inline static bool IsRegularFile(const std::filesystem::path &path) { return std::filesystem::is_regular_file(path); }
        inline static bool IsDirectory(const std::filesystem::path &path) { return std::filesystem::is_directory(path); }
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

    protected:
        friend class GUI;

        void *GetIconForFile(const std::filesystem::path &path);
        void ProcessLoadedImages();
        void RefreshCache();
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
    };
} // namespace pe
