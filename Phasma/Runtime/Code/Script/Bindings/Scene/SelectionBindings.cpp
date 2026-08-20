#include "Script/ScriptSystem.h"
#include "Scene/SceneRuntimeHooks.h"

namespace pe
{
    // Player-side feed for the selection-outline pass. The editor registers a full
    // SelectionManager-backed `selection` table; these two functions exist so PhasmaPlayer
    // scripts can outline a node with the same calls.
    //
    // Only-if-absent keeps every registration order safe: the editor always overwrites with
    // set_function, so wherever its bindings are present they win, and these survive only in
    // hosts that never register the editor table. API stays minimal on purpose — select_node
    // and clear are the whole runtime contract; everything else `selection` offers is editor
    // semantics (gizmos, focus, multi-select) that a shipped game has no business exposing.
    static struct RuntimeSelectionBindings
    {
        RuntimeSelectionBindings()
        {
            ScriptSystem::AddBindings(
                [](sol::state &lua)
                {
                    sol::table selection = lua["selection"].get_or_create<sol::table>();
                    if (!selection["select_node"].valid())
                        selection.set_function("select_node",
                                               [](int nodeIndex)
                                               { SetRuntimeSelectedNodeIndex(nodeIndex); });
                    if (!selection["clear"].valid())
                        selection.set_function("clear", []()
                                               { SetRuntimeSelectedNodeIndex(-1); });
                });
        }
    } s_runtimeSelectionBindings;
} // namespace pe
