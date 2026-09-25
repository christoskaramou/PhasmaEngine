#pragma once
#include "ProjectNative.h"
#include <chrono>
#include <filesystem>
#include <string>

namespace pe
{
    // Destroy active instances before Commit/Reset unloads their code.
    class ProjectNativeModule
    {
    public:
        ProjectNativeModule() = default;
        ~ProjectNativeModule();
        ProjectNativeModule(const ProjectNativeModule &) = delete;
        ProjectNativeModule &operator=(const ProjectNativeModule &) = delete;
        // Live hosts (the editor) poll and stage shadow copies; other hosts load in place exactly once.
        bool Sync(const std::filesystem::path &source, bool liveReload);
        bool Poll(const std::filesystem::path &source);
        bool Stage(const std::filesystem::path &source);
        bool Load(const std::filesystem::path &source);
        // Descriptors and callbacks must remain resident until Reset.
        bool StageLinked(const phasma::ScriptModule *api);
        void Commit();
        void Reset();
        const phasma::ScriptModule *Active() const { return m_active.api; }
        const std::string &Error() const { return m_error; }

    private:
        struct Image
        {
            void *library = nullptr;
            const phasma::ScriptModule *api = nullptr;
            std::filesystem::path path;
            bool ownsFile = false; // shadow copies are deleted on close; in-place loads never are
        };
        static void Close(Image &image);
        bool Open(const std::filesystem::path &path, bool ownsFile);
        bool Validate(const phasma::ScriptModule *api);
        Image m_active, m_candidate;
        std::string m_error;
        std::filesystem::file_time_type m_observed{}, m_attempted{};
        uintmax_t m_observedSize = 0, m_attemptedSize = 0;
        bool m_seen = false, m_tried = false;
        std::chrono::steady_clock::time_point m_nextPoll{};
    };
} // namespace pe
