#include "ProjectNative.h"
#include <cmath>
#include <cstring>
#include <string>
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
    // Drives the scene API (added in ABI v4), the UI API (v6), mouse input and animation speed (v7), sphere and tint (v8), audio and launch options (v9), particles and render type (v10) against a real engine for
    // editor_smoke.py. Publishes on its own node: x = bitmask of passed checks (16777215 = all), y = stage (1 = instance up, 2 = instance destroyed).
    struct ApiProbe
    {
        phasma::World world;
        phasma::Node node, instance = 0, sphere = 0;
        uint64_t passed = 0; // z carries bits 24+ (exact up to bit 47)
        int stage = 0;
        const phasma::ScriptApi &api; // raw calls, for the buffer edge cases
        ApiProbe(const phasma::ScriptApi &api, phasma::Node node) : world(api), node(node), api(api) {}
        void Check(int bit, bool ok) { passed |= ok ? uint64_t{1} << bit : 0u; }
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
                Check(14, !world.LeftMouseDown()); // the smoke editor runs hidden with no button held
                float seconds = -1.0f;
                Check(15, !world.SetSpeed(node, 2.0f) && !world.GetClipDuration(node, "Idle", seconds) && seconds == -1.0f);
                Check(16, world.FindChild(node, "NativeApiProbe") == node && !world.FindChild(node, "missing") &&
                              !world.FindChild(node, ""));
                phasma::Vec3 bone{};
                Check(17, world.SetVisible(node, false) && !world.SetVisible(0xDEAD, true) && !world.BonePosition(node, "Root", bone));
                sphere = world.CreateSphere("NativeProbeSphere", 0.5f);
                Check(18, world.Valid(sphere) && !world.CreateSphere("NativeProbeSphere", 0.0f) && !world.CreateSphere("", 1.0f));
                Check(19, world.SetColor(sphere, {0.25f, 0.5f, 0.75f, 1.0f}, {0.5f, 1.0f, 1.5f}) && !world.SetColor(node, {1, 1, 1, 1}, {}) &&
                              !world.SetColor(sphere, {std::nanf(""), 0, 0, 1}, {}));
                world.StopMusic(); // true or false by build; never throws
                Check(20, !world.PlaySound("") && !world.PlaySound(nullptr) && !world.PlayMusic(""));
                char option[8], tiny[2];
                Check(21, world.LaunchOption("NATIVE_PROBE", option) && std::strcmp(option, "ok") == 0 &&
                              !world.LaunchOption("NATIVE_PROBE", tiny) && !tiny[0] && !world.LaunchOption("native_probe", option) &&
                              !world.LaunchOption("NATIVE_MISSING", option));
                phasma::ParticleBurst burst;
                burst.preset = "enemy_take";
                burst.position = {0.0f, 1.0f, 0.0f};
                burst.set = phasma::BurstCount | phasma::BurstColorStart;
                burst.count = 4;
                burst.colorStart = {1.0f, 0.0f, 0.0f, 1.0f};
                phasma::ParticleBurst bad = burst;
                bad.position.x = std::nanf("");
                Check(22, world.Burst(burst) && !world.Burst(bad));
                Check(23, world.SetRenderType(sphere, "alpha_blend") && !world.SetRenderType(sphere, "nope") &&
                              !world.SetRenderType(node, "opaque"));
                std::string text;
                char small[2];
                uint32_t size = 0;
                Check(24, world.WriteFile("NativeProbe/file_probe.txt", "hello native") &&
                              world.ReadFile("NativeProbe/file_probe.txt", text) && text == "hello native" &&
                              !api.readFile(api.context, "NativeProbe/file_probe.txt", small, 2, &size) && size == 12);
                Check(25, !world.WriteFile("../outside_probe.txt", "x") && !world.ReadFile("../CMakeLists.txt", text) &&
                              !world.ReadFile("NativeProbe/missing.txt", text));
                const phasma::Node ring = world.CreateTorus("NativeProbeRing", 0.5f, 0.02f, 32, 4);
                Check(26, world.Valid(ring) && !world.CreateTorus("NativeProbeRing", 0.0f, 0.02f) &&
                              !world.CreateTorus("NativeProbeRing", 0.5f, 0.02f, 2, 4) && !world.CreateTorus("", 1.0f, 0.1f));
                phasma::FullscreenPass fsPass;
                fsPass.shader = "Shaders/NativeProbe/FullscreenTest.hlsl"; // copied in by editor_smoke.py
                fsPass.params[0] = 1.0f;
                phasma::FullscreenPass badShader = fsPass;
                badShader.shader = nullptr;
                phasma::FullscreenPass badParam = fsPass;
                badParam.params[1] = std::nanf("");
                phasma::FullscreenPass missingShader = fsPass; // accepted; fails to load when it first runs
                missingShader.shader = "Shaders/NativeProbe/Missing.hlsl";
                Check(27, world.AddFullscreenPass("NativeProbeFullscreen", fsPass) &&
                              !world.AddFullscreenPass(nullptr, fsPass) && !world.AddFullscreenPass("", fsPass) &&
                              !world.AddFullscreenPass("NativeProbeFullscreen2", badShader) &&
                              !world.AddFullscreenPass("NativeProbeFullscreen3", badParam) &&
                              world.AddFullscreenPass("NativeProbeBadShader", missingShader)); // both run until stage 2
                float music = -1.0f, check = -1.0f, clamped = -1.0f;
                Check(29, world.GetVolume(phasma::AudioBus::Music, music) &&
                              world.SetVolume(phasma::AudioBus::Music, 0.25f) &&
                              world.GetVolume(phasma::AudioBus::Music, check) && check == 0.25f &&
                              world.SetVolume(phasma::AudioBus::Music, 7.0f) &&
                              world.GetVolume(phasma::AudioBus::Music, clamped) && clamped == 1.0f &&
                              world.SetVolume(phasma::AudioBus::Music, music) &&
                              !world.SetVolume(static_cast<phasma::AudioBus>(9), 0.5f) &&
                              !world.SetVolume(phasma::AudioBus::Sfx, std::nanf("")) &&
                              !world.GetVolume(static_cast<phasma::AudioBus>(9), check));
                Check(30, world.SetStyleBackground(phasma::UiStyle::Card, "") &&
                              !world.SetStyleBackground(static_cast<phasma::UiStyle>(99), "") &&
                              !world.SetStyleBackground(phasma::UiStyle::Card, nullptr));
                // v15 authored UI: the smoke made NativeUiProbe a runtime-UI node before Play.
                const phasma::Node uiNode = world.Find("NativeUiProbe");
                phasma::NodeUi nu;
                nu.set = phasma::NodeUiBody | phasma::NodeUiFill | phasma::NodeUiFontScale | phasma::NodeUiAlignH;
                nu.body = "native ui";
                nu.fill = {0.1f, 0.2f, 0.3f, 1.0f};
                nu.fontScale = 2.0f;
                nu.alignH = phasma::UiAlignH::Right;
                phasma::NodeUi badUi = nu;
                badUi.fontScale = std::nanf("");
                phasma::NodeUi nullText;
                nullText.set = phasma::NodeUiTitle;
                Check(31, world.SetNodeUi(uiNode, nu) && !world.SetNodeUi(uiNode, badUi) &&
                              !world.SetNodeUi(uiNode, nullText) && !world.SetNodeUi(sphere, nu) &&
                              !world.SetNodeUi(0xDEAD, nu));
                Check(32, world.SetEnabled(uiNode, false) && !world.IsEnabled(uiNode) && world.SetEnabled(uiNode, true) &&
                              world.IsEnabled(uiNode) && !world.SetEnabled(0xDEAD, true) && !world.IsEnabled(0xDEAD));
                phasma::Vec3 scale{};
                Check(33, world.GetScale(node, scale) && std::abs(scale.y - 2.0f) < 1e-4f && !world.GetScale(0xDEAD, scale));
                stage = 1;
            }
            else if (stage == 1 && world.Find("ApiProbeDestroy"))
            {
                Check(7, world.Destroy(instance) && !world.Valid(instance) && !world.Destroy(instance));
                // The node-anchored "quad" (bit 9) has been drawn since stage 0.
                phasma::UiRect rect{};
                Check(34, world.GetUiRect(node, rect) && rect.width > 0.0f && rect.height > 0.0f &&
                              !world.GetUiRect(0xDEAD, rect));
                Check(12, world.RemoveWidget("native.probe", "quad"));
                Check(28, world.RemoveFullscreenPass("NativeProbeFullscreen") &&
                              world.RemoveFullscreenPass("NativeProbeBadShader") &&
                              !world.RemoveFullscreenPass("NativeProbeFullscreenMissing"));
                stage = 2;
            }
            world.SetPosition(node, {static_cast<float>(passed & 0xFFFFFFu), static_cast<float>(stage),
                                     static_cast<float>(passed >> 24)});
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
