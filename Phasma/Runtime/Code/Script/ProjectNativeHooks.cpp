#include "ProjectNativeHooks.h"
#include "CppScriptPath.h"
#include <SDL.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_set>
#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#endif

namespace pe
{
    namespace
    {
        uint32_t CurrentPid()
        {
#if defined(_WIN32)
            return static_cast<uint32_t>(GetCurrentProcessId());
#else
            return static_cast<uint32_t>(getpid());
#endif
        }

        std::string Base36(uint64_t value)
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

        // "<pid36><separator><seq36>" -> pid; false for other shapes (older builds' tags, foreign files).
        bool ParseShadowPid(std::string_view tag, char separator, uint32_t &pid)
        {
            const size_t split = tag.find(separator);
            if (split == 0 || split == std::string_view::npos || split + 1 == tag.size())
                return false;
            uint64_t value = 0;
            for (size_t i = 0; i < tag.size(); ++i)
            {
                const char c = tag[i];
                const bool digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z');
                if (i == split || (i > split && digit))
                    continue;
                if (!digit)
                    return false;
                value = value * 36 + static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
                if (value > UINT32_MAX)
                    return false;
            }
            pid = static_cast<uint32_t>(value);
            return true;
        }

        constexpr std::string_view ShadowPdbPrefix = "PG~";

        // Only a refusal to create files justifies the in-place fallback; disk full, sharing violations, bad
        // names and other transient failures stay ordinary failures (loading in place would lock the build output).
        bool PermissionError(const std::error_code &ec)
        {
#if defined(_WIN32)
            if (ec.category() == std::system_category())
                return ec.value() == ERROR_ACCESS_DENIED || ec.value() == ERROR_WRITE_PROTECT;
#endif
            return ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted ||
                   ec == std::errc::read_only_file_system;
        }
    } // namespace

    ProjectNativeModule::~ProjectNativeModule()
    {
        Reset();
    }

    bool ProjectNativeModule::LiveReloadEnabled(bool editorHost, const std::filesystem::path &executableDir)
    {
        std::error_code ec;
        return editorHost || std::filesystem::is_regular_file(executableDir / "NativeScripts.json", ec);
    }

    const char *ProjectNativeModule::ModuleFileName()
    {
#if defined(_WIN32)
        return "PhasmaGame.dll";
#elif defined(__APPLE__)
        return "libPhasmaGame.dylib";
#else
        return "libPhasmaGame.so";
#endif
    }

