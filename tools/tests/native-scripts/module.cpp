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
            if (PROBE_VARIANT == 7)
            {
                static int *volatile target = nullptr;
                *target = 7; // access violation: the host must contain it
            }
            // y = seconds since this instance was created, so a smoke test can see state resets.
            elapsed += dt;
            api.setPosition(api.context, node, {static_cast<float>(PROBE_VARIANT), static_cast<float>(elapsed), 0});
        }
        double elapsed = 0;
    };
    // Drives the scene API (added in ABI v4) and the UI API (v6) against a real engine for editor_smoke.py. Publishes on
    // its own node: x = bitmask of passed checks (16383 = all), y = stage (1 = instance up, 2 = instance destroyed).
    struct ApiProbe
    {
        phasma::World world;
        phasma::Node node, instance = 0;
        unsigned passed = 0;
        int stage = 0;
        ApiProbe(const phasma::ScriptApi &api, phasma::Node node) : world(api), node(node) {}
        void Check(int bit, bool ok) { passed |= ok ? 1u << bit : 0u; }
        void Update(double)
        {
            if (stage == 0)
            {
                instance = world.Instantiate("Prefabs/pickup.peprefab");
                Check(0, instance && world.Valid(instance));
                Check(1, world.SetPosition(instance, {100, 200, 300}));
                Check(2, world.SetRotation(instance, {0, 45, 0}));
                Check(3, world.SetScale(instance, {2, 3, 4}));
                Check(4, !world.Play(node, "Idle")); // an empty node has no clips
                Check(5, world.SetRotation(node, {0, 30, 0}) && world.SetScale(node, {1, 2, 3}));
                Check(6, !world.SetScale(node, {0, 1, 1}) && !world.Instantiate("Prefabs/missing.peprefab"));
                phasma::UiSurface surface{};
                Check(8, world.ShowScreen("native.probe") && world.SurfaceSize(surface) && surface.width > 0);
                phasma::UiQuad quad;
                quad.style = phasma::UiStyle::Text;
                quad.body = "probe";
                quad.width = quad.height = 64;
                quad.node = node;
                Check(9, world.SetQuad("native.probe", "quad", quad));
                quad.style = static_cast<phasma::UiStyle>(99); // falls back to the default style
                quad.node = 0;
                Check(10, world.SetQuad("native.probe", "fallback", quad));
                quad.node = 0xDEAD; // an unknown node handle rejects the quad
                Check(11, !world.SetQuad("native.probe", "stale", quad) && !world.SetQuad("native.probe", "", quad));
                phasma::UiWidgetState state{};
                state.hovered = true;
                Check(13, world.WidgetState("native.probe", "quad", state) && !state.clicked &&
                              !world.WidgetState("native.probe", "missing", state) && !state.hovered &&
                              !world.Clicked("native.probe", "quad"));
                stage = 1;
            }
            else if (stage == 1 && world.Find("ApiProbeDestroy"))
            {
                Check(7, world.Destroy(instance) && !world.Valid(instance) && !world.Destroy(instance));
                Check(12, world.RemoveWidget("native.probe", "quad"));
                stage = 2;
            }
            world.SetPosition(node, {static_cast<float>(passed), static_cast<float>(stage), 0});
        }
    };
    const phasma::ScriptDesc scripts[] = {
        phasma::Script<Probe>("Probe", phasma::ScriptKind::Global, phasma::ScriptMode::Always),
        phasma::Script<Probe>(PROBE_VARIANT == 4 ? "Probe" : "NodeProbe", phasma::ScriptKind::Node),
        phasma::Script<ApiProbe>("ApiProbe", phasma::ScriptKind::Node)};
    const phasma::ScriptModule module{
        PROBE_VARIANT == 3 ? 999u : phasma::ScriptAbiVersion,
        sizeof(phasma::ScriptModule), 3, scripts};
} // namespace

PHASMA_SCRIPT_EXPORT const phasma::ScriptModule *PhasmaGetScriptModule(uint32_t) noexcept
{
    return &module;
}
