#pragma once

#include "FileBrowser.h"

namespace pe
{
    class FileSelector : public FileBrowser
    {
    public:
        FileSelector();
        ~FileSelector() override = default;

        void Update() override;

        using FileSelectCallback = std::function<void(const std::string &)>;
        void OpenSelection(FileSelectCallback callback, const std::vector<std::string> &allowedExtensions = {});
        void CancelSelection();

    private:
        char m_currentFile[1024]{};
        std::filesystem::path m_prevSelectedEntry;
        FileSelectCallback m_selectionCallback;
        std::vector<std::string> m_allowedExtensions;
    };
} // namespace pe
