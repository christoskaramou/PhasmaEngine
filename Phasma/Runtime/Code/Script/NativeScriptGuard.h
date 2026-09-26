#pragma once
#include "ProjectNative.h"

namespace pe
{
    // Calls into game code. Where hardware faults can be contained (MSVC SEH), a fault returns 0 and
    // stores its exception code in *fault; the callee's state may be corrupt and must be leaked.
    // Elsewhere these are plain calls and *fault stays 0.
    bool NativeFaultsContained();
    uint32_t GuardedCreate(decltype(phasma::ScriptDesc::create) create, const phasma::ScriptApi *api, phasma::Node node, void **state, uint32_t *fault);
    uint32_t GuardedUpdate(decltype(phasma::ScriptDesc::update) update, void *state, double dt, uint32_t *fault);
    void GuardedDestroy(decltype(phasma::ScriptDesc::destroy) destroy, void *state, uint32_t *fault);
} // namespace pe
