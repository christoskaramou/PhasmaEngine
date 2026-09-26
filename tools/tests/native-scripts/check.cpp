#include "CppScriptPath.h"
#include "DetachedTask.h"
#include "NativeHandleTable.h"
#include "NativeScriptGuard.h"
#include "NativeTrs.h"
#include "ProjectNativeHooks.h"
#define SDL_MAIN_HANDLED // keep our main(): on Windows SDL.h renames it to SDL_main
#include <SDL.h>
#include <atomic>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fstream>
#include <source_location>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
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

// Scenes store bare filenames; references saved with absolute paths on any OS still match.
static void CheckScriptPaths()
{
    using namespace pe;
    for (const char *path : {"C:\\Users\\dev\\Game\\Native\\Orbit.cpp", "C:/Users/dev/Game/Orbit.cpp",
                             "/home/dev/game/native/Orbit.cpp", "native/sub/Orbit.cpp", "Orbit.cpp"})
    {
        Require(IsCppScriptPath(path) && CppSourceName(path) == "Orbit.cpp" && CppScriptKey(path) == "Orbit.cpp");
        Require(CppScriptReference(path) == "Orbit.cpp");
        Require(MatchesCppScript(path, "Orbit", "Orbit.cpp"));
        Require(MatchesCppScript(path, "Orbit", "/build/machine/Sample/Orbit.cpp"));
        Require(MatchesCppScript(path, "Orbit", "D:\\ci\\Sample\\Orbit.cpp"));
        Require(!MatchesCppScript(path, "Orbit", "PlayerController.cpp") && !MatchesCppScript(path, "Orbit", nullptr));
    }
    Require(IsCppScriptPath("cpp:Orbit") && CppScriptKey("cpp:Orbit") == "Orbit" && CppScriptReference("cpp:Orbit") == "cpp:Orbit");
    Require(MatchesCppScript("cpp:Orbit", "Orbit", nullptr) && !MatchesCppScript("cpp:Orbit", "PlayerController", "Orbit.cpp"));
    for (const char *path : {"Assets/Scripts/orbit.lua", "C:\\Game\\Orbit.h", "/game/Orbit.cpp.bak", ""})
        Require(!IsCppScriptPath(path) && CppScriptReference(path) == path);
    Require(!MatchesCppScript("Assets/Scripts/Orbit.lua", "Orbit", "Orbit.cpp"));
}

static void CheckFindSource(const char *probe)
{
    const auto directory = std::filesystem::path(probe).parent_path() / "find_source";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory / "Gameplay" / "Enemies");
    std::ofstream(directory / "Gameplay" / "Enemies" / "Turret.cpp") << "";
    std::ofstream(directory / "Orbit.cpp") << "";
    std::filesystem::create_directories(directory / "Folder.cpp");
    Require(std::filesystem::path(pe::FindCppSource(directory, "Turret.cpp")) == directory / "Gameplay" / "Enemies" / "Turret.cpp");
    Require(std::filesystem::path(pe::FindCppSource(directory, "C:\\Users\\dev\\Turret.cpp")) == directory / "Gameplay" / "Enemies" / "Turret.cpp");
    Require(std::filesystem::path(pe::FindCppSource(directory, "/elsewhere/Orbit.cpp")) == directory / "Orbit.cpp");
    for (const char *missing : {"Missing.cpp", "Folder.cpp", "Turret", "cpp:Turret", "Enemies", ""})
        Require(pe::FindCppSource(directory, missing).empty());
    Require(pe::FindCppSource(directory / "absent", "Orbit.cpp").empty());
    std::filesystem::remove_all(directory);
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
    const phasma::ScriptDesc *controller = nullptr, *orbit = nullptr;
    for (uint32_t i = 0; i < module.Active()->scriptCount; ++i)
    {
        const auto &script = module.Active()->scripts[i];
        Require(script.kind == phasma::ScriptKind::Node); // the sample must not run globally in every project
        if (!std::strcmp(script.name, "PlayerController"))
            controller = &script;
        else if (!std::strcmp(script.name, "Orbit"))
            orbit = &script;
    }
    Require(module.Active()->scriptCount == 2 && orbit);
    Require(controller && controller->kind == phasma::ScriptKind::Node);
    // Binaries carry bare filenames, never the build machine's source paths.
    Require(controller->sourceFile && !std::strcmp(controller->sourceFile, "PlayerController.cpp"));
    Require(orbit->sourceFile && !std::strcmp(orbit->sourceFile, "Orbit.cpp"));
    auto duplicateSources = std::vector<phasma::ScriptDesc>{*controller, *controller};
    duplicateSources[1].name = "DifferentName";
    const phasma::ScriptModule duplicate{phasma::ScriptAbiVersion, sizeof(phasma::ScriptModule), 2, duplicateSources.data()};
    Require(!module.StageLinked(&duplicate));
    Require(module.Active() && module.Active()->scriptCount == 2);
    // Validation normalizes names the way scenes match them: Windows and POSIX paths to one filename
    // collide on every host, distinct filenames do not.
    duplicateSources[0].sourceFile = "a\\Dup.cpp";
    duplicateSources[1].sourceFile = "b/Dup.cpp";
    Require(!module.StageLinked(&duplicate) && module.Error() == "Node scripts must have unique source filenames");
    duplicateSources[1].sourceFile = "b/Other.cpp";
    Require(module.StageLinked(&duplicate) && module.Active()->scriptCount == 2);
    void *instance = nullptr;
    api.version = 1;
    Require(!controller->create(&api, 42, &instance) && !instance);
    // Append-only ABI: a newer host (higher version, longer table) is accepted; older or shorter ones are not.
    struct NewerApi
    {
        phasma::ScriptApi base;
        void (*appended)() noexcept;
    } newer{api, nullptr};
    newer.base.version = phasma::ScriptAbiVersion + 1;
    newer.base.size = sizeof(NewerApi);
    Require(controller->create(&newer.base, 42, &instance) != 0 && instance);
    controller->destroy(instance);
    instance = nullptr;
    newer.base.version = phasma::ScriptAbiVersion - 1;
    Require(!controller->create(&newer.base, 42, &instance) && !instance);
    newer.base.version = phasma::ScriptAbiVersion;
    newer.base.size = sizeof(phasma::ScriptApi) - 1;
    Require(!controller->create(&newer.base, 42, &instance) && !instance);
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

    // Module.cpp serves hosts at or above its version.
    void *library = SDL_LoadObject(path);
    Require(library != nullptr);
    const auto query = reinterpret_cast<phasma::GetScriptModule>(SDL_LoadFunction(library, "PhasmaGetScriptModule"));
    Require(query && query(phasma::ScriptAbiVersion) && query(phasma::ScriptAbiVersion + 1));
    Require(!query(phasma::ScriptAbiVersion - 1));
    SDL_UnloadObject(library);
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
        Require(!module.Status().liveReload); // no policy before the host asks
        Require(module.Sync(game, false));
        module.Commit();
        Require(module.Active() && module.Active()->scriptCount == 3);
        Require(fileCount() == 1 && !module.Status().liveReload); // installed load: never polled
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
        Require(module.Active() && fileCount() == 2 && module.Status().liveReload);
        module.Reset();
        Require(fileCount() == 1);
    }
    std::filesystem::remove_all(directory);
}

