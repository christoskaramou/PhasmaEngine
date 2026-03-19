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

        // Return true to close the selector, false to keep it open
        using FileSelectCallback = std::function<bool(const std::string &)>;
        using CancelCallback = std::function<void()>;
        void OpenSelection(FileSelectCallback callback, const std::vector<std::string> &allowedExtensions = {}, const std::string &defaultPath = "", CancelCallback cancelCallback = {}, const std::string &defaultName = {}, const std::string &confirmLabel = "Select");
        void CancelSelection();

    private:
        char m_currentFile[1024]{};
        std::filesystem::path m_prevSelectedEntry;
        FileSelectCallback m_selectionCallback;
        CancelCallback m_cancelCallback;
        std::vector<std::string> m_allowedExtensions;
        bool m_focusOnce = false;
        int m_selectStemLen = -1; // select [0, stemLen) on first InputText callback
        std::string m_confirmLabel = "Select";
    };
} // namespace pe
