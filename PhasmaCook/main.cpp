// PhasmaCook — desktop mesh-cook tool. The ONLY engine target that links Assimp.
//
// Imports source models (glTF/FBX/OBJ/...) through Assimp and writes portable, GPU-ready,
// Assimp-free cooked meshes (".pemesh"). A Vulkan device is brought up because the importer's
// UploadGpu() builds the cooked CPU streams (and uploads textures) through the engine's GPU path;
// cooking reads that CPU-side data back out into the ".pemesh". The window is created HIDDEN so the
// tool never flashes a black surface when invoked headlessly (editor import, cook_model.py).
//
// Modes (dispatched in main):
//   PhasmaCook <source> <output.pemesh>   one-shot cook (editor single import / cook_model.py)
//   PhasmaCook --batch <manifest>         cook many models in ONE process (one device bring-up);
//                                         manifest is UTF-8 text, one "<src>\t<out>" per line
//   PhasmaCook                            open the interactive cook window (CookUI.cpp)
//
// Everything that needs to cook shells out to this exe, so Assimp stays out of the runtime, editor,
// and player binaries.

#include "API/RHI.h"
#include "Base/EventSystem.h"
#include "Base/Log.h"
#include "Base/Path.h"
#include "Runtime/RuntimeHost.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetAssimp.h"
#include "Scene/ModelAssetCooked.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
#else
#include <cstring>
#endif

namespace
{
    // UTF-8-safe path display for log/format strings (path::string() throws on Windows for paths
    // with no active-code-page mapping; u8string() always succeeds).
    std::string PathUtf8(const std::filesystem::path &p)
    {
        const auto u8 = p.u8string();
        return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
    }

    std::filesystem::path PathFromUtf8(const std::string &s)
    {
        return std::filesystem::path(reinterpret_cast<const char8_t *>(s.c_str()));
    }

    // Brings up a HIDDEN Vulkan device once; CookOne() imports a source model and writes a ".pemesh".
    // Reused for both the single cook and the batch cook so a whole folder pays one device bring-up.
    class CookSession
    {
    public:
        CookSession()
        {
            pe::RuntimeWindowDesc desc;
            desc.title = "PhasmaCook";
            desc.api = PE_GRAPHICS_API_VULKAN;
            desc.flags = SDL_WINDOW_HIDDEN; // no visible surface — cooking never presents a frame
            desc.showAfterCreate = false;
            desc.maximizeAfterCreate = false;
            desc.pumpEventsAfterCreate = false;
            m_window = std::make_unique<pe::RuntimeWindow>(desc);
            // initializeSwapchain stays true: the engine sizes deletion queues (and more) from the
            // swapchain image count, so device bring-up is monolithic — dropping the swapchain
            // crashes the texture-upload path. A truly headless cook needs an engine-side change.
            m_rhi = std::make_unique<pe::RuntimeRhiSession>(m_window->Get(), PE_GRAPHICS_API_VULKAN, true);
        }

        ~CookSession()
        {
            // Free default GPU resources before the RHI session (m_rhi) tears the device down.
            pe::ModelAsset::DestroyDefaults();
        }

        CookSession(const CookSession &) = delete;
        CookSession &operator=(const CookSession &) = delete;

        // Returns an empty string on success, else a short failure reason. MUST NOT call PE_ERROR
        // (which throws in this engine): a model Assimp rejects has to be skipped, not crash the
        // whole batch — that was the cause of "cooked 80/104" (one bad model killed the rest).
        std::string CookOne(const std::filesystem::path &src, const std::filesystem::path &out)
        {
            try
            {
                pe::ModelAsset *model = pe::ModelAssetAssimp::Load(src);
                if (!model)
                {
                    PE_WARN("[Cook] Failed to import source model: %s", PathUtf8(src).c_str());
                    return "import failed (model rejected or unreadable)";
                }
                std::error_code ec;
                std::filesystem::create_directories(out.parent_path(), ec);
                const bool ok = pe::ModelAssetCooked::WriteToFile(model, out);
                delete model;
                if (!ok)
                {
                    PE_WARN("[Cook] Failed to write cooked mesh: %s", PathUtf8(out).c_str());
                    return "failed to write .pemesh";
                }
                return {};
            }
            catch (const std::exception &e)
            {
                PE_WARN("[Cook] Exception cooking '%s': %s", PathUtf8(src).c_str(), e.what());
                return e.what();
            }
            catch (...)
            {
                // Assimp can throw non-std types on malformed assets; contain everything.
                PE_WARN("[Cook] Unknown exception cooking '%s'", PathUtf8(src).c_str());
                return "unknown exception";
            }
        }

    private:
        pe::RuntimeSdlSession m_sdl; // SDL_Init; destroyed last
        std::unique_ptr<pe::RuntimeWindow> m_window;
        std::unique_ptr<pe::RuntimeRhiSession> m_rhi;
    };