// Build folders carry NativeScripts.json beside the executable, so a Player run there live-reloads
// (shadow copy, rebuildable); exported folders omit it and load in place.
static void CheckLiveReloadPolicy(const char *probe)
{
    const auto directory = std::filesystem::path(probe).parent_path() / "live_policy";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    Require(pe::ProjectNativeModule::LiveReloadEnabled(true, directory));
    Require(!pe::ProjectNativeModule::LiveReloadEnabled(false, directory));
    Require(!pe::ProjectNativeModule::LiveReloadEnabled(false, directory / "missing"));
    std::ofstream(directory / "NativeScripts.json") << "{}";
    Require(pe::ProjectNativeModule::LiveReloadEnabled(false, directory));
    Require(pe::ProjectNativeModule::LiveReloadEnabled(true, directory));
    std::filesystem::remove_all(directory);
}

// Minimal PE image: DOS header, PE signature, COFF header, optional header, one section holding the
// debug directory (one CODEVIEW entry) and its RSDS record.
static std::vector<uint8_t> MakePeImage(bool pe32Plus, const std::string &pdb)
{
    std::vector<uint8_t> image(0x600);
    const auto put16 = [&](size_t at, uint32_t v)
    { image[at] = uint8_t(v), image[at + 1] = uint8_t(v >> 8); };
    const auto put32 = [&](size_t at, uint32_t v)
    { put16(at, v), put16(at + 2, v >> 16); };
    image[0] = 'M', image[1] = 'Z';
    put32(0x3C, 0x80);
    std::memcpy(&image[0x80], "PE\0\0", 4);
    const size_t optional = 0x98, directories = optional + (pe32Plus ? 112 : 96);
    const uint32_t optionalSize = uint32_t(directories - optional + 16 * 8);
    put16(0x86, 1); // NumberOfSections
    put16(0x94, optionalSize);
    put16(optional, pe32Plus ? 0x20b : 0x10b);
    put32(directories - 4, 16); // NumberOfRvaAndSizes
    put32(directories + 6 * 8, 0x1000);
    put32(directories + 6 * 8 + 4, 28);
    const size_t section = optional + optionalSize;
    std::memcpy(&image[section], ".rdata", 6);
    put32(section + 8, 0x200);
    put32(section + 12, 0x1000);
    put32(section + 16, 0x200);
    put32(section + 20, 0x400);
    put32(0x400 + 12, 2); // IMAGE_DEBUG_TYPE_CODEVIEW
    put32(0x400 + 16, uint32_t(24 + pdb.size() + 1));
    put32(0x400 + 20, 0x1040);
    put32(0x400 + 24, 0x440);
    std::memcpy(&image[0x440], "RSDS", 4);
    for (int i = 0; i < 20; ++i)
        image[0x444 + i] = uint8_t(0xA0 + i); // GUID + age
    std::memcpy(&image[0x458], pdb.data(), pdb.size());
    return image;
}

// Naive reader for the tests: the path of the first RSDS record.
static std::string CodeViewPath(const std::vector<uint8_t> &image)
{
    for (size_t i = 0; i + 24 < image.size(); ++i)
        if (!std::memcmp(&image[i], "RSDS", 4))
            return reinterpret_cast<const char *>(&image[i + 24]);
    return {};
}

