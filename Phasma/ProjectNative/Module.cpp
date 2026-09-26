#include "ScriptModule.h"

PHASMA_SCRIPT_EXPORT const phasma::ScriptModule *PhasmaGetScriptModule(uint32_t version) noexcept
{
    const auto &scripts = phasma::RegisteredScripts();
    static const phasma::ScriptModule module{phasma::ScriptAbiVersion, sizeof(phasma::ScriptModule),
                                             static_cast<uint32_t>(scripts.size()), scripts.data()};
    return version >= phasma::ScriptAbiVersion ? &module : nullptr;
}
