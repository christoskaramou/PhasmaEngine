#include "ProjectNativeHooks.h"
#include <SDL.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_set>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace pe
{
    ProjectNativeModule::~ProjectNativeModule()
    {
        Reset();
    }

    bool ProjectNativeModule::LiveReloadEnabled(bool editorHost, const std::filesystem::path &executableDir)
    {
        std::error_code ec;
        return editorHost || std::filesystem::is_regular_file(executableDir / "NativeScripts.json", ec);
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
        m_seen = m_tried = false;
        m_nextPoll = {};
    }

    bool ProjectNativeModule::Stage(const std::filesystem::path &source)
    {
        Close(m_candidate);
        m_error.clear();
        static std::atomic<uint64_t> sequence{0};
        const auto tag = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto path = source.parent_path() / (source.stem().string() + "_live_" + std::to_string(tag) +
                                            "_" + std::to_string(sequence++) + source.extension().string());
        std::error_code ec;
        // Sweep copies left by killed processes; loaded copies (ours or another host's) stay locked on Windows.
        const auto prefix = source.stem().string() + "_live_";
        for (auto it = std::filesystem::directory_iterator(source.parent_path(), ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            std::error_code ignored;
            if (it->path().filename().string().rfind(prefix, 0) == 0 && it->path() != m_active.path &&
                it->path() != m_active.pdb)
                std::filesystem::remove(it->path(), ignored);
        }
        ec.clear();
        m_candidate.path = path;
        m_candidate.ownsFile = true;
        std::filesystem::copy_file(source, path, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            m_error = "Copy failed: " + ec.message();
            Close(m_candidate);
            return false;
        }
#if defined(_WIN32)
        StagePdb(source, m_candidate);
#endif
        return Open(path, true);
    }

    // A debugger locks the PDB the loaded image names, so the next link of the original fails (LNK1201).
    // Give the shadow copy its own PDB; if that fails the copy still loads, naming the original.
    void ProjectNativeModule::StagePdb(const std::filesystem::path &source, Image &image)
    {
        std::error_code ec;
        const auto pdb = std::filesystem::path(source).replace_extension(".pdb");
        const auto copy = std::filesystem::path(image.path).replace_extension(".pdb");
        if (!std::filesystem::is_regular_file(pdb, ec) ||
            !std::filesystem::copy_file(pdb, copy, std::filesystem::copy_options::none, ec))
            return;
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
            return;
        }
        std::filesystem::remove(copy, ec);
        image.pdb.clear();
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
        return Open(source, false);
    }

    bool ProjectNativeModule::Open(const std::filesystem::path &path, bool ownsFile)
    {
        m_candidate.path = path;
        m_candidate.ownsFile = ownsFile;
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
        {
            Close(m_candidate);
            return false;
        }
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
                    !sources.insert(std::filesystem::path(script.sourceFile).filename().string()).second)
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
        if (!Validate(api))
            return false;
        m_candidate.api = api;
        return true;
    }

    void ProjectNativeModule::Commit()
    {
        if (!m_candidate.api)
            return;
        Close(m_active);
        m_active = std::move(m_candidate);
        m_candidate = {};
    }

    bool ProjectNativeModule::Sync(const std::filesystem::path &source, bool liveReload)
    {
        if (liveReload)
            return Poll(source);
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