static std::vector<uint8_t> ReadBytes(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Live copies must name their own PDB or an attached debugger locks the build's PDB (LNK1201).
static void CheckPdbPatch(const char *probe)
{
    const std::string original = "C:\\Build\\Release\\PhasmaGame.pdb";
    for (bool pe32Plus : {true, false})
    {
        auto image = MakePeImage(pe32Plus, original);
        const auto pristine = image;
        Require(pe::ProjectNativeModule::PatchCodeViewPdbPath(image, "PhasmaGame_live_1_0.pdb"));
        Require(CodeViewPath(image) == "PhasmaGame_live_1_0.pdb");
        for (size_t i = 0; i < image.size(); ++i)
            Require(image[i] == pristine[i] || (i >= 0x458 && i < 0x458 + original.size()));
        for (size_t i = 0x458 + 23; i < 0x458 + original.size() + 1; ++i)
            Require(image[i] == 0);
        image = pristine;
        Require(pe::ProjectNativeModule::PatchCodeViewPdbPath(image, std::string(original.size(), 'x')));
        Require(CodeViewPath(image) == std::string(original.size(), 'x'));
        // Too long, empty or embedded NUL: refused, image untouched.
        image = pristine;
        Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(image, std::string(original.size() + 1, 'x')));
        Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(image, ""));
        Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(image, std::string_view("a\0b", 3)));
        Require(image == pristine);
        // Every truncation is refused (or, once the record is complete, patched) without reading out of bounds.
        const size_t recordEnd = 0x458 + original.size() + 1;
        for (size_t length = 0; length < image.size(); ++length)
        {
            std::vector<uint8_t> truncated(pristine.begin(), pristine.begin() + length);
            Require(pe::ProjectNativeModule::PatchCodeViewPdbPath(truncated, "p.pdb") == (length >= recordEnd));
        }
        // Corrupt headers and records.
        const auto corrupt = [&](size_t at, uint32_t value)
        {
            auto bad = pristine;
            std::memcpy(&bad[at], &value, 4);
            const auto before = bad;
            return !pe::ProjectNativeModule::PatchCodeViewPdbPath(bad, "p.pdb") && bad == before;
        };
        const size_t directories = 0x98 + (pe32Plus ? 112 : 96);
        Require(corrupt(0, 0x5A4E));                  // 'NZ'
        Require(corrupt(0x3C, 0xFFFFFFF0u));          // e_lfanew past the end
        Require(corrupt(0x80, 0x00004550 + 1));       // PE signature
        Require(corrupt(0x84, 0xFFFF0000u | 0x8664)); // 65535 sections
        Require(corrupt(0x98, 0x0107));               // optional header magic
        Require(corrupt(directories - 4, 6));         // no debug directory slot
        Require(corrupt(directories + 48, 0x5000));   // debug RVA outside every section
        Require(corrupt(directories + 52, 0xFFFFFFF0u));
        Require(corrupt(0x400 + 16, 0xFFFFFFF0u)); // record size
        Require(corrupt(0x400 + 24, 0xFFFFFFF0u)); // record offset
        Require(corrupt(0x440, 0x3031424E));       // 'NB10'
        auto unterminated = pristine;
        std::memset(&unterminated[0x458], 'x', original.size() + 1);
        Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(unterminated, "p.pdb"));
    }
    std::vector<uint8_t> empty, text(4096, 'M');
    Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(empty, "p.pdb"));
    Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(text, "p.pdb"));
    auto real = ReadBytes(probe);
#if defined(_MSC_VER)
    // Probes link with /DEBUG, so a real image carries an RSDS record naming probeN.pdb.
    Require(std::filesystem::path(CodeViewPath(real)).filename() ==
            std::filesystem::path(probe).filename().replace_extension(".pdb"));
    Require(pe::ProjectNativeModule::PatchCodeViewPdbPath(real, "p.pdb") && CodeViewPath(real) == "p.pdb");
#elif !defined(_WIN32)
    Require(!pe::ProjectNativeModule::PatchCodeViewPdbPath(real, "p.pdb")); // ELF/Mach-O
#endif
}

static std::string Base36(uint64_t value)
{
    std::string text;
    do
    {
        text.insert(text.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[value % 36]);
        value /= 36;
    }
    while (value);
    return text;
}

static uint32_t SelfPid()
{
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

#if defined(_WIN32)
static constexpr uint32_t AlivePid = 4; // System
#else
static constexpr uint32_t AlivePid = 1; // init
#endif
static constexpr uint32_t DeadPid = 0x7FFFFFF0; // above any real pid on Windows and Linux

static size_t CountEntries(const std::filesystem::path &directory)
{
    size_t count = 0;
    for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(directory))
        ++count;
    return count;
}

// Makes a folder refuse new files (POSIX mode bits; a Windows deny ACE), or restores it. False if unsupported.
static bool SetFolderWritable(const std::filesystem::path &directory, bool writable)
{
#if defined(_WIN32)
    const std::string command = std::string("icacls \"") + directory.string() +
                                (writable ? "\" /remove:d *S-1-1-0 >nul 2>nul" : "\" /deny *S-1-1-0:(WD,AD) >nul 2>nul");
    return std::system(command.c_str()) == 0;
#else
    if (geteuid() == 0)
        return false; // root ignores mode bits
    std::error_code ec;
    std::filesystem::permissions(directory,
                                 writable ? std::filesystem::perms::owner_all
                                          : std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 ec);
    return !ec;
#endif
}

