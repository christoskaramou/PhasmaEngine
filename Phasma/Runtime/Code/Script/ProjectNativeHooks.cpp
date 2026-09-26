#include "ProjectNativeHooks.h"
#include "CppScriptPath.h"
#include <SDL.h>
#include <algorithm>
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

        // Directory entries are compared in the platform's native encoding: converting an unrelated file's
        // name to the ANSI code page throws on Windows. Shadow-name pieces are ASCII.
        using NativeString = std::filesystem::path::string_type;
        using NativeChar = NativeString::value_type;

        NativeString Native(std::string_view ascii)
        {
            return NativeString(ascii.begin(), ascii.end());
        }

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.u8string();
            return std::string(reinterpret_cast<const char *>(text.data()), text.size());
        }

        std::string Identity(uintmax_t size, std::filesystem::file_time_type written)
        {
            return std::to_string(size) + ":" + std::to_string(written.time_since_epoch().count());
        }

        enum class ShadowTag
        {
            Foreign, // not a name this loader writes: never deleted
            Legacy,  // "<clock>_<seq>" from builds before shadow files carried a pid
            Pid,     // "<pid36><separator><seq36>"
        };

        // Validates a whole shadow tag, so only names the loader itself writes can ever be swept.
        ShadowTag ClassifyShadowTag(const NativeString &tag, NativeChar separator, uint32_t &pid)
        {
            const size_t split = tag.find(separator);
            if (split == 0 || split == NativeString::npos || split + 1 == tag.size() ||
                tag.find(separator, split + 1) != NativeString::npos)
                return ShadowTag::Foreign;
            uint64_t value = 0;
            bool overflow = false, decimal = true;
            for (size_t i = 0; i < tag.size(); ++i)
            {
                if (i == split)
                    continue;
                const NativeChar c = tag[i];
                const bool digit = c >= '0' && c <= '9', letter = c >= 'a' && c <= 'z';
                if (!digit && !letter)
                    return ShadowTag::Foreign;
                decimal = decimal && digit;
                if (i < split && !overflow)
                {
                    value = value * 36 + static_cast<uint64_t>(digit ? c - '0' : c - 'a' + 10);
                    overflow = value > UINT32_MAX;
                }
            }
            if (!overflow)
            {
                pid = static_cast<uint32_t>(value);
                return ShadowTag::Pid;
            }
            return decimal ? ShadowTag::Legacy : ShadowTag::Foreign; // legacy tags are a decimal clock count
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
        return Identity(size, written);
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
        m_seen = m_tried = m_copyFailed = m_copyTransient = m_liveDisabled = m_livePolicy = false;
        m_nextPoll = m_retryAt = {};
        m_retryDelay = std::chrono::milliseconds(500);
        m_error.clear();
        m_notice.clear();
        m_rejection.clear();
    }

    void ProjectNativeModule::SetNotice(std::string notice)
    {
        m_notice = std::move(notice);
        ++m_notices;
    }

    // Only names this loader writes are candidates: other hosts sharing the build folder (an editor and a
    // Player) keep their files, a killed process's files go, and anything else is left alone.
    void ProjectNativeModule::SweepStaleShadows(const std::filesystem::path &source) const
    {
        const NativeString dllPrefix = source.stem().native() + Native("_live_");
        const auto extension = source.extension();
        const uint32_t self = CurrentPid();
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(source.parent_path(), ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            const auto &path = it->path();
            const NativeString name = path.filename().native();
            const bool pdb = path.extension() == ".pdb";
            NativeString tag;
            NativeChar separator = '_';
            if (name.compare(0, dllPrefix.size(), dllPrefix) == 0 && (path.extension() == extension || pdb))
                tag = name.substr(dllPrefix.size(), name.size() - dllPrefix.size() - path.extension().native().size());
#if defined(_WIN32)
            else if (pdb && name.compare(0, ShadowPdbPrefix.size(), Native(ShadowPdbPrefix)) == 0)
            {
                tag = name.substr(ShadowPdbPrefix.size(), name.size() - ShadowPdbPrefix.size() - 4);
                separator = '.';
            }
#endif
            else
                continue;
            if (path == m_active.path || path == m_active.pdb)
                continue;
            uint32_t pid = 0;
            const ShadowTag kind = ClassifyShadowTag(tag, separator, pid);
            if (kind == ShadowTag::Foreign)
                continue;
            if (pdb && separator == '_' && kind != ShadowTag::Legacy)
                continue; // "<stem>_live_*.pdb" is only written by legacy builds
            if (kind == ShadowTag::Pid && pid != self && ProcessAlive(pid))
                continue;
            std::error_code ignored;
            std::filesystem::remove(path, ignored); // a copy another process has loaded stays locked on Windows
        }
    }

    bool ProjectNativeModule::Stage(const std::filesystem::path &source, const std::string *expectedArtifact)
    {
        Close(m_candidate);
        m_error.clear();
        m_notice.clear();
        m_copyFailed = m_copyTransient = false;
        const std::string artifact = ArtifactIdentity(source);
        if (expectedArtifact && artifact != *expectedArtifact)
        {
            // Not an attempt: the next poll observes the new artifact and stages that one.
            m_error = "Build output changed after it was observed";
            return false;
        }
        static std::atomic<uint64_t> sequence{0};
        const std::string pid = Base36(CurrentPid()), seq = Base36(sequence++);
        ++m_attempts;
        m_attemptedArtifact = artifact;
        const auto path = source.parent_path() /
                          (source.stem().native() + Native("_live_" + pid + "_" + seq) + source.extension().native());
        SweepStaleShadows(source);
        std::error_code ec;
        m_candidate.path = path;
        m_candidate.ownsFile = true;
        std::filesystem::copy_file(source, path, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            m_error = "Copy failed: " + ec.message();
            m_copyFailed = PermissionError(ec);
            m_copyTransient = !m_copyFailed;
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
            SetNotice("PDB not isolated (" + why + "); a debugger attached to this process may lock " +
                      Utf8(pdb.filename()) + " and fail the next link");
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
        if (PatchCodeViewPdbPath(bytes, Utf8(copy)) || PatchCodeViewPdbPath(bytes, Utf8(copy.filename())))
        {
            std::ofstream out(image.path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (out.flush())
                return true;
            return warn("could not rewrite the shadow module"); // copy may be damaged; Open() validates it
        }
        std::filesystem::remove(copy, ec);
        image.pdb.clear();
        return warn("the module has no CodeView record, or its PDB path is too short for " + Utf8(copy.filename()));
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
        m_candidate.library = SDL_LoadObject(Utf8(path).c_str()); // SDL takes UTF-8 on every platform
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
            {
                m_error = "Shadow copy failed (" + reason + ") and the in-place load failed: " + m_error;
                m_rejection = m_error;
                return false;
            }
            m_liveDisabled = true;
            SetNotice("Shadow copy failed (" + reason + "); loaded " + Utf8(source.filename()) +
                      " in place, live reload disabled for this session");
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
            m_retryDelay = std::chrono::milliseconds(500);
            return false;
        }
        // A tried artifact waits for the next build, except after a transient copy failure (a sharing
        // violation while an antivirus scans the new DLL, say): that is retried with backoff up to 30 s.
        if (m_tried && stamp == m_attempted && size == m_attemptedSize && (!m_copyTransient || now < m_retryAt))
            return false;
        m_tried = true;
        m_attempted = stamp;
        m_attemptedSize = size;
        const std::string observed = Identity(size, stamp);
        const bool staged = Stage(source, &observed); // refuses a file that changed since this observation
        if (m_copyTransient)
        {
            m_retryAt = now + m_retryDelay;
            m_retryDelay = (std::min)(m_retryDelay * 2, std::chrono::milliseconds(30000));
        }
        return staged;
    }
} // namespace pe
