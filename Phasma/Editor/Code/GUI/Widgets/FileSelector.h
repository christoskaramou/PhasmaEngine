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

        // Multi-file pick: navigate freely, add files to a basket, confirm returns all of them at once.
        using MultiSelectCallback = std::function<void(const std::vector<std::string> &)>;
        void OpenMultiSelection(MultiSelectCallback callback, const std::vector<std::string> &allowedExtensions = {}, const std::string &defaultPath = "");

        // Folder pick: navigate into the target directory, confirm returns that directory's path.
        using FolderSelectCallback = std::function<void(const std::string &)>;
        void OpenFolderSelection(FolderSelectCallback callback, const std::string &defaultPath = "");

        void CancelSelection();

    private:
        enum class PickMode
        {
            File,   // single file (default)
            Files,  // multiple files (basket)
            Folder, // a directory
        };

        void AddToBasket(const std::filesystem::path &path);

        char m_currentFile[1024]{};
        std::filesystem::path m_prevSelectedEntry;
        FileSelectCallback m_selectionCallback;
        MultiSelectCallback m_multiCallback;
        FolderSelectCallback m_folderCallback;
        CancelCallback m_cancelCallback;
        std::vector<std::string> m_allowedExtensions;
        std::vector<std::filesystem::path> m_basket; // PickMode::Files staging list
        PickMode m_pickMode = PickMode::File;
        bool m_focusOnce = false;
        int m_selectStemLen = -1; // select [0, stemLen) on first InputText callback
        std::string m_confirmLabel = "Select";
    };
} // namespace pe