// Artifact identity ties status to one build: a delayed earlier build is never credited to a later one, and
// the attempt counter exposes repeated rejections even when the error text is identical.
static void CheckArtifactStatus(const char *good, const char *badAbi)
{
    using Outcome = pe::NativeReloadOutcome;
    const auto directory = std::filesystem::path(good).parent_path() / "artifact_status";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto game = directory / ("PhasmaGame" + std::filesystem::path(good).extension().string());
    const auto bump = [&]
    {
        std::filesystem::last_write_time(game, std::filesystem::last_write_time(game) + std::chrono::seconds(2));
    };
    Require(pe::ProjectNativeModule::ArtifactIdentity(directory / "missing.dll").empty());
    std::filesystem::copy_file(good, game);
    const auto first = pe::ProjectNativeModule::ArtifactIdentity(game);
    Require(!first.empty());
    bump();
    Require(pe::ProjectNativeModule::ArtifactIdentity(game) != first);
    {
        pe::ProjectNativeModule module;
        const auto goodId = pe::ProjectNativeModule::ArtifactIdentity(game);
        Require(module.Stage(game));
        module.Commit();
        auto status = module.Status();
        Require(status.reloads == 1 && status.attempts == 1 && status.active && status.error.empty());
        Require(status.activeArtifact == goodId && status.attemptedArtifact == goodId);
        Require(!status.liveReload); // staged directly, no host policy: nothing polls it

        // A rejected build: the running module and its artifact stay; the attempt names the new artifact.
        std::filesystem::copy_file(badAbi, game, std::filesystem::copy_options::overwrite_existing);
        bump();
        const auto badId = pe::ProjectNativeModule::ArtifactIdentity(game);
        Require(!module.Stage(game));
        status = module.Status();
        Require(status.attempts == 2 && status.reloads == 1 && status.attemptedArtifact == badId);
        Require(status.activeArtifact == goodId && !status.error.empty());
        const auto error = status.error;

        // The same bad build again: identical error, but a new attempt and a new artifact.
        bump();
        const auto badAgain = pe::ProjectNativeModule::ArtifactIdentity(game);
        Require(!module.Stage(game));
        status = module.Status();
        Require(status.attempts == 3 && status.error == error && status.attemptedArtifact == badAgain && badAgain != badId);

        // Poll hands Stage the artifact it observed: a file that changed since is refused without an attempt,
        // so a newer build is never loaded under an older build's record.
        const std::string observedEarlier = badId;
        Require(!module.Stage(game, &observedEarlier) && module.Error().find("changed") != std::string::npos);
        Require(module.Status().attempts == 3 && module.Status().activeArtifact == goodId);
        module.Reset();
        Require(module.Status().error.empty() && module.Error().empty()); // Reset clears diagnostics
    }

    pe::CppScriptStatus start, now;
    start.activeArtifact = "10:1";
    now = start;
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "20:2", false) == Outcome::Pending);
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "20:2", true) == Outcome::NotObserved);
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "10:1", false) == Outcome::Current); // no-op build
    now.activeArtifact = "15:5";                                                                     // a delayed earlier build reloads first: a real reload, but not this build's
    now.reloads = start.reloads + 1;
    now.attempts = start.attempts + 1;
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "20:2", false) == Outcome::Pending);
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "20:2", true) == Outcome::NotObserved);
    now.activeArtifact = "20:2";
    now.reloads = start.reloads + 2;
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "20:2", false) == Outcome::Reloaded);
    now = start;
    now.attemptedArtifact = "20:2";
    now.attempts = start.attempts + 1;
    now.error = "Incompatible script module ABI";
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "20:2", false) == Outcome::Rejected);
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "30:3", false) == Outcome::Pending); // someone else's rejection
    Require(pe::ProjectNativeModule::ClassifyReload(start, now, "", true) == Outcome::NotObserved);
    std::filesystem::remove_all(directory);
}

// Only permission refusals trigger the in-place fallback; any other copy failure stays a failure.
static void CheckNonPermissionCopyFailure(const char *good)
{
    const auto directory = std::filesystem::path(good).parent_path() / "copy_failure";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    // Here the shadow copy's name is too long; loading in place would work, so a fallback would be visible.
    const auto folder = directory.string().size() + 1;
#if defined(_WIN32)
    const size_t length = folder < 240 ? 259 - folder : 0; // the build output fits MAX_PATH; its copy does not
#else
    const size_t length = 250; // NAME_MAX is 255; the copy adds "_live_<pid>_<seq>"
#endif
    const auto extension = std::filesystem::path(good).extension().string();
    if (length <= extension.size() + 1)
        std::puts("SKIP: non-permission copy failure (build folder path too long for the long-name case)");
    else
    {
        const auto longGame = directory / (std::string(length - extension.size(), 'n') + extension);
        std::filesystem::copy_file(good, longGame);
        pe::ProjectNativeModule module;
        Require(!module.Sync(longGame, true));
        // Polls at ~0.55 s: the first attempt fails, then retries follow the backoff (0.5 s, 1 s, 2 s ...)
        // for the same artifact, so over ~3.3 s there are a few attempts but fewer than polls.
        for (int i = 0; i < 6; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            if (module.Sync(longGame, true)) // a load of any kind (a wrongful fallback included) becomes visible
                module.Commit();
        }
        const auto status = module.Status();
        if (status.active && status.liveReload)
            std::puts("SKIP: non-permission copy failure (long paths are enabled; the copy succeeded)");
        else
        {
            Require(!status.active && status.liveReload && status.error.rfind("Copy failed", 0) == 0);
            Require(module.Notice().empty());
            Require(status.attempts >= 3 && status.attempts <= 5); // retried without a new artifact, with backoff
        }
        module.Reset();
        std::filesystem::remove(longGame);
    }
#if defined(_WIN32)
    {
        // A sharing violation (an antivirus scanning the new DLL, say) is retried for the same artifact and
        // loads once the lock is released, without another build.
        const auto locked = directory / ("PhasmaGame" + extension);
        std::filesystem::copy_file(good, locked);
        HANDLE handle = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(handle != INVALID_HANDLE_VALUE);
        pe::ProjectNativeModule module;
        Require(!module.Sync(locked, true));
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        Require(!module.Sync(locked, true)); // copy fails while locked; not a permission error, so no fallback
        Require(module.Status().error.rfind("Copy failed", 0) == 0 && module.Status().liveReload);
        CloseHandle(handle);
        bool loaded = false;
        for (int i = 0; i < 8 && !loaded; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            loaded = module.Sync(locked, true);
        }
        Require(loaded);
        module.Commit();
        Require(module.Status().active && module.Status().liveReload && module.Status().error.empty());
        module.Reset();
    }
#endif
    std::filesystem::remove_all(directory);
}

