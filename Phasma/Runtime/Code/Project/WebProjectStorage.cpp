#include "Project/WebProjectStorage.h"

#if defined(PE_WEB)
#include <emscripten.h>

// Each committed key is a relative path. null hides a bundled entry, true creates a directory, bytes replace a file.
// clang-format off
EM_ASYNC_JS(void, PeRestoreProject, (const char *rootPtr, const char *namePtr), {
    const root = UTF8ToString(rootPtr);
    const state = Module.peProjectStorage = {root, ready: false};
    const request = (req) => new Promise((resolve, reject) => {
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    });
    state.snapshot = (roots, readBytes) => {
        const entries = new Map();
        const walk = (path) => {
            if (entries.has(path) || !FS.analyzePath(root + path, true).exists) return;
            const stat = FS.lstat(root + path);
            if (FS.isDir(stat.mode)) {
                entries.set(path, true);
                for (const child of FS.readdir(root + path)) {
                    if (child !== "." && child !== "..") walk(path + "/" + child);
                }
            } else if (FS.isFile(stat.mode)) {
                entries.set(path, readBytes ? FS.readFile(root + path) : false);
            } else {
                throw new Error("Unsupported project entry: " + path);
            }
        };
        for (const path of roots) walk(path);
        return entries;
    };
    try {
        const open = indexedDB.open("PhasmaEditor:" + JSON.stringify([location.pathname, UTF8ToString(namePtr)]), 1);
        open.onupgradeneeded = () => open.result.createObjectStore("edits");
        state.db = await request(open);
        const edits = state.db.transaction("edits").objectStore("edits");
        const [paths, values] = await Promise.all([request(edits.getAllKeys()), request(edits.getAll())]);
        // IDBFS had one database per origin, with no project identity. Recover only into an empty namespace,
        // at the user's request. Leave the original intact; Save commits the recovered files to this project.
        const recover = !paths.length && new URLSearchParams(location.search).get("recover-legacy") === "1";
        if (recover) {
            const legacy = indexedDB.open("/WebEdits");
            legacy.onupgradeneeded = () => legacy.transaction.abort(); // no old database: don't create one
            const db = await request(legacy).catch(() => null);
            if (db) {
                try {
                    const store = db.transaction("FILE_DATA").objectStore("FILE_DATA");
                    const prefix = "/WebEdits" + root + "/";
                    const range = IDBKeyRange.bound(prefix, prefix + "\uffff");
                    const [keys, files] = await Promise.all([request(store.getAllKeys(range)), request(store.getAll(range))]);
                    for (let i = 0; i < keys.length; ++i) {
                        if (FS.isFile(files[i].mode)) {
                            paths.push(keys[i].slice(prefix.length - 1));
                            values.push(files[i].contents);
                        }
                    }
                } finally {
                    db.close();
                }
            }
        } else if (!paths.length && indexedDB.databases) {
            indexedDB.databases().then((dbs) => {
                if (dbs.some((db) => db.name === "/WebEdits"))
                    console.warn("[WebStorage] Old saves have no project identity. To recover them here, open this page with ?recover-legacy=1, inspect the scene, then Save.");
            }).catch(() => {});
        }
        const records = paths.map((key, i) => {
            if (typeof key !== "string" || !key.startsWith("/") ||
                key.slice(1).split("/").some((p) => !p || p === "." || p === ".."))
                throw new Error("Invalid project edit path");
            return [root + key, values[i]];
        });
        // IndexedDB sorts string keys: remove old children first, then create parents before children.
        for (const [path, value] of records.slice().reverse()) {
            if (!FS.analyzePath(path).exists) continue;
            if (FS.isDir(FS.lstat(path).mode)) {
                if (value === null && FS.readdir(path).length > 2) continue;
                if (value !== true) FS.rmdir(path);
            } else if (value === null || value === true) {
                FS.unlink(path);
            }
        }
        let restored = 0;
        for (const [path, value] of records) {
            if (value === null) continue;
            if (value === true) FS.mkdirTree(path);
            else {
                FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
                FS.writeFile(path, value);
                restored++;
            }
        }
        // Adopt explicitly recovered bytes on the first successful edit, never later uncommitted MEMFS changes.
        state.recovered = new Map(recover ? paths.map((path, i) => [path, values[i]]) : []);
        state.ready = true;
        console.info("[WebStorage] restored " + restored + " saved project files from browser storage");
    } catch (error) {
        console.error("[WebStorage] browser storage could not be read: saves stay in memory", error);
    }
});

