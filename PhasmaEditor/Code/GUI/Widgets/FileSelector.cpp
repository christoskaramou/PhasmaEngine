#include "FileSelector.h"
#include "GUI/Helpers.h"
#include "imgui/imgui.h"

namespace pe
{
    FileSelector::FileSelector() : FileBrowser("Select File")
    {
        m_open = false;
    }

    void FileSelector::OpenSelection(FileSelectCallback callback, const std::vector<std::string> &allowedExtensions, const std::string &defaultPath, CancelCallback cancelCallback, const std::string &defaultName, const std::string &confirmLabel)
    {
        m_pickMode = PickMode::File;
        m_multiCallback = nullptr;
        m_folderCallback = nullptr;
        m_basket.clear();
        m_selectionCallback = callback;
        m_cancelCallback = cancelCallback;
        m_allowedExtensions = allowedExtensions;
        m_confirmLabel = confirmLabel.empty() ? "Select" : confirmLabel;

        if (!defaultPath.empty())
            NavigateTo(std::filesystem::path(defaultPath));

        memset(m_currentFile, 0, sizeof(m_currentFile));
        if (!defaultName.empty())
        {
#ifdef _WIN32
            strncpy_s(m_currentFile, defaultName.c_str(), sizeof(m_currentFile) - 1);
#else
            strncpy(m_currentFile, defaultName.c_str(), sizeof(m_currentFile) - 1);
            m_currentFile[sizeof(m_currentFile) - 1] = '\0';
#endif
            // Select only the stem (before the last dot)
            std::string_view sv(defaultName);
            auto dot = sv.rfind('.');
            m_selectStemLen = static_cast<int>(dot != std::string_view::npos ? dot : sv.size());
            m_focusOnce = true;
        }

        m_open = true;

        RefreshCache();

        ImGui::SetWindowFocus("Select File"); // The window name set in constructor
    }

    void FileSelector::OpenMultiSelection(MultiSelectCallback callback, const std::vector<std::string> &allowedExtensions, const std::string &defaultPath)
    {
        m_pickMode = PickMode::Files;
        m_selectionCallback = nullptr;
        m_folderCallback = nullptr;
        m_cancelCallback = nullptr;
        m_multiCallback = std::move(callback);
        m_allowedExtensions = allowedExtensions;
        m_confirmLabel = "Import";
        m_basket.clear();

        if (!defaultPath.empty())
            NavigateTo(std::filesystem::path(defaultPath));

        memset(m_currentFile, 0, sizeof(m_currentFile));
        m_focusOnce = false;
        m_open = true;
        RefreshCache();
        ImGui::SetWindowFocus("Select File");
    }

    void FileSelector::OpenFolderSelection(FolderSelectCallback callback, const std::string &defaultPath)
    {
        m_pickMode = PickMode::Folder;
        m_selectionCallback = nullptr;
        m_multiCallback = nullptr;
        m_cancelCallback = nullptr;
        m_folderCallback = std::move(callback);
        m_allowedExtensions.clear();
        m_confirmLabel = "Use This Folder";
        m_basket.clear();

        if (!defaultPath.empty())
            NavigateTo(std::filesystem::path(defaultPath));

        memset(m_currentFile, 0, sizeof(m_currentFile));
        m_focusOnce = false;
        m_open = true;
        RefreshCache();
        ImGui::SetWindowFocus("Select File");
    }

    void FileSelector::AddToBasket(const std::filesystem::path &path)
    {
        if (path.empty() || IsDirectory(path))
            return;
        for (const auto &p : m_basket)
            if (p == path)
                return; // already staged
        m_basket.push_back(path);
    }

    void FileSelector::CancelSelection()
    {
        if (m_cancelCallback)
        {
            m_cancelCallback();
            m_cancelCallback = nullptr;
        }
        m_selectionCallback = nullptr;
        m_multiCallback = nullptr;
        m_folderCallback = nullptr;
        m_allowedExtensions.clear();
        m_basket.clear();
        m_pickMode = PickMode::File;
        m_focusOnce = false;
        m_open = false;
    }