// When no shadow copy can be made, the initial load runs in place with reload off; a running module is never replaced.
static void CheckInPlaceFallback(const char *good, const char *badAbi)
{
    const auto directory = std::filesystem::path(good).parent_path() / "fallback_load";
    if (std::filesystem::exists(directory))
        SetFolderWritable(directory, true);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto game = directory / ("PhasmaGame" + std::filesystem::path(good).extension().string());
    std::filesystem::copy_file(good, game);
    if (!SetFolderWritable(directory, false))
    {
        std::puts("SKIP: in-place fallback (cannot make a folder unwritable here, e.g. running as root)");
        std::filesystem::remove_all(directory);
        return;
    }
    {
        pe::ProjectNativeModule module;
        Require(!module.Sync(game, true));
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        Require(module.Sync(game, true)); // copy fails -> in-place load
        module.Commit();
        auto status = module.Status();
        Require(status.active && !status.liveReload && status.reloads == 1);
        Require(status.activeArtifact == pe::ProjectNativeModule::ArtifactIdentity(game));
        Require(module.Notice().find("live reload disabled") != std::string::npos);
        Require(CountEntries(directory) == 1);
        const auto attempts = status.attempts;
        for (int i = 0; i < 2; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            Require(!module.Sync(game, true)); // no polling or retries once disabled
        }
        Require(module.Status().attempts == attempts && module.NoticeCount() == 1);
        module.Reset();
        Require(std::filesystem::exists(game));                            // in-place loads never delete the build output
        Require(module.Notice().empty() && module.Status().error.empty()); // Reset clears diagnostics
    }
    Require(SetFolderWritable(directory, true));
    {
        pe::ProjectNativeModule module;
        Require(!module.Sync(game, true));
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        Require(module.Sync(game, true)); // normal shadow copy
        module.Commit();
        const auto running = module.Status().activeArtifact;
        Require(SetFolderWritable(directory, false));
        std::filesystem::last_write_time(game, std::filesystem::last_write_time(game) + std::chrono::seconds(2));
        for (int i = 0; i < 3; ++i) // first sight of the new stamp, then the stage attempt (and one spare poll)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            Require(!module.Sync(game, true)); // copy fails, but a running module is never replaced in place
        }
        const auto status = module.Status();
        Require(status.liveReload && status.activeArtifact == running && !status.error.empty());
        Require(module.Notice().find("live reload disabled") == std::string::npos);
        Require(SetFolderWritable(directory, true));
        module.Reset();
    }
    {
        // A failed in-place load reports the copy error with the load error, and keeps polling: the next
        // build then loads in place.
        const auto rewrite = [&](const char *from, int seconds)
        {
            const auto bytes = ReadBytes(from);
            std::ofstream(game, std::ios::binary | std::ios::trunc)
                .write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            std::filesystem::last_write_time(game, std::filesystem::last_write_time(game) + std::chrono::seconds(seconds));
        };
        rewrite(badAbi, 4);
        Require(SetFolderWritable(directory, false));
        pe::ProjectNativeModule module;
        Require(!module.Sync(game, true));
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        Require(!module.Sync(game, true));
        auto status = module.Status();
        Require(!status.active && status.liveReload);
        Require(status.error.rfind("Shadow copy failed", 0) == 0 && status.error.find("in-place load failed") != std::string::npos);
        rewrite(good, 8);
        bool loaded = false;
        for (int i = 0; i < 4 && !loaded; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            loaded = module.Sync(game, true);
        }
        Require(loaded);
        module.Commit();
        status = module.Status();
        Require(status.active && !status.liveReload && status.error.empty());
        module.Reset();
        Require(SetFolderWritable(directory, true));
    }
    std::filesystem::remove_all(directory);
}

// SEH-guarded calls pass results through, and contain a hardware fault in game code where supported.
static void CheckGuard(const char *faultingProbe)
{
    uint32_t fault = 1;
    void *state = nullptr;
    Require(pe::GuardedCreate([](const phasma::ScriptApi *, phasma::Node node, void **out) noexcept -> uint32_t
                              { *out = reinterpret_cast<void *>(node); return 7; }, nullptr, 42, &state, &fault) == 7);
    Require(fault == 0 && state == reinterpret_cast<void *>(42));
    Require(pe::GuardedCreate([](const phasma::ScriptApi *, phasma::Node, void **) noexcept -> uint32_t
                              { return 0; }, nullptr, 0, &state, &fault) == 0 &&
            fault == 0);
    const auto update = [](void *s, double dt) noexcept -> uint32_t
    { return s && dt > 0 ? 1u : 0u; };
    Require(pe::GuardedUpdate(update, &fault, 1, &fault) == 1 && fault == 0);
    fault = 1;
    Require(pe::GuardedUpdate(update, &fault, -1, &fault) == 0 && fault == 0);
    int destroyed = 0;
    fault = 1;
    pe::GuardedDestroy([](void *s) noexcept
                       { ++*static_cast<int *>(s); }, &destroyed, &fault);
    Require(destroyed == 1 && fault == 0);

    int logs = 0;
    phasma::ScriptApi api{phasma::ScriptAbiVersion, sizeof(phasma::ScriptApi), &logs};
    api.log = [](void *ctx, const char *) noexcept
    { ++*static_cast<int *>(ctx); };
    pe::ProjectNativeModule module;
    Require(module.Stage(faultingProbe));
    module.Commit();
    const auto &script = module.Active()->scripts[0];
    Require(pe::GuardedCreate(script.create, &api, 0, &state, &fault) == 1 && fault == 0 && state);
    if (pe::NativeFaultsContained())
    {
        Require(pe::GuardedUpdate(script.update, state, 1, &fault) == 0);
        std::printf("contained fault 0x%08X\n", fault);
        Require(fault != 0); // state is leaked, never destroyed
    }
    else
        pe::GuardedDestroy(script.destroy, state, &fault);
    module.Reset();
}

