#pragma once

#include <string>
#include <vector>

namespace pe
{
    // Scene-owned script references. Persisted as a top-level "scene_scripts" object in
    // the .pescene and applied on load. This is the scene-scoped counterpart to the
    // directory-scanned Scripts/Player auto-load: it lets one scene declare exactly which
    // gameplay scripts run when it plays and which named action entry points it exposes,
    // without relying on file placement or a boot-script dispatcher.
    //
    // MVP buckets (see docs/wiki/architecture/runtime.md "Scene Scripts"):
    //   onPlay  — scripts run with PlayerOnly lifecycle when the scene enters play.
    //   actions — named id -> {script, function} entry points dispatched on demand
    //             (Lua scene.run_action("id"), UI buttons, triggers). Zero lifecycle:
    //             the action's script is loaded lazily the first time it is invoked.
    //
    // Deliberately omitted for now (deferred — they introduce a non-play lifecycle that
    // the snapshot/restore machinery does not yet model): compose / on_load.
    struct SceneScriptAction
    {
        std::string id;       // unique key referenced by UI / triggers / scene.run_action
        std::string script;   // project-relative path to the .lua file
        std::string function; // function name to call inside that script's environment
    };

    struct SceneScriptManifest
    {
        std::vector<std::string> onPlay;
        std::vector<SceneScriptAction> actions;

        bool Empty() const { return onPlay.empty() && actions.empty(); }
        void Clear()
        {
            onPlay.clear();
            actions.clear();
        }

        const SceneScriptAction *FindAction(const std::string &id) const
        {
            for (const auto &a : actions)
                if (a.id == id)
                    return &a;
            return nullptr;
        }
    };
} // namespace pe
