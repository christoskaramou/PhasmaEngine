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

    void FileSelector::CancelSelection()
    {
        if (m_cancelCallback)
        {
            m_cancelCallback();
            m_cancelCallback = nullptr;
        }
        m_selectionCallback = nullptr;
        m_allowedExtensions.clear();
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

            // --- Content ---
            // Callback for Selection Mode: Double click confirms selection
            auto onSelectAction = [this](const std::filesystem::path &path)
            {
                if (std::filesystem::is_directory(path))
                {
                    NavigateTo(path);
                }
                else
                {
                    bool shouldClose = true;
                    if (m_selectionCallback)
                    {
                        auto pathU8 = path.u8string();
                        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                        shouldClose = m_selectionCallback(pathStr);
                    }
                    if (shouldClose)
                        CancelSelection();
                }
            };

            auto filterAction = [this](const std::filesystem::path &path) -> bool
            {
                if (std::filesystem::is_directory(path))
                    return true;
                if (m_allowedExtensions.empty())
                    return true;

                auto extU8 = path.extension().u8string();
                std::string ext(reinterpret_cast<const char *>(extU8.c_str()));
                for (const auto &ae : m_allowedExtensions)
                    if (ae == ext || ae == "*")
                        return true;

                return false;
            };

            float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
            DrawDirectoryContent(m_currentPath, onSelectAction, filterAction, footerHeight);

            if (m_selectedEntry != m_prevSelectedEntry)
            {
                m_prevSelectedEntry = m_selectedEntry;
                if (!std::filesystem::is_directory(m_selectedEntry))
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
            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                CancelSelection();
            ImGui::SameLine();
            confirm |= ImGui::Button(m_confirmLabel.c_str());
            if (confirm)
            {
                std::string fileStr = m_currentFile;
                if (!fileStr.empty())
                {
                    std::filesystem::path selectedPath = m_currentPath / fileStr;
                    if (std::filesystem::is_directory(selectedPath))
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
        ImGui::End();

        if (!m_open)
            CancelSelection();
    }

} // namespace pe