struct FakeNode
{
    int slot;
    uint32_t revision;
};

struct FakeAlive
{
    const std::vector<uint32_t> *revisions; // 0 = dead
    bool operator()(const FakeNode &node) const { return (*revisions)[node.slot] == node.revision; }
};

static void CheckHandleTable()
{
    std::vector<uint32_t> revisions(4096, 0);
    uint32_t nextRevision = 1;
    const auto spawn = [&](int slot)
    { return FakeNode{slot, revisions[slot] = nextRevision++}; };
    pe::NativeHandleTable<int, FakeNode, FakeAlive> table(FakeAlive{&revisions});
    Require(!table.Resolve(0) && !table.Resolve(12345));

    const auto a = spawn(1);
    const auto handle = table.Issue(1, a);
    Require(handle != 0 && table.Issue(1, a) == handle && table.Size() == 1);
    Require(table.Resolve(handle) && table.Resolve(handle)->revision == a.revision);
    revisions[1] = 0; // dies
    Require(!table.Resolve(handle));
    const auto b = spawn(1); // slot reused by another node
    const auto reused = table.Issue(1, b);
    Require(reused != handle && !table.Resolve(handle) && table.Resolve(reused)->revision == b.revision);
    Require(table.Size() == 1);

    // Churn: nodes created and destroyed outside the table must not accumulate.
    size_t peak = 0;
    for (int i = 0; i < 20000; ++i)
    {
        const int slot = 100 + i % 1000;
        table.Issue(slot, spawn(slot));
        revisions[slot] = 0;
        peak = (std::max)(peak, table.Size()); // parenthesized: <windows.h> max macro
    }
    Require(peak <= 2 * decltype(table)::MinPruneSize);
    Require(table.Resolve(reused));

    // A subtree delete kills several nodes at once; MarkStale prunes them on the next Maintain.
    table.Prune();
    std::vector<uint64_t> subtree;
    for (int slot = 2000; slot < 2010; ++slot)
        subtree.push_back(table.Issue(slot, spawn(slot)));
    const auto before = table.Size();
    for (int slot = 2000; slot < 2010; ++slot)
        revisions[slot] = 0;
    table.MarkStale();
    table.Maintain();
    Require(table.Size() == before - subtree.size());
    for (const auto h : subtree)
        Require(!table.Resolve(h));
    Require(table.Resolve(reused));

    // Many live handles grow the table; pruning never drops them.
    std::vector<uint64_t> live;
    for (int slot = 3000; slot < 3500; ++slot)
        live.push_back(table.Issue(slot, spawn(slot)));
    for (size_t i = 0; i < live.size(); ++i)
        Require(table.Resolve(live[i]) && table.Resolve(live[i])->slot == 3000 + static_cast<int>(i));
    table.Clear();
    Require(table.Size() == 0 && !table.Resolve(reused) && table.Issue(1, b) > live.back());
}

static bool Near(const glm::mat4 &a, const glm::mat4 &b, float epsilon = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!(std::abs(a[c][r] - b[c][r]) <= epsilon))
                return false;
    return true;
}

static bool Finite(const glm::mat4 &m)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r]))
                return false;
    return true;
}