    void FileSelector::Update()
    {
        ProcessLoadedImages();

        if (!m_open)
            return;

        ui::SetInitialWindowSizeFraction(0.4f, 0.5f);
        ImGui::SetNextWindowFocus(); // Auto-focus when it opens or updates

        if (ImGui::Begin(m_name.c_str(), &m_open))
        {
            // If closed via X button logic in Begin
            if (!m_open)
            {
                CancelSelection();
                ImGui::End();
                return;
            }

            DrawNavBar(false);
            ImGui::Separator();

            const bool folderMode = (m_pickMode == PickMode::Folder);
            const bool filesMode = (m_pickMode == PickMode::Files);

            // --- Content interaction (double-click) ---
            auto onSelectAction = [this, folderMode, filesMode](const std::filesystem::path &path)
            {
                if (IsDirectory(path))
                {
                    NavigateTo(path);
                    return;
                }
                if (folderMode)
                    return; // files aren't pickable when choosing a folder

                if (filesMode)
                {
                    AddToBasket(path); // stage the file; don't close
                    return;
                }

                // Single-file mode: double-click confirms.
                bool shouldClose = true;
                if (m_selectionCallback)
                {
                    auto pathU8 = path.u8string();
                    std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                    shouldClose = m_selectionCallback(pathStr);
                }
                if (shouldClose)
                    CancelSelection();
            };

            auto filterAction = [this, folderMode](const std::filesystem::path &path) -> bool
            {
                if (IsDirectory(path))
                    return true;
                if (folderMode)
                    return false; // only directories are relevant when picking a folder
                if (m_allowedExtensions.empty())
                    return true;

                auto extU8 = path.extension().u8string();
                std::string ext(reinterpret_cast<const char *>(extU8.c_str()));
                for (const auto &ae : m_allowedExtensions)
                    if (ae == ext || ae == "*")
                        return true;

                return false;
            };

            // Reserve footer space; multi-select needs room for the staged-files basket.
            float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
            if (filesMode)
                footerHeight += ImGui::GetTextLineHeightWithSpacing() * 5.0f;
            else if (folderMode)
                footerHeight += ImGui::GetTextLineHeightWithSpacing();

            DrawDirectoryContent(m_currentPath, onSelectAction, filterAction, footerHeight);

            // Mirror single-click selection into the filename box (single-file mode only).
            if (m_pickMode == PickMode::File && m_selectedEntry != m_prevSelectedEntry)
            {
                m_prevSelectedEntry = m_selectedEntry;
                if (!IsDirectory(m_selectedEntry))
                {
                    auto filename = m_selectedEntry.filename().u8string();
                    if (filename.length() < sizeof(m_currentFile))
                    {
#ifdef _WIN32
                        strncpy_s(m_currentFile, reinterpret_cast<const char *>(filename.c_str()), sizeof(m_currentFile) - 1);
#else
                        strncpy(m_currentFile, reinterpret_cast<const char *>(filename.c_str()), sizeof(m_currentFile) - 1);
                        m_currentFile[sizeof(m_currentFile) - 1] = '\0';
#endif
                    }
                }
            }

            ImGui::Separator();

            if (filesMode)
            {
                // --- Multi-file basket footer ---
                ImGui::Text("Staged: %d file(s)", static_cast<int>(m_basket.size()));
                if (ImGui::BeginChild("##basket", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 3.0f), true))
                {
                    int removeIdx = -1;
                    for (int i = 0; i < static_cast<int>(m_basket.size()); ++i)
                    {
                        ImGui::PushID(i);
                        if (ImGui::SmallButton("x"))
                            removeIdx = i;
                        ui::ItemTooltip("Remove this file from the staged import list.");
                        ImGui::SameLine();
                        auto u8 = m_basket[i].filename().u8string();
                        ImGui::TextUnformatted(reinterpret_cast<const char *>(u8.c_str()));
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0)
                        m_basket.erase(m_basket.begin() + removeIdx);
                }
                ImGui::EndChild();

                const bool canAdd = !m_selectedEntry.empty() && !IsDirectory(m_selectedEntry);
                if (!canAdd)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Add Selected"))
                    AddToBasket(m_selectedEntry);
                ui::ItemTooltip("Add the selected file to the staged import list.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
                if (!canAdd)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    CancelSelection();
                ui::ItemTooltip("Cancel file selection.");
                ImGui::SameLine();
                const bool canImport = !m_basket.empty();
                if (!canImport)
                    ImGui::BeginDisabled();
                if (ImGui::Button(m_confirmLabel.c_str()))
                {
                    std::vector<std::string> out;
                    out.reserve(m_basket.size());
                    for (const auto &p : m_basket)
                    {
                        auto u8 = p.u8string();
                        out.emplace_back(reinterpret_cast<const char *>(u8.c_str()));
                    }
                    auto cb = m_multiCallback;
                    CancelSelection();
                    if (cb)
                        cb(out);
                }
                ui::ItemTooltip("Confirm the staged file list.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
                if (!canImport)
                    ImGui::EndDisabled();
            }
            else if (folderMode)
            {
                // --- Folder pick footer ---
                auto cu8 = m_currentPath.u8string();
                ImGui::Text("Folder: %s", reinterpret_cast<const char *>(cu8.c_str()));
                if (ImGui::Button("Cancel"))
                    CancelSelection();
                ui::ItemTooltip("Cancel folder selection.");
                ImGui::SameLine();
                if (ImGui::Button(m_confirmLabel.c_str()))
                {
                    auto u8 = m_currentPath.u8string();
                    std::string pathStr(reinterpret_cast<const char *>(u8.c_str()));
                    auto cb = m_folderCallback;
                    CancelSelection();
                    if (cb)
                        cb(pathStr);
                }
                ui::ItemTooltip("Use the currently open folder.");
            }
            else
            {
                // --- Single-file footer (default) ---
                if (m_focusOnce)
                {
                    ImGui::SetKeyboardFocusHere();
                    m_focusOnce = false;
                }

                auto stemSelectCb = [](ImGuiInputTextCallbackData *d) -> int
                {
                    auto *self = static_cast<FileSelector *>(d->UserData);
                    if (self->m_selectStemLen >= 0)
                    {
                        d->SelectionStart = 0;
                        d->SelectionEnd = self->m_selectStemLen;
                        self->m_selectStemLen = -1;
                    }
                    return 0;
                };
                bool confirm = ImGui::InputText("##filename", m_currentFile, sizeof(m_currentFile),
                                                ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_EnterReturnsTrue,
                                                stemSelectCb, this);
                ui::ItemTooltip("Selected filename or folder name to open.");
                ImGui::SameLine();

                if (ImGui::Button("Cancel"))
                    CancelSelection();
                ui::ItemTooltip("Cancel file selection.");
                ImGui::SameLine();
                const bool confirmClicked = ImGui::Button(m_confirmLabel.c_str());
                ui::ItemTooltip("Confirm the selected file.");
                confirm |= confirmClicked;
                if (confirm)
                {
                    std::string fileStr = m_currentFile;
                    if (!fileStr.empty())
                    {
                        std::filesystem::path selectedPath = m_currentPath / fileStr;
                        if (IsDirectory(selectedPath))
                        {
                            NavigateTo(selectedPath);
                            memset(m_currentFile, 0, sizeof(m_currentFile));
                        }
                        else
                        {
                            bool shouldClose = true;
                            if (m_selectionCallback)
                            {
                                auto pathU8 = selectedPath.u8string();
                                std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                                shouldClose = m_selectionCallback(pathStr);
                            }
                            if (shouldClose)
                                CancelSelection();
                        }
                    }
                }
            }
        }
        ImGui::End();

        if (!m_open)
            CancelSelection();
    }

} // namespace pe
