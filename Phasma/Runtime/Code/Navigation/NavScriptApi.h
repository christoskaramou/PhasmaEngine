#pragma once

#include "ProjectNative.h"

namespace pe::navscript
{
    // The script ABI's navigation (ScriptApi v27 nav*): meshes and the swarms on them, by handle. The
    // engine's CppScript hands them to native game code; a game's headless check compiles this file with the
    // Navigation sources (Navigation/NavStandalone.h) and fills its own ScriptApi the same way.
    // ponytail: process-wide and main-thread only, as the ABI; per-host worlds if two hosts ever share a process.
    void Fill(phasma::ScriptApi &api);
    void DestroyAll();
} // namespace pe::navscript
