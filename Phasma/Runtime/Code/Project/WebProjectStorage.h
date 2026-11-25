#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace pe
{
    // Browser project saves (web editor): the project is a preloaded in-memory snapshot, so saved files are mirrored
    // into IndexedDB and copied back over the snapshot on the next page load. Other hosts use their normal filesystem.

    // Loads the saved files from browser storage over the project at projectRoot. Call once, before the project's
    // files are read.
    void RestoreWebProjectEdits(const std::filesystem::path &projectRoot, const std::string &projectName);

    // Desktop permits normal filesystem editing; browser mutations must stay inside the selected project.
    bool CanWriteWebProjectPath(const std::filesystem::path &path);

    // Run a filesystem edit. In the browser editor its destinations (including directory subtrees)
    // are one IndexedDB transaction; failure restores their pre-edit contents in MEMFS.
    bool CommitProjectEdit(const std::vector<std::filesystem::path> &paths, const std::function<bool()> &edit);
} // namespace pe
