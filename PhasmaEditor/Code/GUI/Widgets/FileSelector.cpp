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

            DrawDirectoryContent(m_currentPath, onSelectAction, filterAction);

            ImGui::Separator();
            if (ImGui::Button("Cancel"))
                CancelSelection();
            ImGui::SameLine();
            if (ImGui::Button("Select"))
            {
                if (!IsDirectory(m_selectedEntry) && std::filesystem::exists(m_selectedEntry))
                {
                    if (m_selectionCallback)
                    {
                        auto pathU8 = m_selectedEntry.u8string();
                        std::string pathStr(reinterpret_cast<const char *>(pathU8.c_str()));
                        m_selectionCallback(pathStr);
                    }
                    CancelSelection();
                }
            }
        }
        ImGui::End();

        // Handle case where End() was reached but m_open might have been toggled internally if we didn't return early
        if (!m_open)
            CancelSelection();
    }

} // namespace pe
