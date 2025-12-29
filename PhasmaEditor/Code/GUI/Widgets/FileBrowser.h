#pragma once

#include "GUI/Widget.h"

namespace pe
{
    class FileBrowser : public Widget
    {
    public:
        FileBrowser() : Widget("File Browser") {}
        ~FileBrowser() override;
        void Init(GUI *gui) override;
        void Update() override;

        inline static bool IsRegularFile(const std::filesystem::path &path) { return std::filesystem::is_regular_file(path); }
        inline static bool IsDirectory(const std::filesystem::path &path) { return std::filesystem::is_directory(path); }
        inline static bool IsTextFile(const std::filesystem::path &path) { return s_textExtensions.find(path.extension().string()) != s_textExtensions.end(); }
        inline static bool IsShaderFile(const std::filesystem::path &path) { return s_shaderExtensions.find(path.extension().string()) != s_shaderExtensions.end(); }
        inline static bool IsScriptFile(const std::filesystem::path &path) { return s_scriptExtensions.find(path.extension().string()) != s_scriptExtensions.end(); }
        inline static bool IsImageFile(const std::filesystem::path &path) { return s_imageExtensions.find(path.extension().string()) != s_imageExtensions.end(); }
        inline static bool IsModelFile(const std::filesystem::path &path) { return s_modelExtensions.find(path.extension().string()) != s_modelExtensions.end(); }

    private:
        friend class GUI;

        void *GetIconForFile(const std::filesystem::path &path) const;

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

        enum class ViewMode
        {
            List,
            Grid
        };
        ViewMode m_viewMode = ViewMode::Grid;
        float m_gridIconSize = 64.0f;
    };
} // namespace pe
