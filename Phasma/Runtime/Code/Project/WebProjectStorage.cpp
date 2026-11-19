#include "Project/WebProjectStorage.h"

#if defined(PE_WEB)
#include <emscripten.h>

// /WebEdits is an IndexedDB-backed folder (IDBFS, linked with -lidbfs.js) holding saved project files at their absolute
// paths, e.g. /WebEdits/Project/Assets/Scenes/a.pescene. The bodies are JavaScript; clang-format would split their =>
// into "= >".
// clang-format off
EM_JS_DEPS(PeWebProjectStorage, "$IDBFS"); // -lidbfs.js only makes it available; this keeps it in the link

EM_JS(void, PeWebMountEdits, (), {
    FS.mkdirTree("/WebEdits");
    FS.mount(IDBFS, {}, "/WebEdits");
});

// load 1 fills /WebEdits from IndexedDB, 0 writes /WebEdits to IndexedDB. Suspends through JSPI until it finishes.
EM_ASYNC_JS(int, PeWebSyncEdits, (int load), {
    return await new Promise((resolve) => FS.syncfs(!!load, (error) => {
        if (error)
            console.error("[WebStorage] IndexedDB sync failed:", error);
        resolve(error ? 0 : 1);
    }));
});
// clang-format on
#endif

namespace pe
{
#if defined(PE_WEB)
    namespace
    {
        enum class WebStorageState
        {
            Off, // never restored (desktop, the player): nothing to persist
            Ready,
            Failed // IndexedDB could not be read: a sync now would delete the files saved there before
        };

        using WriteTimes = std::unordered_map<std::string, std::filesystem::file_time_type>;

        const std::filesystem::path kEditsRoot = "/WebEdits";
        constexpr auto kOverwrite = std::filesystem::copy_options::overwrite_existing;

        WebStorageState s_state = WebStorageState::Off;
        std::filesystem::path s_projectRoot;
        // Each project file's write time when it was last restored or persisted; any other time is an unsaved change.
        WriteTimes s_writeTimes;

        std::filesystem::path MirrorPath(const std::filesystem::path &file)
        {
            return kEditsRoot / file.relative_path();
        }

        size_t CopyMirrorOverProject()
        {
            std::error_code ec;
            const std::filesystem::path mirror = MirrorPath(s_projectRoot);
            if (!std::filesystem::is_directory(mirror, ec))
                return 0;

            size_t restored = 0;
            for (const auto &entry : std::filesystem::recursive_directory_iterator(mirror, ec))
            {
                if (!entry.is_regular_file(ec))
                    continue;

                const std::filesystem::path target = s_projectRoot / entry.path().lexically_relative(mirror);
                std::filesystem::create_directories(target.parent_path(), ec);
                if (!std::filesystem::copy_file(entry.path(), target, kOverwrite, ec))
                {
                    Log::Warn("[WebStorage] could not restore " + target.string() + ": " + ec.message());
                    continue;
                }

                restored++;
            }

            return restored;
        }

        WriteTimes ReadWriteTimes()
        {
            WriteTimes times;
            std::error_code ec;
            for (const auto &entry : std::filesystem::recursive_directory_iterator(s_projectRoot, ec))
            {
                if (entry.is_regular_file(ec))
                    times[entry.path().string()] = entry.last_write_time(ec);
            }

            return times;
        }

        // Copies each project file whose write time changed into the mirror. False on the first file it cannot copy.
        bool CopyChangedFilesToMirror(const WriteTimes &current, WriteTimes &changed)
        {
            std::error_code ec;
            for (const auto &[path, writeTime] : current)
            {
                const auto known = s_writeTimes.find(path);
                if (known != s_writeTimes.end() && known->second == writeTime)
                    continue;

                const std::filesystem::path mirror = MirrorPath(path);
                std::filesystem::create_directories(mirror.parent_path(), ec);
                if (!std::filesystem::copy_file(path, mirror, kOverwrite, ec))
                {
                    Log::Error("[WebStorage] could not copy " + path + " to browser storage: " + ec.message());
                    return false;
                }

                changed[path] = writeTime;
            }

            return true;
        }
    } // namespace

    void RestoreWebProjectEdits(const std::filesystem::path &projectRoot)
    {
        if (projectRoot.empty())
            return;

        s_projectRoot = projectRoot;
        PeWebMountEdits();
        if (!PeWebSyncEdits(1))
        {
            s_state = WebStorageState::Failed;
            Log::Error("[WebStorage] browser storage could not be read: saves stay in memory and are lost on reload");
            return;
        }

        const size_t restored = CopyMirrorOverProject();
        s_writeTimes = ReadWriteTimes();
        s_state = WebStorageState::Ready;
        Log::Info("[WebStorage] restored " + std::to_string(restored) + " saved project files from browser storage");
    }

    bool PersistWebProjectEdits()
    {
        if (s_state == WebStorageState::Off)
            return true;

        if (s_state == WebStorageState::Failed)
        {
            Log::Error("[WebStorage] browser storage could not be read at startup, so nothing is saved there");
            return false;
        }

        // ponytail: deleted or renamed project files are not mirrored, so the preloaded copy returns after a reload.
        // The upgrade is to record removals in the mirror as well.
        WriteTimes changed;
        if (!CopyChangedFilesToMirror(ReadWriteTimes(), changed))
            return false;

        if (!PeWebSyncEdits(0))
        {
            Log::Error("[WebStorage] browser storage did not accept the save");
            return false;
        }

        for (const auto &[path, writeTime] : changed)
            s_writeTimes[path] = writeTime;

        Log::Info("[WebStorage] " + std::to_string(changed.size()) + " changed project files saved to browser storage");
        return true;
    }
#else
    void RestoreWebProjectEdits(const std::filesystem::path &)
    {
    }

    bool PersistWebProjectEdits()
    {
        return true;
    }
#endif
} // namespace pe