static void CheckTrs()
{
    const auto compose = [](glm::vec3 t, glm::vec3 degrees, glm::vec3 s)
    { return pe::ComposeTrs({t, glm::quat(glm::radians(degrees)), s}); };
    const glm::vec3 t(1, -2, 3), r1(10, 20, 30), r2(-40, 75, 5);
    for (const glm::vec3 s : {glm::vec3(1), glm::vec3(2, 3, 4), glm::vec3(-2, 3, 4), glm::vec3(0.001f, 5, 1)})
    {
        const auto m = compose(t, r1, s);
        Require(Near(pe::ComposeTrs(pe::DecomposeTrs(m)), m));
        const glm::vec3 scale(7, 8, 9);
        Require(Near(pe::ReplaceTrs(m, &r2, nullptr), compose(t, r2, s)));
        Require(Near(pe::ReplaceTrs(m, nullptr, &scale), compose(t, r1, scale)));
    }
    {
        // Documented in the ProjectNative README: a Y mirror decomposes onto X, so replacing the rotation
        // keeps the reflection but can change orientation; an all-positive scale removes the reflection.
        const glm::mat4 yMirror = glm::scale(glm::mat4(1.0f), glm::vec3(1, -3, 4));
        const glm::vec3 zero(0), positive(1, 3, 4);
        Require(Near(pe::ReplaceTrs(yMirror, &zero, nullptr), glm::scale(glm::mat4(1.0f), glm::vec3(-1, 3, 4))));
        Require(glm::determinant(glm::mat3(pe::ReplaceTrs(yMirror, nullptr, &positive))) > 0.0f);
    }
    // Mirrored scale survives a rotation change (lengths alone would drop the reflection).
    const auto mirrored = pe::ReplaceTrs(compose(t, r1, {-2, 3, 4}), &r2, nullptr);
    Require(glm::determinant(glm::mat3(mirrored)) < 0);
    Require(pe::DecomposeTrs(compose(t, r1, {1, -3, 1})).scale.x < 0); // any single mirror becomes -x
    // Zero-scale axes keep a finite rotation.
    for (const glm::vec3 s : {glm::vec3(0, 2, 3), glm::vec3(2, 0, 3), glm::vec3(2, 3, 0), glm::vec3(0, 0, 3), glm::vec3(0)})
    {
        const auto m = compose(t, r1, s);
        const auto rotated = pe::ReplaceTrs(m, &r2, nullptr);
        Require(Finite(rotated) && Near(rotated, compose(t, r2, s)));
        const glm::vec3 scale(1, 2, 3);
        const auto rescaled = pe::ReplaceTrs(m, nullptr, &scale);
        Require(Finite(rescaled) && glm::length(glm::vec3(rescaled[3]) - t) < 1e-4f);
        for (int axis = 0; axis < 3; ++axis)
            Require(std::abs(glm::length(glm::vec3(rescaled[axis])) - scale[axis]) < 1e-4f);
        // Surviving axes keep their directions.
        for (int axis = 0; axis < 3; ++axis)
            if (s[axis] != 0 && (s.x == 0) + (s.y == 0) + (s.z == 0) == 1)
                Require(glm::length(glm::vec3(rescaled[axis]) / scale[axis] - glm::vec3(m[axis]) / s[axis]) < 1e-4f);
    }
    // Parallel survivors (sheared degenerate input) stay finite too.
    glm::mat4 sheared(1.0f);
    sheared[0] = glm::vec4(0);
    sheared[2] = glm::vec4(0, 2, 0, 0);
    Require(Finite(pe::ReplaceTrs(sheared, &r2, nullptr)));
}

