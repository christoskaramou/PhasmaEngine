-- play_controls.lua
-- Previously: Escape exited play mode, P toggled engine pause.
-- Games (AgainstTheHero, …) own pause/menu via their own UI (gear hub).
-- Editor stop/pause live on the toolbar — keep this file as a no-op stub so
-- global Always scripts still resolve.

hooks {
    update = function() end,
    update_editor = function() end,
}
