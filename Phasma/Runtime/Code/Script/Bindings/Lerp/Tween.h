#pragma once

#include <functional>

namespace pe
{
    // Time-based vec3 tween system, shared by Lua scripts (the `tween` table) and
    // native editor code (camera focus). All active tweens advance once per frame
    // from ScriptSystem::Update via TickTweens. `vec3` comes from the PCH, as in Camera.h.

    void TickTweens(double dt);

    // Tween `from`->`to` over `duration` seconds (smoothstep when `smooth`, else linear).
    // `setter` receives the interpolated vec3 each frame and returns false to drop the
    // tween early (e.g. its target was destroyed). Returns an id for TweenCancel, or 0.
    int TweenStart(const vec3 &from, const vec3 &to, float duration, bool smooth,
                   std::function<bool(const vec3 &)> setter,
                   std::function<void()> onDone = {});

    void TweenCancel(int id);

    // Glide the active scene camera to `target`, replacing any in-flight camera glide.
    // Shared by every editor focus trigger (F key, Hierarchy double-click / Focus menu).
    void TweenCameraTo(const vec3 &target, float duration);
} // namespace pe