    std::string ProjectNativeModule::ArtifactIdentity(const std::filesystem::path &file)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec)
            return {};
        const auto written = std::filesystem::last_write_time(file, ec);
        if (ec)
            return {};
        return std::to_string(size) + ":" + std::to_string(written.time_since_epoch().count());
    }

    NativeReloadOutcome ProjectNativeModule::ClassifyReload(const CppScriptStatus &atBuildStart, const CppScriptStatus &now,
                                                            const std::string &builtArtifact, bool timedOut)
    {
        if (!builtArtifact.empty() && now.activeArtifact == builtArtifact)
            return atBuildStart.activeArtifact == builtArtifact ? NativeReloadOutcome::Current : NativeReloadOutcome::Reloaded;
        if (!builtArtifact.empty() && now.attemptedArtifact == builtArtifact && !now.error.empty())
            return NativeReloadOutcome::Rejected;
        return timedOut ? NativeReloadOutcome::NotObserved : NativeReloadOutcome::Pending;
    }

    bool ProjectNativeModule::ProcessAlive(uint32_t pid)
    {
        if (pid == CurrentPid())
            return true;
#if defined(_WIN32)
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
            return GetLastError() == ERROR_ACCESS_DENIED; // exists, just not ours to query
        DWORD code = 0;
        const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
        CloseHandle(process);
        return alive;
#else
        if (pid == 0 || pid > INT32_MAX) // kill() treats these as process groups
            return false;
        return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
    }

    CppScriptStatus ProjectNativeModule::Status() const
    {
        CppScriptStatus status;
        status.reloads = m_commits;
        status.attempts = m_attempts;
        status.active = m_active.api != nullptr;
        status.liveReload = m_livePolicy && !m_liveDisabled;
        status.scriptCount = m_active.api ? m_active.api->scriptCount : 0u;
        status.error = m_rejection;
        status.activeArtifact = m_active.artifact;
        status.attemptedArtifact = m_attemptedArtifact;
        return status;
    }

    bool ProjectNativeModule::Reject()
    {
        m_rejection = m_error;
        Close(m_candidate);
        return false;
    }

    void ProjectNativeModule::Close(Image &image)
    {
        if (image.library)
            SDL_UnloadObject(image.library);
        std::error_code ec;
        if (image.ownsFile && !image.path.empty())
            std::filesystem::remove(image.path, ec);
        if (!image.pdb.empty())
            std::filesystem::remove(image.pdb, ec);
        image = {};
    }

    void ProjectNativeModule::Reset()
    {
        Close(m_candidate);
        Close(m_active);
        m_seen = m_tried = m_copyFailed = m_liveDisabled = m_livePolicy = false;
        m_nextPoll = {};
    }

    // Other hosts sharing the build folder (an editor and a Player) keep their files; a killed process's go.
    void ProjectNativeModule::SweepStaleShadows(const std::filesystem::path &source) const
    {
        const std::string dllPrefix = source.stem().string() + "_live_";
        const uint32_t self = CurrentPid();
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(source.parent_path(), ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            const auto &path = it->path();
            const std::string name = path.filename().string();
            std::string_view tag;
            char separator = '_';
            if (name.rfind(dllPrefix, 0) == 0)
                tag = std::string_view(name).substr(dllPrefix.size(), name.size() - dllPrefix.size() - path.extension().string().size());
            else if (name.rfind(ShadowPdbPrefix, 0) == 0 && path.extension() == ".pdb")
            {
                tag = std::string_view(name).substr(ShadowPdbPrefix.size(), name.size() - ShadowPdbPrefix.size() - 4);
                separator = '.';
            }
            else
                continue;
            if (path == m_active.path || path == m_active.pdb)
                continue;
            uint32_t pid = 0;
            if (ParseShadowPid(tag, separator, pid) && pid != self && ProcessAlive(pid))
                continue;
            std::error_code ignored;
            std::filesystem::remove(path, ignored); // a copy another process has loaded stays locked on Windows
        }
    }

    bool ProjectNativeModule::Stage(const std::filesystem::path &source)
    {
        Close(m_candidate);
        m_error.clear();
        m_notice.clear();
        m_copyFailed = false;
        static std::atomic<uint64_t> sequence{0};
        const std::string pid = Base36(CurrentPid()), seq = Base36(sequence++);
        const std::string artifact = ArtifactIdentity(source);
        ++m_attempts;
        m_attemptedArtifact = artifact;
        const auto path = source.parent_path() / (source.stem().string() + "_live_" + pid + "_" + seq + source.extension().string());
        SweepStaleShadows(source);
        std::error_code ec;
        m_candidate.path = path;
        m_candidate.ownsFile = true;
        std::filesystem::copy_file(source, path, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            m_error = "Copy failed: " + ec.message();
            m_copyFailed = PermissionError(ec);
            return Reject();
        }
        if (ArtifactIdentity(source) != artifact)
        {
            m_error = "Build output changed while it was being copied";
            return Reject();
        }
#if defined(_WIN32)
        StagePdb(source, m_candidate, pid + "." + seq);
#endif
        return Open(path, true, artifact);
    }

    // A debugger locks the PDB the loaded image names, so the next link of the original fails (LNK1201).
    // Give the shadow copy its own short, unique PDB; if that fails the copy still loads, naming the original.
    bool ProjectNativeModule::StagePdb(const std::filesystem::path &source, Image &image, const std::string &tag)
    {
        std::error_code ec;
        const auto pdb = std::filesystem::path(source).replace_extension(".pdb");
        if (!std::filesystem::is_regular_file(pdb, ec))
            return true; // nothing to isolate
        const auto warn = [&](const std::string &why)
        {
            m_notice = "PDB not isolated (" + why + "); a debugger attached to this process may lock " +
                       pdb.filename().string() + " and fail the next link";
            return false;
        };
        // Short enough to fit where the linker wrote "<dir>/PhasmaGame.pdb"; unique per process and reload.
        const auto copy = source.parent_path() / (std::string(ShadowPdbPrefix) + tag + ".pdb");
        if (!std::filesystem::copy_file(pdb, copy, std::filesystem::copy_options::none, ec))
            return warn("copy failed: " + ec.message());
        image.pdb = copy;
        std::vector<uint8_t> bytes;
        {
            std::ifstream in(image.path, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (PatchCodeViewPdbPath(bytes, copy.string()) || PatchCodeViewPdbPath(bytes, copy.filename().string()))
        {
            std::ofstream out(image.path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (out.flush())
                return true;
            return warn("could not rewrite the shadow module"); // copy may be damaged; Open() validates it
        }
        std::filesystem::remove(copy, ec);
        image.pdb.clear();
        return warn("the module has no CodeView record, or its PDB path is too short for " + copy.filename().string());
    }

    bool ProjectNativeModule::PatchCodeViewPdbPath(std::vector<uint8_t> &image, std::string_view pdbPath)
    {
        const uint64_t size = image.size();
        const auto has = [&](uint64_t offset, uint64_t bytes)
        { return offset <= size && bytes <= size - offset; };
        const auto u16 = [&](uint64_t at)
        { return static_cast<uint32_t>(image[at] | image[at + 1] << 8); };
        const auto u32 = [&](uint64_t at)
        { return u16(at) | u16(at + 2) << 16; };
        if (pdbPath.empty() || pdbPath.find('\0') != std::string_view::npos || !has(0, 0x40) || image[0] != 'M' ||
            image[1] != 'Z')
            return false;
        // PE signature, COFF file header, optional header (PE32 or PE32+) with the debug data directory (index 6).
        const uint64_t pe = u32(0x3C), optional = pe + 24;
        if (!has(pe, 24) || std::memcmp(&image[pe], "PE\0\0", 4) != 0)
            return false;
        const uint64_t sectionCount = u16(pe + 6), optionalSize = u16(pe + 20);
        if (optionalSize < 2 || !has(optional, optionalSize))
            return false;
        const uint32_t magic = u16(optional);
        const uint64_t directories = magic == 0x20b ? 112 : magic == 0x10b ? 96
                                                                           : 0;
        if (!directories || optionalSize < directories + 7 * 8 || u32(optional + directories - 4) < 7)
            return false;
        const uint64_t debugRva = u32(optional + directories + 48), debugSize = u32(optional + directories + 52);
        const uint64_t sections = optional + optionalSize;
        if (!has(sections, sectionCount * 40))
            return false;
        uint64_t debug = UINT64_MAX;
        for (uint64_t i = 0; i < sectionCount && debug == UINT64_MAX; ++i)
        {
            const uint64_t section = sections + i * 40, address = u32(section + 12);
            if (debugRva >= address && debugRva - address < u32(section + 16))
                debug = u32(section + 20) + (debugRva - address);
        }
        if (debug == UINT64_MAX || debugSize < 28 || !has(debug, debugSize))
            return false;
        // IMAGE_DEBUG_DIRECTORY entries of type CODEVIEW -> 'RSDS', GUID, age, NUL-terminated path.
        // Validate every record before writing any.
        std::vector<std::pair<uint64_t, uint64_t>> paths;
        for (uint64_t entry = debug; entry + 28 <= debug + debugSize; entry += 28)
        {
            if (u32(entry + 12) != 2)
                continue;
            const uint64_t record = u32(entry + 24), recordSize = u32(entry + 16);
            if (recordSize < 25 || !has(record, recordSize) || std::memcmp(&image[record], "RSDS", 4) != 0)
                return false;
            const auto *path = &image[record + 24];
            const auto *end = static_cast<const uint8_t *>(std::memchr(path, 0, recordSize - 24));
            if (!end || pdbPath.size() > static_cast<uint64_t>(end - path))
                return false;
            paths.emplace_back(record + 24, end - path);
        }
        if (paths.empty())
            return false;
        for (const auto &[offset, length] : paths)
        {
            std::memcpy(&image[offset], pdbPath.data(), pdbPath.size());
            std::memset(&image[offset + pdbPath.size()], 0, length - pdbPath.size());
        }
        return true;
    }

    bool ProjectNativeModule::Load(const std::filesystem::path &source)
    {
        Close(m_candidate);
        m_error.clear();
        std::string artifact = ArtifactIdentity(source);
        ++m_attempts;
        m_attemptedArtifact = artifact;
        return Open(source, false, std::move(artifact));
    }

    bool ProjectNativeModule::Open(const std::filesystem::path &path, bool ownsFile, std::string artifact)
    {
        m_candidate.path = path;
        m_candidate.ownsFile = ownsFile;
        m_candidate.artifact = std::move(artifact);
#if defined(_WIN32)
        DWORD previousErrorMode = 0;
        const bool changedErrorMode = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previousErrorMode) != 0;
#endif
        m_candidate.library = SDL_LoadObject(path.string().c_str());
#if defined(_WIN32)
        if (changedErrorMode)
            SetThreadErrorMode(previousErrorMode, nullptr);
#endif
        if (!m_candidate.library)
            m_error = SDL_GetError();
        else
        {
            const auto query = reinterpret_cast<phasma::GetScriptModule>(
                SDL_LoadFunction(m_candidate.library, "PhasmaGetScriptModule"));
            if (!query)
                m_error = "Missing PhasmaGetScriptModule";
            else
            {
                const auto *api = query(phasma::ScriptAbiVersion);
                if (Validate(api))
                    m_candidate.api = api;
            }
        }
        if (!m_candidate.api)
            return Reject();
        return true;
    }

    bool ProjectNativeModule::Validate(const phasma::ScriptModule *api)
    {
        if (!api || api->version < phasma::ScriptAbiMinVersion || api->version > phasma::ScriptAbiVersion ||
            api->size != sizeof(phasma::ScriptModule) || api->scriptCount > 4096 || (api->scriptCount && !api->scripts))
            m_error = "Incompatible script module ABI";
        else
        {
            std::unordered_set<std::string> names;
            std::unordered_set<std::string> sources;
            for (uint32_t i = 0; i < api->scriptCount; ++i)
            {
                const auto &script = api->scripts[i];
                if (script.kind == phasma::ScriptKind::Node && script.sourceFile &&
                    !sources.insert(CppSourceName(script.sourceFile)).second) // same rule scenes match by
                {
                    m_error = "Node scripts must have unique source filenames";
                    break;
                }
                if (!script.name || !*script.name || std::strlen(script.name) > 128 ||
                    !names.insert(script.name).second || !script.create || !script.destroy || !script.update ||
                    script.kind > phasma::ScriptKind::Node || script.mode > phasma::ScriptMode::Play ||
                    std::string(script.name).rfind("cpp:", 0) == 0)
                {
                    m_error = "Invalid or duplicate script descriptor";
                    break;
                }
            }
        }
        return m_error.empty();
    }

    bool ProjectNativeModule::StageLinked(const phasma::ScriptModule *api)
    {
        Close(m_candidate);
        m_error.clear();
        ++m_attempts;
        m_attemptedArtifact = "linked";
        if (!Validate(api))
            return Reject();
        m_candidate.api = api;
        m_candidate.artifact = m_attemptedArtifact;
        return true;
    }

    void ProjectNativeModule::Commit()
    {
        if (!m_candidate.api)
            return;
        Close(m_active);
        m_active = std::move(m_candidate);
        m_candidate = {};
        ++m_commits;
        m_rejection.clear();
    }

    bool ProjectNativeModule::Sync(const std::filesystem::path &source, bool liveReload)
    {
        m_livePolicy = liveReload;
        if (liveReload && !m_liveDisabled)
        {
            if (Poll(source))
                return true;
            // Initial load only: when no shadow copy can be made beside the build output, run it in place and
            // turn reload off for the session (a loaded in-place module would block rebuilds on Windows).
            // Never replaces a running module; a failed in-place load keeps polling for the next build.
            if (!m_copyFailed || m_active.api)
                return false;
            const std::string reason = m_error;
            m_copyFailed = false;
            if (!Load(source))
                return false;
            m_liveDisabled = true;
            m_notice = "Shadow copy failed (" + reason + "); loaded " + source.filename().string() +
                       " in place, live reload disabled for this session";
            return true;
        }
        // Shipped hosts: no polling, no shadow copy (install folders may be read-only), one attempt.
        if (m_tried)
            return false;
        m_tried = true;
        return Load(source);
    }

    bool ProjectNativeModule::Poll(const std::filesystem::path &source)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextPoll)
            return false;
        m_nextPoll = now + std::chrono::milliseconds(500);
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(source, ec);
        if (ec)
            return false;
        const auto size = std::filesystem::file_size(source, ec);
        if (ec)
            return false;
        if (!m_seen || stamp != m_observed || size != m_observedSize)
        {
            m_seen = true;
            m_observed = stamp;
            m_observedSize = size;
            return false;
        }
        if (m_tried && stamp == m_attempted && size == m_attemptedSize)
            return false;
        m_tried = true;
        m_attempted = stamp;
        m_attemptedSize = size;
        return Stage(source);
    }
} // namespace pe
