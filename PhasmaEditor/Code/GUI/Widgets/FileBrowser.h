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

    private:
        void *GetIconForFile(const std::filesystem::path &path) const;

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

        Image *m_headerIcon = nullptr;
        void *m_headerIconDS = nullptr;

        Image *m_cppIcon = nullptr;
        void *m_cppIconDS = nullptr;

        enum class ViewMode
        {
            List,
            Grid
        };
        ViewMode m_viewMode = ViewMode::Grid;
        float m_gridIconSize = 64.0f;
    };
} // namespace pe