static void CheckDetachedTask()
{
    using namespace std::chrono;
    auto finished = std::make_shared<std::atomic<bool>>(false);
    {
        pe::DetachedTask<int> task;
        task.Start([finished]
                   {
            std::this_thread::sleep_for(milliseconds(1500));
            finished->store(true);
            return 1; });
        Require(task.valid() && !task.Ready());
        const auto start = steady_clock::now();
        {
            pe::DetachedTask<int> moved = std::move(task);
        } // abandoning a running task must not wait for it
        Require(steady_clock::now() - start < milliseconds(500));
        Require(!finished->load());
    }
    for (int i = 0; i < 100 && !finished->load(); ++i)
        std::this_thread::sleep_for(milliseconds(50));
    Require(finished->load());

    pe::DetachedTask<std::string> task;
    Require(!task.valid() && !task.Ready());
    task.Start([]
               {
        std::this_thread::sleep_for(milliseconds(50));
        return std::string("built"); });
    const auto deadline = steady_clock::now() + seconds(10);
    while (!task.Ready() && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds(5));
    Require(task.Ready() && task.Get() == "built" && !task.valid());
    task.Start([]() -> std::string
               { throw std::runtime_error("cmake missing"); });
    while (!task.Ready())
        std::this_thread::sleep_for(milliseconds(5));
    bool threw = false;
    try
    {
        task.Get();
    }
    catch (const std::runtime_error &e)
    {
        threw = std::string(e.what()) == "cmake missing";
    }
    Require(threw && !task.valid());
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
    Require(argc == 9);
    CheckScriptPaths();
    CheckFindSource(argv[1]);
    CheckGuard(argv[8]);
    CheckHandleTable();
    CheckTrs();
    CheckDetachedTask();
    CheckController(argv[7]);
    CheckInstalledLoad(argv[1]);
    CheckLiveReloadPolicy(argv[1]);
    CheckPdbPatch(argv[1]);
    CheckArtifactStatus(argv[1], argv[3]);
    CheckInPlaceFallback(argv[1], argv[3]);
    CheckNonPermissionCopyFailure(argv[1]);
    Require(pe::ProjectNativeModule::ProcessAlive(SelfPid()) && pe::ProjectNativeModule::ProcessAlive(AlivePid));
    Require(!pe::ProjectNativeModule::ProcessAlive(DeadPid));
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
        Require(!module.Status().liveReload); // statically linked: never reloaded
        Require(!module.StageLinked(nullptr) && module.Active() == linked);
        auto invalid = *linked;
        invalid.version = 0;
        Require(!module.StageLinked(&invalid) && module.Active() == linked);
        // Hosts accept module versions in [ScriptAbiMinVersion, ScriptAbiVersion] with the frozen size.
        invalid.version = phasma::ScriptAbiVersion + 1;
        Require(!module.StageLinked(&invalid) && module.Active() == linked);
        invalid.version = phasma::ScriptAbiMinVersion - 1;
        Require(!module.StageLinked(&invalid) && module.Active() == linked);
        invalid.version = phasma::ScriptAbiVersion;
        invalid.size = sizeof(phasma::ScriptModule) + 8;
        Require(!module.StageLinked(&invalid) && module.Active() == linked);
        invalid.size = sizeof(phasma::ScriptModule);
        Require(module.StageLinked(&invalid));
        module.Commit();
        Require(module.Active() == &invalid);
        Require(module.StageLinked(linked));
        module.Commit();
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
    // The sweep deletes only names the loader writes: this process's stale copies, a dead process's, and
    // the legacy clock-tag format. Everything else stays, including names that merely share a prefix and
    // names that are not representable in the ANSI code page (converting those throws on Windows).
    const auto dir = live.parent_path();
    const auto ext = live.extension().string();
    const auto self = Base36(SelfPid()), alive = Base36(AlivePid), dead = Base36(DeadPid);
    const std::string legacy = "live_input_live_1790000000000000000_0";
#if defined(_WIN32)
    const bool pdbSweep = true;
#else
    const bool pdbSweep = false; // PG~ PDB copies only exist on Windows
#endif
    const std::vector<std::string> swept = {"live_input_live_" + dead + "_0" + ext, "live_input_live_" + self + "_zz" + ext,
                                            legacy + ext, legacy + ".pdb"};
    const std::vector<std::string> sweptOnWindows = {"PG~" + dead + ".0.pdb", "PG~" + self + ".zz.pdb"};
    const std::vector<std::string> kept = {"live_input_live_" + alive + "_0" + ext, "PG~" + alive + ".0.pdb",
                                           "live_input_live_stale" + ext, "live_input_live_notes.txt",
                                           "live_input_live_1_0" + ext + ".bak", "live_input_live_1_0_extra" + ext,
                                           "live_input_live_" + dead + "_0.pdb", "live_input_live_Ab_0" + ext,
                                           "PG~notes.pdb", "PG~1.pdb", "PG~.pdb", "PG~notes.txt", "live_input_notes"};
    const std::vector<std::filesystem::path> unicode = {dir / std::filesystem::path(u8"live_input_live_\u65e5\u672c\U0001F642.txt"),
                                                        dir / std::filesystem::path(u8"\u65e5\u672c\U0001F642" + std::u8string(u8".pdb"))};
    for (const auto *names : {&swept, &sweptOnWindows, &kept})
        for (const auto &name : *names)
            std::ofstream(dir / name) << "shadow";
    for (const auto &path : unicode)
        std::ofstream(path) << "unicode";
    std::filesystem::copy_file(argv[1], live, std::filesystem::copy_options::overwrite_existing);
#if defined(_MSC_VER)
    std::filesystem::copy_file(std::filesystem::path(argv[1]).replace_extension(".pdb"),
                               std::filesystem::path(live).replace_extension(".pdb"),
                               std::filesystem::copy_options::overwrite_existing);
#endif
    Require(module.Stage(live)); // must not throw on the Unicode names
    module.Commit();
    for (const auto &name : swept)
        Require(!std::filesystem::exists(dir / name));
    for (const auto &name : sweptOnWindows)
        Require(std::filesystem::exists(dir / name) != pdbSweep);
    for (const auto *names : {&kept, &sweptOnWindows})
        for (const auto &name : *names)
        {
            Require(names == &sweptOnWindows || std::filesystem::exists(dir / name));
            std::filesystem::remove(dir / name);
        }
    for (const auto &path : unicode)
    {
        Require(std::filesystem::exists(path));
        std::filesystem::remove(path); // before this test's own directory walks, which use ANSI names
    }
    {
        // This process's copy is tagged with its pid.
        size_t copies = 0;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
            copies += entry.path().filename().string().rfind("live_input_live_" + self + "_", 0) == 0;
        Require(copies == 1);
    }
#if defined(_MSC_VER)
    {
        // The loaded copy names its own short, per-process PDB copy, not live_input.pdb.
        std::vector<std::filesystem::path> dlls, pdbs;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
        {
            const auto name = entry.path().filename().string();
            if (name.rfind("live_input_live_", 0) == 0)
                dlls.push_back(entry.path());
            else if (name.rfind("PG~" + self + ".", 0) == 0 && entry.path().extension() == ".pdb")
                pdbs.push_back(entry.path());
        }
        Require(dlls.size() == 1 && pdbs.size() == 1 && module.Notice().empty());
        const auto named = std::filesystem::path(CodeViewPath(ReadBytes(dlls[0])));
        Require(named == pdbs[0] || named == pdbs[0].filename());
    }
#endif
#if defined(_WIN32)
    {
        // A CodeView path too short for the shadow PDB name is left alone, with a notice, and nothing leaks.
        // Its own folder: a second module in one process may sweep the first one's PDB copy.
        const auto tinyDir = dir / "tiny_pdb";
        std::filesystem::remove_all(tinyDir);
        std::filesystem::create_directories(tinyDir);
        const auto tiny = tinyDir / ("tiny" + ext);
        const auto image = MakePeImage(true, "a.pdb");
        std::ofstream(tiny, std::ios::binary).write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        std::ofstream(tinyDir / "tiny.pdb") << "pdb";
        pe::ProjectNativeModule tinyModule;
        for (int i = 0; i < 2; ++i)
        {
            Require(!tinyModule.Stage(tiny)); // synthetic image: never loadable
            Require(tinyModule.Notice().find("PDB not isolated") != std::string::npos);
            Require(tinyModule.Notice().find("too short") != std::string::npos);
        }
        Require(tinyModule.NoticeCount() == 2); // repeated warnings are counted (and logged) every time
        Require(CountEntries(tinyDir) == 2);    // only tiny and tiny.pdb: its copy and PDB copy were removed
        std::filesystem::remove_all(tinyDir);
    }
#endif
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
    std::filesystem::remove(std::filesystem::path(live).replace_extension(".pdb"));
    for (const auto &entry : std::filesystem::directory_iterator(live.parent_path()))
        Require(entry.path().filename().string().find("_live_") == std::string::npos);
    std::puts("PASS: script paths, source lookup, guard, handle table, TRS, detached task, controller, installed in-place load, live-reload policy, append-only ABI, PDB patch, artifact status, in-place fallback, non-permission copy failure and retry, reload policy, shadow ownership and foreign-file safety, replacement, rejection, writable output, repeated reload, exceptions, stale-copy sweep, cleanup");
}
