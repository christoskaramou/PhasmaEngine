#include "ProjectNativeHooks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fstream>
#include <source_location>
#include <cmath>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#endif

// Keep checks active in Release.
extern "C" const phasma::ScriptModule *PhasmaGetScriptModule(uint32_t version) noexcept;

static void Require(bool value, std::source_location where = std::source_location::current())
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL at line %u\n", where.line());
        std::exit(1);
    }
}

static void CheckController(const char *path)
{
    struct State
    {
        phasma::Vec3 position{0, 7, 0};
        const char *keys = "";
        int writes = 0;
        bool valid = true;
    } state;
    phasma::ScriptApi api{phasma::ScriptAbiVersion, sizeof(phasma::ScriptApi), &state};
    api.getPosition = [](void *ctx, phasma::Node node, phasma::Vec3 *value) noexcept -> uint32_t
    {
        const auto &s = *static_cast<State *>(ctx);
        if (!s.valid || node != 42)
            return 0;
        *value = s.position;
        return 1;
    };
    api.setPosition = [](void *ctx, phasma::Node node, phasma::Vec3 value) noexcept -> uint32_t
    {
        auto &s = *static_cast<State *>(ctx);
        if (!s.valid || node != 42)
            return 0;
        s.position = value;
        ++s.writes;
        return 1;
    };
    api.isKeyDown = [](void *ctx, const char *name) noexcept -> uint32_t
    { return std::strchr(static_cast<State *>(ctx)->keys, name[0]) != nullptr; };
    pe::ProjectNativeModule module;
    Require(module.Stage(path));
    module.Commit();
    const phasma::ScriptDesc *controller = nullptr;
    for (uint32_t i = 0; i < module.Active()->scriptCount; ++i)
        if (!std::strcmp(module.Active()->scripts[i].name, "PlayerController"))
            controller = &module.Active()->scripts[i];
    Require(controller && controller->kind == phasma::ScriptKind::Node);
    Require(controller->sourceFile && std::filesystem::exists(controller->sourceFile));
    Require(std::filesystem::path(controller->sourceFile).filename() == "PlayerController.cpp");
    auto duplicateSources = std::vector<phasma::ScriptDesc>{*controller, *controller};
    duplicateSources[1].name = "DifferentName";
    const phasma::ScriptModule duplicate{phasma::ScriptAbiVersion, sizeof(phasma::ScriptModule), 2, duplicateSources.data()};
    Require(!module.StageLinked(&duplicate));
    Require(module.Active() && module.Active()->scriptCount == 3);
    void *instance = nullptr;
    api.version = 1;
    Require(!controller->create(&api, 42, &instance) && !instance);
    api.version = phasma::ScriptAbiVersion;
    Require(controller->create(&api, 42, &instance) != 0);
    Require(controller->update(instance, 1) && state.writes == 0);
    state.keys = "WSAD";
    Require(controller->update(instance, 1) && state.writes == 0);
    state.keys = "W";
    Require(controller->update(instance, 0.5) && controller->update(instance, 0.5));
    Require(state.position.z == -4 && state.position.x == 0 && state.position.y == 7);
    state.position = {0, 7, 0};
    state.keys = "WD";
    Require(controller->update(instance, 1) != 0);
    Require(std::abs(std::hypot(state.position.x, state.position.z) - 4) < 0.0001f);
    Require(state.position.x > 0 && state.position.z < 0 && state.position.y == 7);
    const int writes = state.writes;
    for (double dt : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        Require(controller->update(instance, dt) && state.writes == writes);
    state.valid = false;
    Require(controller->update(instance, 1) && state.writes == writes);
    controller->destroy(instance);
    module.Reset();
}

// Shipped (non-editor) hosts load the installed module in place, once: no shadow copy, no polling,
// and the installed file is never deleted.
static void CheckInstalledLoad(const char *probe)
{
    const auto directory = std::filesystem::path(probe).parent_path() / "installed_load";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto game = directory / ("PhasmaGame" + std::filesystem::path(probe).extension().string());
    std::filesystem::copy_file(probe, game);
    const auto fileCount = [&]
    {
        size_t count = 0;
        for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(directory))
            ++count;
        return count;
    };
    {
        pe::ProjectNativeModule module;
        Require(module.Sync(game, false));
        module.Commit();
        Require(module.Active() && module.Active()->scriptCount == 2);
        Require(fileCount() == 1);
        Require(!module.Sync(game, false));
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        Require(!module.Sync(game, false) && module.Active());
        module.Reset();
        Require(!module.Active() && std::filesystem::exists(game) && fileCount() == 1);
    }
    {
        // A missing module reports one error and is not retried every frame.
        pe::ProjectNativeModule module;
        Require(!module.Sync(directory / "Absent.dll", false) && !module.Error().empty());
        Require(!module.Sync(directory / "Absent.dll", false) && !module.Active());
    }
    {
        // The editor path still observes first and stages a shadow copy.
        pe::ProjectNativeModule module;
        Require(!module.Sync(game, true));
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        Require(module.Sync(game, true));
        module.Commit();
        Require(module.Active() && fileCount() == 2);
        module.Reset();
        Require(fileCount() == 1);
    }
    std::filesystem::remove_all(directory);
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
    Require(argc == 8);
    CheckController(argv[7]);
    CheckInstalledLoad(argv[1]);
    struct State
    {
        int created = 0, destroyed = 0;
        float position = 0;
    } state;
    phasma::ScriptApi api{phasma::ScriptAbiVersion, sizeof(phasma::ScriptApi), &state};
    api.log = [](void *ctx, const char *text) noexcept
    {
        auto &s = *static_cast<State *>(ctx);
        if (!std::strcmp(text, "create"))
            ++s.created;
        if (!std::strcmp(text, "destroy"))
            ++s.destroyed;
    };
    api.setPosition = [](void *ctx, phasma::Node, phasma::Vec3 position) noexcept -> uint32_t
    {
        static_cast<State *>(ctx)->position = position.x;
        return 1;
    };
    pe::ProjectNativeModule module;
    const auto *linked = PhasmaGetScriptModule(phasma::ScriptAbiVersion);
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        Require(module.StageLinked(linked));
        module.Commit();
        Require(!module.StageLinked(nullptr) && module.Active() == linked);
        auto invalid = *linked;
        invalid.version = 0;
        Require(!module.StageLinked(&invalid) && module.Active() == linked);
        void *object = nullptr;
        const auto &script = module.Active()->scripts[0];
        Require(script.create(&api, 0, &object) != 0);
        Require(script.update(object, 1) != 0 && state.position == 1);
        script.destroy(object);
        module.Reset();
        Require(!module.Active() && state.created == state.destroyed);
    }
    state = {};
    const auto live = std::filesystem::path(argv[1]).parent_path() /
                      ("live_input" + std::filesystem::path(argv[1]).extension().string());
    // A copy left by a killed process is swept on the next stage.
    std::ofstream(live.parent_path() / ("live_input_live_stale" + live.extension().string())) << "stale";
    std::filesystem::copy_file(argv[1], live, std::filesystem::copy_options::overwrite_existing);
    Require(module.Stage(live));
    module.Commit();
    void *instance = nullptr;
    auto desc = module.Active()->scripts[0];
    Require(desc.create(&api, 0, &instance) != 0);
    Require(desc.update(instance, 1) != 0 && state.position == 1);

    // Bad ABI, duplicate names, missing file and invalid image retain the active code and state.
    for (int i : {3, 4})
    {
        Require(!module.Stage(argv[i]));
        Require(desc.update(instance, 1) != 0 && state.position == 1);
    }
    Require(!module.Stage(live.string() + ".missing"));
    Require(!module.Stage(__FILE__));
    Require(desc.update(instance, 1) != 0 && state.created == 1 && state.destroyed == 0);

    // The original output remains writable while its shadow copy is loaded.
    std::filesystem::copy_file(argv[2], live, std::filesystem::copy_options::overwrite_existing);
    Require(!module.Poll(live));
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    Require(module.Poll(live));
    desc.destroy(instance);
    module.Commit();
    desc = module.Active()->scripts[0];
    Require(desc.create(&api, 0, &instance) != 0);
    Require(desc.update(instance, 1) != 0 && state.position == 2);
    Require(state.created == 2 && state.destroyed == 1);
    desc.destroy(instance);

    for (int i = 0; i < 8; ++i)
    {
        Require(module.Stage(argv[1 + i % 2]));
        module.Commit();
        desc = module.Active()->scripts[0];
        Require(desc.create(&api, 0, &instance) != 0);
        Require(desc.update(instance, 1) != 0);
        desc.destroy(instance);
    }
    Require(module.Stage(argv[5]));
    module.Commit();
    desc = module.Active()->scripts[0];
    Require(desc.create(&api, 0, &instance) != 0);
    Require(desc.update(instance, 1) == 0); // exception contained by module trampoline
    desc.destroy(instance);                 // failed instances still clean up
    Require(module.Stage(argv[6]));
    module.Commit();
    desc = module.Active()->scripts[0];
    Require(desc.create(&api, 0, &instance) == 0 && instance == nullptr);
    desc.destroy(instance);
    module.Reset();
    Require(state.created == state.destroyed);
    std::filesystem::remove(live);
    for (const auto &entry : std::filesystem::directory_iterator(live.parent_path()))
        Require(entry.path().filename().string().find("_live_") == std::string::npos);
    std::puts("PASS: controller, installed in-place load, replacement, rejection, writable output, repeated reload, exceptions, stale-copy sweep, cleanup");
}
