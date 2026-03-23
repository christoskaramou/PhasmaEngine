#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <memory>

namespace pagent
{
    // Manages ollama server and model pulling.
    // Cross-platform: uses fork/exec on POSIX, CreateProcess on Windows.
    // RAII: destructor terminates the server process if still running.
    class OllamaProcess
    {
    public:
        OllamaProcess() = default;
        ~OllamaProcess();

        OllamaProcess(const OllamaProcess &) = delete;
        OllamaProcess &operator=(const OllamaProcess &) = delete;
        OllamaProcess(OllamaProcess &&other) noexcept;
        OllamaProcess &operator=(OllamaProcess &&other) noexcept;

        // Start "ollama serve" in the background.
        // Stops any previously running server first.
        void Start(const std::string &model);

        // Terminate the running server (if any).
        void Stop();

        // Pull a model in the background. Calls progressCb with status messages.
        // Calls completeCb(true/false) when done.
        // Thread-safe: spawns its own thread.
        // Returns a cancel token - set it to true to abort the download.
        using ProgressCallback = std::function<void(const std::string &status)>;
        using CompleteCallback = std::function<void(bool success)>;
        using CancelToken = std::shared_ptr<std::atomic<bool>>;
        static CancelToken Pull(const std::string &model,
                                ProgressCallback progressCb,
                                CompleteCallback completeCb);

        // Cancel an in-progress pull via its token.
        static void CancelPull(const CancelToken &token);

        bool IsRunning() const { return m_handle != InvalidHandle; }

        // Unload a model from GPU memory. Blocking HTTP call.
        static void UnloadModel(const std::string &model, const std::string &base_url = "");

    private:
#ifdef _WIN32
        using HandleType = void *; // HANDLE
        static constexpr HandleType InvalidHandle = nullptr;
#else
        using HandleType = int; // pid_t
        static constexpr HandleType InvalidHandle = -1;
#endif
        HandleType m_handle = InvalidHandle;
    };
} // namespace pagent
