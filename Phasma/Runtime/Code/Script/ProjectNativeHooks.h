#pragma once
#include "ProjectNative.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pe
{
    // Module state for the editor, Lua and agents. Artifacts are identified by build output size and write time.
    struct CppScriptStatus
    {
        uint64_t reloads = 0;          // modules committed
        uint64_t attempts = 0;         // artifacts tried, accepted or rejected (repeated identical rejections count)
        bool active = false;           // a module is running
        bool liveReload = false;       // this host polls and hot-swaps the module (not exports, static or fallback)
        uint32_t scriptCount = 0;      // descriptors in the active module
        std::string error;             // latest rejection; cleared by a commit
        std::string activeArtifact;    // identity of the running module
        std::string attemptedArtifact; // identity of the last artifact tried
    };

    enum class NativeReloadOutcome
    {
        Pending,     // no verdict on the built artifact yet
        Current,     // the built artifact was already running when the build started (no-op build)
        Reloaded,    // the built artifact is now running
        Rejected,    // the built artifact was tried and rejected
        NotObserved, // timed out without a verdict
    };

    // Destroy active instances before Commit/Reset unloads their code.
    class ProjectNativeModule
    {
    public:
        ProjectNativeModule() = default;
        ~ProjectNativeModule();
        ProjectNativeModule(const ProjectNativeModule &) = delete;
        ProjectNativeModule &operator=(const ProjectNativeModule &) = delete;
        // Editors and build-folder Players (NativeScripts.json beside the executable; exports omit it) live-reload.
        static bool LiveReloadEnabled(bool editorHost, const std::filesystem::path &executableDir);
        // Rewrites every PE CodeView (RSDS) PDB path in place; false (image untouched) if malformed or it does not fit.
        static bool PatchCodeViewPdbPath(std::vector<uint8_t> &image, std::string_view pdbPath);
        // PhasmaGame.dll / libPhasmaGame.so / libPhasmaGame.dylib.
        static const char *ModuleFileName();
        // "<size>:<write time>" of a build output, or empty if it cannot be read.
        static std::string ArtifactIdentity(const std::filesystem::path &file);
        // Correlates status with one build's artifact, so a delayed earlier build is never credited to it.
        static NativeReloadOutcome ClassifyReload(const CppScriptStatus &atBuildStart, const CppScriptStatus &now,
                                                  const std::string &builtArtifact, bool timedOut);
        // Shadow files are tagged with their process id; a file is stale if its process is gone.
        static bool ProcessAlive(uint32_t pid);
        // Live hosts poll and stage shadow copies; other hosts load in place exactly once.
        bool Sync(const std::filesystem::path &source, bool liveReload);
        bool Poll(const std::filesystem::path &source);
        // With expectedArtifact, refuses (without counting an attempt) if the file is no longer that artifact.
        bool Stage(const std::filesystem::path &source, const std::string *expectedArtifact = nullptr);
        bool Load(const std::filesystem::path &source);
        // Descriptors and callbacks must remain resident until Reset.
        bool StageLinked(const phasma::ScriptModule *api);
        void Commit();
        void Reset();
        const phasma::ScriptModule *Active() const { return m_active.api; }
        const std::string &Error() const { return m_error; }
        // Warning from the last stage/load (PDB not isolated, reload disabled); empty when there is none.
        const std::string &Notice() const { return m_notice; }
        uint64_t NoticeCount() const { return m_notices; } // every notice counts, identical text included
        CppScriptStatus Status() const;

    private:
        struct Image
        {
            void *library = nullptr;
            const phasma::ScriptModule *api = nullptr;
            std::filesystem::path path;
            bool ownsFile = false;     // shadow copies are deleted on close; in-place loads never are
            std::filesystem::path pdb; // owned shadow PDB (Windows)
            std::string artifact;
        };
        static void Close(Image &image);
        bool StagePdb(const std::filesystem::path &source, Image &image, const std::string &tag);
        void SweepStaleShadows(const std::filesystem::path &source) const;
        bool Open(const std::filesystem::path &path, bool ownsFile, std::string artifact);
        bool Validate(const phasma::ScriptModule *api);
        bool Reject(); // records a failed attempt; returns false
        void SetNotice(std::string notice);
        Image m_active, m_candidate;
        std::string m_error, m_notice, m_rejection, m_attemptedArtifact;
        uint64_t m_attempts = 0, m_commits = 0, m_notices = 0;
        bool m_copyFailed = false;    // last shadow copy was refused (permissions): in-place fallback candidate
        bool m_liveDisabled = false;  // in-place fallback turned reload off for this session
        bool m_livePolicy = false;    // the host asked for live reload on the last Sync
        bool m_copyTransient = false; // last shadow copy failed for another reason: retried with backoff
        std::chrono::milliseconds m_retryDelay{500};
        std::chrono::steady_clock::time_point m_retryAt{};
        std::filesystem::file_time_type m_observed{}, m_attempted{};
        uintmax_t m_observedSize = 0, m_attemptedSize = 0;
        bool m_seen = false, m_tried = false;
        std::chrono::steady_clock::time_point m_nextPoll{};
    };
} // namespace pe