EM_JS(int, PeBeginProjectEdit, (const char *const *paths, int count), {
    const state = Module.peProjectStorage;
    if (!state || !state.ready || state.edit) return 0;
    try {
        const roots = new Set();
        for (let i = 0; i < count; i++) {
            const absolute = UTF8ToString(HEAPU32[(paths >>> 2) + i]);
            if (!absolute.startsWith(state.root + "/")) throw new Error("Not a project path: " + absolute);
            let path = absolute.slice(state.root.length);
            // A writer may need mkdirTree: include only ancestors that this operation will create.
            for (let slash = path.lastIndexOf("/"); slash > 0; slash = path.lastIndexOf("/")) {
                const parent = path.slice(0, slash);
                if (FS.analyzePath(state.root + parent).exists) break;
                path = parent;
            }
            roots.add(path);
        }
        state.edit = {roots, before: state.snapshot(roots, true)};
        return 1;
    } catch (error) {
        console.error("[WebStorage] cannot start project edit", error);
        return 0;
    }
});

EM_ASYNC_JS(int, PeFinishProjectEdit, (int written), {
    const state = Module.peProjectStorage;
    const {roots, before} = state.edit;
    try {
        if (!written) throw new Error("Filesystem edit failed");
        const current = state.snapshot(roots, true);
        const changes = new Map(state.recovered);
        for (const path of before.keys())
            if (!current.has(path)) changes.set(path, null);
        for (const [path, value] of current) changes.set(path, value);
        await new Promise((resolve, reject) => {
            const tx = state.db.transaction("edits", "readwrite");
            tx.oncomplete = resolve;
            tx.onabort = () => reject(tx.error || new Error("IndexedDB transaction aborted"));
            try {
                const edits = tx.objectStore("edits");
                for (const [path, value] of changes) edits.put(value, path);
            } catch (error) {
                tx.abort();
                reject(error);
            }
        });
        state.recovered.clear();
        console.info("[WebStorage] " + changes.size + " changed project files saved to browser storage");
        return 1;
    } catch (error) {
        console.error("[WebStorage] browser storage did not accept the save", error);
        const current = state.snapshot(roots, false);
        for (const path of Array.from(current.keys()).sort().reverse()) {
            const was = before.get(path);
            if (was !== undefined && (was === true) === current.get(path)) continue;
            if (current.get(path)) FS.rmdir(state.root + path);
            else FS.unlink(state.root + path);
        }
        for (const [path, value] of before) {
            if (value === true) FS.mkdirTree(state.root + path);
        }
        for (const [path, value] of before) {
            if (value !== true) FS.writeFile(state.root + path, value);
        }
        return 0;
    } finally {
        state.edit = null;
    }
});
// clang-format on
#endif

namespace pe
{
#if defined(PE_WEB)
    namespace
    {
        std::filesystem::path s_projectRoot;
    }
#endif

    bool CanWriteWebProjectPath(const std::filesystem::path &path)
    {
#if defined(PE_WEB)
        if (s_projectRoot.empty()) // No editor storage in the standalone player.
            return true;

        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (ec)
            return false;

        // rename/unlink operate on a link, whereas a file stream follows it. Never journal a different entry.
        if (resolved != std::filesystem::absolute(path, ec).lexically_normal() || ec)
            return false;

        const auto relative = resolved.lexically_relative(s_projectRoot);
        return !relative.empty() && relative != "." && *relative.begin() != "..";
#else
        return true;
#endif
    }

    void RestoreWebProjectEdits(const std::filesystem::path &projectRoot, const std::string &projectName)
    {
#if defined(PE_WEB)
        if (!projectRoot.empty())
        {
            s_projectRoot = std::filesystem::weakly_canonical(projectRoot);
            PeRestoreProject(s_projectRoot.c_str(), projectName.c_str());
        }
#endif
    }

    bool CommitProjectEdit(const std::vector<std::filesystem::path> &paths, const std::function<bool()> &edit)
    {
#if defined(PE_WEB)
        if (s_projectRoot.empty())
            return edit();

        std::vector<std::filesystem::path> resolved;
        for (const auto &path : paths)
        {
            if (!CanWriteWebProjectPath(path))
                return false;

            resolved.push_back(std::filesystem::absolute(path).lexically_normal());
        }

        std::vector<const char *> names;
        for (const auto &path : resolved)
            names.push_back(path.c_str());

        if (names.empty() || !PeBeginProjectEdit(names.data(), static_cast<int>(names.size())))
            return false;

        bool written = false;
        try
        {
            written = edit();
        }
        catch (const std::exception &error)
        {
            emscripten_log(EM_LOG_ERROR, "[WebStorage] Filesystem edit failed: %s", error.what());
        }
        return PeFinishProjectEdit(written) != 0;
#else
        return edit();
#endif
    }
} // namespace pe
