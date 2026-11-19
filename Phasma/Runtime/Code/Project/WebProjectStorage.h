#pragma once

namespace pe
{
    // Browser project saves (web editor): the project is a preloaded in-memory snapshot, so saved files are mirrored
    // into IndexedDB and copied back over the snapshot on the next page load. Elsewhere both are no-ops.

    // Loads the saved files from browser storage over the project at projectRoot. Call once, before the project's
    // files are read.
    void RestoreWebProjectEdits(const std::filesystem::path &projectRoot);

    // Writes every project file changed since the restore or the previous call to browser storage and waits for it.
    // False when it did not reach storage: the save must not count as done. True when there is nothing to persist
    // (desktop, or a page that never restored).
    bool PersistWebProjectEdits();
} // namespace pe
