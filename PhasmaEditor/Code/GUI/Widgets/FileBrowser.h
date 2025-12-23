#pragma once

#include "GUI/Widget.h"

namespace pe
{
    class FileBrowser : public Widget
    {
    public:
        FileBrowser() : Widget("File Browser") {}
        void Init(GUI *gui) override;
        void Update() override;

    private:
        std::filesystem::path m_currentPath;
        std::filesystem::path m_selectedEntry;
    };
} // namespace pe
