#include "ProjectNative.h"
#include <stdexcept>

namespace
{
    struct Probe
    {
        const phasma::ScriptApi &api;
        phasma::Node node;
        Probe(const phasma::ScriptApi &api, phasma::Node node) : api(api), node(node)
        {
            if (PROBE_VARIANT == 6)
                throw std::runtime_error("create");
            if (!node)
                this->node = api.findNode ? api.findNode(api.context, "NativeReloadProbe") : 1;
            api.log(api.context, "create");
        }
        ~Probe() { api.log(api.context, "destroy"); }
        void Update(double dt)
        {
            if (PROBE_VARIANT == 5)
                throw std::runtime_error("update");
            // y = seconds since this instance was created, so a smoke test can see state resets.
            elapsed += dt;
            api.setPosition(api.context, node, {static_cast<float>(PROBE_VARIANT), static_cast<float>(elapsed), 0});
        }
        double elapsed = 0;
    };
    const phasma::ScriptDesc scripts[] = {
        phasma::Script<Probe>("Probe", phasma::ScriptKind::Global, phasma::ScriptMode::Always),
        phasma::Script<Probe>(PROBE_VARIANT == 4 ? "Probe" : "NodeProbe", phasma::ScriptKind::Node)};
    const phasma::ScriptModule module{
        PROBE_VARIANT == 3 ? 999u : phasma::ScriptAbiVersion,
        sizeof(phasma::ScriptModule), 2, scripts};
} // namespace

PHASMA_SCRIPT_EXPORT const phasma::ScriptModule *PhasmaGetScriptModule(uint32_t) noexcept
{
    return &module;
}
