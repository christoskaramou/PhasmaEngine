#include "ProjectNativeHooks.h"
#include <SDL.h>
#include <atomic>
#include <cstring>
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

    void ProjectNativeModule::Close(Image &image)
    {
        if (image.library)
            SDL_UnloadObject(image.library);
        if (image.ownsFile && !image.path.empty())
        {
            std::error_code ec;
            std::filesystem::remove(image.path, ec);
        }
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
            if (it->path().filename().string().rfind(prefix, 0) == 0 && it->path() != m_active.path)
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
        return Open(path, true);
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
        if (!api || api->version != phasma::ScriptAbiVersion || api->size != sizeof(phasma::ScriptModule) ||
            api->scriptCount > 4096 || (api->scriptCount && !api->scripts))
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
