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
        std::filesystem::path m_currentPath;
        std::filesystem::path m_selectedEntry;
        Image *m_folderIcon = nullptr;
        Image *m_fileIcon = nullptr;
        void *m_folderIconDS = nullptr;
        void *m_fileIconDS = nullptr;

        enum class ViewMode
        {
            List,
            Grid
        };
        ViewMode m_viewMode = ViewMode::List;
        float m_gridIconSize = 64.0f;
    };
} // namespace pe
