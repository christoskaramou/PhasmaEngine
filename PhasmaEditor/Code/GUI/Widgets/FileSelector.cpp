#include "FileSelector.h"
#include "GUI/Helpers.h"
#include "imgui/imgui.h"

namespace pe
{
    FileSelector::FileSelector() : FileBrowser("Select File")
    {
        m_open = false;
    }

    void FileSelector::OpenSelection(FileSelectCallback callback, const std::vector<std::string> &allowedExtensions)
    {
        m_selectionCallback = callback;
        m_allowedExtensions = allowedExtensions;

        m_open = true;

        ImGui::SetWindowFocus("Select File"); // The window name set in constructor
    }

    void FileSelector::CancelSelection()
    {
        m_selectionCallback = nullptr;
        m_allowedExtensions.clear();
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

            // --- Top Bar ---
            if (ImGui::Button("..") && m_currentPath.has_parent_path())
            {
                auto parentU8 = m_currentPath.parent_path().u8string();
                std::string parent(reinterpret_cast<const char *>(parentU8.c_str()));
                if (parent.find("PhasmaEngine") != std::string::npos || parent.find("PhasmaEditor") != std::string::npos)
                    m_currentPath = m_currentPath.parent_path();
            }
            ImGui::SameLine();
            auto currentPathU8 = m_currentPath.u8string();
            ImGui::Text("%s", reinterpret_cast<const char *>(currentPathU8.c_str()));
            ImGui::Separator();

            // --- Content ---
            // Callback for Selection Mode: Double click confirms selection
            auto onSelectAction = [this](const std::filesystem::path &path)
            {
                if (std::filesystem::is_directory(path))
                {
                    m_currentPath = path;
                }
                else
                {
                    if (m_selectionCallback)
                    {
                        auto pathU8 = path.u8string();
                        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                        m_selectionCallback(pathStr);
                    }
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
                        strcpy_s(m_currentFile, reinterpret_cast<const char *>(filename.c_str()));
#else
                        strcpy(m_currentFile, reinterpret_cast<const char *>(filename.c_str()));
#endif
                    }
                }
            }

            ImGui::Separator();

            ImGui::InputText("##filename", m_currentFile, sizeof(m_currentFile));
            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                CancelSelection();
            ImGui::SameLine();
            if (ImGui::Button("Select"))
            {
                std::string fileStr = m_currentFile;
                if (!fileStr.empty())
                {
                    std::filesystem::path selectedPath = m_currentPath / fileStr;
                    if (std::filesystem::is_directory(selectedPath))
                    {
                        m_currentPath = selectedPath;
                        memset(m_currentFile, 0, sizeof(m_currentFile));
                    }
                    else
                    {
                        if (m_selectionCallback)
                        {
                            auto pathU8 = selectedPath.u8string();
                            std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                            m_selectionCallback(pathStr);
                        }
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
