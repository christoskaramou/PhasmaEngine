#pragma once

#include "Base/ProfilerSnapshot.h"

#include <cstdint>

namespace pe
{
    enum class ProfilerRefreshRate : uint8_t
    {
        Hz4,
        Hz10,
        Hz30,
        Hz60,
        PerFrame,
    };

    // Loopback live profiler: Player/Editor pushes snapshot frames; PhasmaProfiler pulls them.
    // Server to client: little-endian uint32 length + UTF-8 JSON payload.
    // Client to server: one ProfilerRefreshRate byte.
    // ponytail: one client; reconnect replaces; GPU timing on only while a client is connected.
    class ProfilerStreamServer
    {
    public:
        static constexpr int kDefaultPort = 9876;

        ProfilerStreamServer() = default;
        ~ProfilerStreamServer() { Stop(); }

        ProfilerStreamServer(const ProfilerStreamServer &) = delete;
        ProfilerStreamServer &operator=(const ProfilerStreamServer &) = delete;

        // Binds 127.0.0.1:port and starts accept thread. Registers AfterCommandWait for GPU samples.
        bool Start(int port = kDefaultPort);
        void Stop();

        bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }
        bool HasClient() const { return m_clientFd.load(std::memory_order_relaxed) >= 0; }
        int GetPort() const { return m_port; }

        // Call once per frame after Profiler::EndFrame() (and after WaitPreviousFrameCommands).
        // Publishes at the connected viewer's requested rate (4 Hz by default).
        // Enables GPU timing only while a client is connected.
        void Tick();

    private:
        void AcceptLoop();
        void CloseClient();
        void PollClientCommands();
        bool SendFrame(const std::string &json);

        int m_port = kDefaultPort;
        std::atomic<bool> m_running{false};
        std::atomic<std::intptr_t> m_listenFd{-1};
        std::atomic<std::intptr_t> m_clientFd{-1};
        std::thread m_acceptThread;
        EventSystem::CallbackToken m_gpuToken{};

        std::mutex m_gpuMutex;
        std::vector<GpuTimerSample> m_gpuSamples;
        std::vector<GpuTimerSample> m_latestGpuSamples;
        std::vector<ProfilerFrameSample> m_pendingFrames;

        Timer m_publishTimer;
        std::atomic<double> m_publishIntervalSeconds{0.25};
        bool m_firstPublish = true;
        bool m_gpuTimingOn = false;
    };

    class ProfilerStreamClient
    {
    public:
        ProfilerStreamClient() = default;
        ~ProfilerStreamClient() { Disconnect(); }

        ProfilerStreamClient(const ProfilerStreamClient &) = delete;
        ProfilerStreamClient &operator=(const ProfilerStreamClient &) = delete;

        bool Connect(const char *host = "127.0.0.1", int port = ProfilerStreamServer::kDefaultPort);
        void Disconnect();
        bool IsConnected() const { return m_fd >= 0; }

        // Requests the Player's detailed-snapshot cadence. Non-blocking.
        bool SetRefreshRate(ProfilerRefreshRate rate);

        // Non-blocking: returns true when a full frame was read into outJson.
        bool TryRecvFrame(std::string &outJson);

    private:
        std::intptr_t m_fd = -1;
        std::vector<uint8_t> m_buf;
        size_t m_have = 0;
        uint32_t m_pendingLen = 0;
        bool m_haveLen = false;
    };

    // Parse --profiler / --profiler-port N / PE_PROFILER env. Returns nullopt if disabled.
    std::optional<int> ParseProfilerStreamPortArg(int argc, char *argv[]);
} // namespace pe