    int RunCook(const std::filesystem::path &src, const std::filesystem::path &out)
    {
        pe::Path::Init();
        pe::Log::Init();
        PE_INFO("[Cook] Importing '%s' -> '%s'", PathUtf8(src).c_str(), PathUtf8(out).c_str());

        pe::EventSystem::Init();
        int result = 1;
        try
        {
            CookSession session;
            result = session.CookOne(src, out).empty() ? 0 : 1;
        }
        catch (const std::exception &e)
        {
            PE_WARN("[Cook] Cook session error: %s", e.what());
        }
        catch (...)
        {
            PE_WARN("[Cook] Unknown cook session error");
        }
        pe::EventSystem::Destroy();
        return result;
    }

    // Cook many models in one device bring-up. Manifest is UTF-8 text, one "<src>\t<out>" per line.
    int RunBatch(const std::filesystem::path &manifestPath)
    {
        pe::Path::Init();
        pe::Log::Init();

        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> jobs;
        {
            std::ifstream in(manifestPath, std::ios::binary);
            if (!in)
            {
                PE_WARN("[Cook] Cannot read batch manifest: %s", PathUtf8(manifestPath).c_str());
                return 2;
            }
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const size_t tab = line.find('\t');
                if (tab == std::string::npos)
                    continue;
                std::string s = line.substr(0, tab);
                std::string o = line.substr(tab + 1);
                if (s.empty() || o.empty())
                    continue;
                jobs.emplace_back(PathFromUtf8(s), PathFromUtf8(o));
            }
        }
        if (jobs.empty())
        {
            PE_WARN("[Cook] Batch manifest had no jobs: %s", PathUtf8(manifestPath).c_str());
            return 0;
        }

        PE_INFO("[Cook] Batch cooking %zu model(s)", jobs.size());
        pe::EventSystem::Init();
        std::vector<std::pair<std::filesystem::path, std::string>> failures; // (src, reason)
        // `processed` is visible to the catch so a session that cannot start (e.g. device/window
        // init throws) marks every not-yet-attempted job failed — never silently "all ok".
        size_t processed = 0;
        try
        {
            CookSession session;
            for (; processed < jobs.size(); ++processed)
            {
                const auto &job = jobs[processed];
                const std::string err = session.CookOne(job.first, job.second);
                if (err.empty())
                    PE_INFO("[Cook] (%zu/%zu) ok: %s", processed + 1, jobs.size(), PathUtf8(job.second).c_str());
                else
                    failures.emplace_back(job.first, err);
            }
        }
        catch (const std::exception &e)
        {
            PE_WARN("[Cook] Cook session error: %s", e.what());
            for (size_t i = processed; i < jobs.size(); ++i)
                failures.emplace_back(jobs[i].first, std::string("cook session error: ") + e.what());
        }
        catch (...)
        {
            PE_WARN("[Cook] Unknown cook session error");
            for (size_t i = processed; i < jobs.size(); ++i)
                failures.emplace_back(jobs[i].first, "unknown cook session error");
        }
        pe::EventSystem::Destroy();

        // Sidecar "<manifest>.failed": one "<src>\t<reason>" per failure, so the editor/UI can show
        // exactly which models failed and why. Removed when there are no failures.
        std::filesystem::path failedPath = manifestPath;
        failedPath += ".failed";
        std::error_code ec;
        if (failures.empty())
        {
            std::filesystem::remove(failedPath, ec);
        }
        else
        {
            std::ofstream out(failedPath, std::ios::binary | std::ios::trunc);
            for (const auto &f : failures)
                out << PathUtf8(f.first) << '\t' << f.second << '\n';
        }

        PE_INFO("[Cook] Batch done: %zu ok, %zu failed", jobs.size() - failures.size(), failures.size());
        return failures.empty() ? 0 : 1;
    }
} // namespace

namespace pe
{
    // The interactive cook window (CookUI.cpp). Opened when no cook arguments are given.
    int RunCookUI();
} // namespace pe

int main(int argc, char *argv[])
{
#if defined(_WIN32)
    // Read the wide command line so Unicode model/output paths survive (narrow argv is the active
    // code page on Windows and mangles non-ASCII names). SDL2main routes WinMain -> this main.
    std::vector<std::wstring> args;
    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv)
    {
        for (int i = 0; i < wargc; ++i)
            args.emplace_back(wargv[i]);
        LocalFree(wargv);
    }

    if (args.size() >= 3 && args[1] == L"--batch")
        return RunBatch(std::filesystem::path(args[2]));
    if (args.size() >= 3)
        return RunCook(std::filesystem::path(args[1]), std::filesystem::path(args[2]));
    return pe::RunCookUI();
#else
    // POSIX argv is UTF-8; build paths from char8_t so they are not re-decoded as a narrow locale.
    if (argc >= 3 && std::strcmp(argv[1], "--batch") == 0)
        return RunBatch(std::filesystem::path(reinterpret_cast<const char8_t *>(argv[2])));
    if (argc >= 3)
        return RunCook(std::filesystem::path(reinterpret_cast<const char8_t *>(argv[1])),
                       std::filesystem::path(reinterpret_cast<const char8_t *>(argv[2])));
    return pe::RunCookUI();
#endif
}
