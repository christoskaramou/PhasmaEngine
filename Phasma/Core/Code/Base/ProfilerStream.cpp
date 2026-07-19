#include "Base/ProfilerSnapshot.h"
#include "Base/ProfilerStream.h"
#include "API/RHI.h"

#if defined(PE_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketFd = SOCKET;
static constexpr SocketFd kInvalidSocket = INVALID_SOCKET;
static int CloseSocketFd(SocketFd fd)
{
    return closesocket(fd);
}
static int SetNonBlocking(SocketFd fd)
{
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
}
static int LastSocketError()
{
    return WSAGetLastError();
}
static bool WouldBlock(int err)
{
    return err == WSAEWOULDBLOCK;
}
static bool ConnectInProgress(int err)
{
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketFd = int;
static constexpr SocketFd kInvalidSocket = -1;
static int CloseSocketFd(SocketFd fd)
{
    return close(fd);
}
static int SetNonBlocking(SocketFd fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
static int LastSocketError()
{
    return errno;
}
static bool WouldBlock(int err)
{
    return err == EAGAIN || err == EWOULDBLOCK;
}
static bool ConnectInProgress(int err)
{
    return err == EINPROGRESS || WouldBlock(err);
}
#endif

namespace pe
{
    namespace
    {
#if defined(PE_WIN32)
        std::once_flag g_wsaOnce;
        void EnsureWsa()
        {
            std::call_once(g_wsaOnce, []
                           {
                WSADATA data{};
                WSAStartup(MAKEWORD(2, 2), &data); });
        }
#else
        void EnsureWsa() {}
#endif

        std::intptr_t ToStoredFd(SocketFd fd)
        {
            return static_cast<std::intptr_t>(fd);
        }

        SocketFd FromStoredFd(std::intptr_t fd)
        {
            if (fd < 0)
                return kInvalidSocket;
            return static_cast<SocketFd>(fd);
        }

        bool SendAll(SocketFd fd, const void *data, size_t n)
        {
            const char *p = static_cast<const char *>(data);
            size_t left = n;
            while (left > 0)
            {
                const int sent = static_cast<int>(send(fd, p, static_cast<int>(left), 0));
                if (sent <= 0)
                    return false;
                p += sent;
                left -= static_cast<size_t>(sent);
            }
            return true;
        }

        double RefreshIntervalSeconds(ProfilerRefreshRate rate)
        {
            switch (rate)
            {
            case ProfilerRefreshRate::Hz4:
                return 0.25;
            case ProfilerRefreshRate::Hz10:
                return 0.1;
            case ProfilerRefreshRate::Hz30:
                return 1.0 / 30.0;
            case ProfilerRefreshRate::Hz60:
                return 1.0 / 60.0;
            case ProfilerRefreshRate::PerFrame:
                return 0.0;
            }
            return 0.25;
        }
    } // namespace

    bool ProfilerStreamServer::Start(int port)
    {
        if (m_running.load())
            return true;

        EnsureWsa();
        m_port = std::clamp(port > 0 ? port : kDefaultPort, 1, 65535);

        SocketFd listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenFd == kInvalidSocket)
        {
            Log::Error("ProfilerStream: socket() failed");
            return false;
        }

        int yes = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(m_port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (bind(listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            Log::Error("ProfilerStream: bind(127.0.0.1:" + std::to_string(m_port) + ") failed");
            CloseSocketFd(listenFd);
            return false;
        }
        if (listen(listenFd, 1) != 0)
        {
            Log::Error("ProfilerStream: listen() failed");
            CloseSocketFd(listenFd);
            return false;
        }

        m_listenFd.store(ToStoredFd(listenFd));
        m_running.store(true);
        m_firstPublish = true;
        m_publishTimer.Start();

        m_gpuToken = EventSystem::RegisterCallbackWithToken(
            EventType::AfterCommandWait,
            [this](const std::any &data)
            {
                try
                {
                    const auto &samples = std::any_cast<const std::vector<GpuTimerSample> &>(data);
                    if (samples.empty())
                        return;
                    std::lock_guard lock(m_gpuMutex);
                    m_gpuSamples = samples;
                }
                catch (const std::bad_any_cast &)
                {
                }
            });

        m_acceptThread = std::thread([this]
                                     { AcceptLoop(); });
        Log::Info("ProfilerStream: listening on 127.0.0.1:" + std::to_string(m_port));
        return true;
    }

    void ProfilerStreamServer::Stop()
    {
        if (!m_running.exchange(false))
            return;

        EventSystem::UnregisterCallback(EventType::AfterCommandWait, m_gpuToken);
        m_gpuToken = 0;

        Debug::SetGpuTimingEnabled(false);
        m_gpuTimingOn = false;

        const SocketFd listenFd = FromStoredFd(m_listenFd.exchange(-1));
        if (listenFd != kInvalidSocket)
            CloseSocketFd(listenFd);
        CloseClient();

        if (m_acceptThread.joinable())
            m_acceptThread.join();

        std::lock_guard lock(m_gpuMutex);
        m_gpuSamples.clear();
        m_latestGpuSamples.clear();
        m_pendingFrames.clear();
    }

    void ProfilerStreamServer::CloseClient()
    {
        const SocketFd fd = FromStoredFd(m_clientFd.exchange(-1));
        if (fd != kInvalidSocket)
            CloseSocketFd(fd);
    }

    void ProfilerStreamServer::AcceptLoop()
    {
        while (m_running.load())
        {
            const SocketFd listenFd = FromStoredFd(m_listenFd.load());
            if (listenFd == kInvalidSocket)
                break;

            sockaddr_in clientAddr{};
#if defined(PE_WIN32)
            int len = sizeof(clientAddr);
#else
            socklen_t len = sizeof(clientAddr);
#endif
            const SocketFd client = accept(listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &len);
            if (client == kInvalidSocket)
            {
                if (!m_running.load())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // One client: drop previous.
            CloseClient();
            m_publishIntervalSeconds.store(0.25, std::memory_order_relaxed);
            m_clientFd.store(ToStoredFd(client));
            Log::Info("ProfilerStream: client connected");
        }
    }

    void ProfilerStreamServer::PollClientCommands()
    {
        const SocketFd fd = FromStoredFd(m_clientFd.load());
        if (fd == kInvalidSocket)
            return;

        for (;;)
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(fd, &readSet);
            timeval timeout{};
            if (select(static_cast<int>(fd + 1), &readSet, nullptr, nullptr, &timeout) <= 0)
                return;

            uint8_t value = 0;
            const int received = static_cast<int>(recv(fd, reinterpret_cast<char *>(&value), 1, 0));
            if (received != 1)
            {
                CloseClient();
                return;
            }

            if (value <= static_cast<uint8_t>(ProfilerRefreshRate::PerFrame))
            {
                const auto rate = static_cast<ProfilerRefreshRate>(value);
                m_publishIntervalSeconds.store(RefreshIntervalSeconds(rate), std::memory_order_relaxed);
            }
            else if (value >= kProfilerRenderDocCaptureBase &&
                     value < kProfilerRenderDocCaptureBase + kProfilerMaxRenderDocCaptureFrames)
            {
                const uint32_t frameCount = value - kProfilerRenderDocCaptureBase + 1;
                if (!Debug::IsCaptureApiAvailable())
                    Log::Warn("ProfilerStream: RenderDoc capture requested, but RenderDoc is not available");
                else
                {
                    Log::Info("ProfilerStream: requesting RenderDoc capture for " + std::to_string(frameCount) + " frame(s)");
                    Debug::TriggerMultiFrameCapture(frameCount);
                }
            }
        }
    }

    bool ProfilerStreamServer::SendFrame(const std::string &json)
    {
        const SocketFd fd = FromStoredFd(m_clientFd.load());
        if (fd == kInvalidSocket)
            return false;

        const uint32_t len = static_cast<uint32_t>(json.size());
        uint8_t header[4] = {
            static_cast<uint8_t>(len & 0xff),
            static_cast<uint8_t>((len >> 8) & 0xff),
            static_cast<uint8_t>((len >> 16) & 0xff),
            static_cast<uint8_t>((len >> 24) & 0xff),
        };
        if (!SendAll(fd, header, 4) || !SendAll(fd, json.data(), json.size()))
        {
            CloseClient();
            return false;
        }
        return true;
    }

    void ProfilerStreamServer::Tick()
    {
        if (!m_running.load())
            return;

        const bool hasClient = HasClient();
        if (hasClient != m_gpuTimingOn)
        {
            m_gpuTimingOn = hasClient;
            Debug::SetGpuTimingEnabled(hasClient);
            if (!hasClient)
            {
                std::lock_guard lock(m_gpuMutex);
                m_gpuSamples.clear();
                m_latestGpuSamples.clear();
                m_pendingFrames.clear();
            }
        }

        if (!hasClient)
            return;

        PollClientCommands();
        if (!HasClient())
            return;

        {
            std::lock_guard lock(m_gpuMutex);
            if (!m_gpuSamples.empty())
            {
                m_latestGpuSamples = std::move(m_gpuSamples);
                m_gpuSamples.clear();
            }
        }

        float gpuTotalMs = 0.f;
        for (const auto &sample : m_latestGpuSamples)
        {
            if (sample.depth == 0)
                gpuTotalMs += sample.timeMs;
        }

        FrameTimer &frameTimer = FrameTimer::Instance();
        ProfilerFrameSample frameSample;
        frameSample.frameMs = Profiler::GetFrameTimeMs();
        frameSample.cpuTotalMs = static_cast<float>(MILLI(frameTimer.GetCpuTotal()));
        frameSample.cpuUpdateMs = static_cast<float>(MILLI(frameTimer.GetUpdatesStamp()));
        frameSample.cpuDrawMs = frameSample.cpuTotalMs - frameSample.cpuUpdateMs;
        frameSample.gpuTotalMs = gpuTotalMs;
        m_pendingFrames.push_back(frameSample);
        if (m_pendingFrames.size() > 256)
            m_pendingFrames.erase(m_pendingFrames.begin());

        const double publishInterval = m_publishIntervalSeconds.load(std::memory_order_relaxed);
        if (!m_firstPublish && publishInterval > 0.0 && m_publishTimer.Count() < publishInterval)
            return;
        m_publishTimer.Start();
        m_firstPublish = false;

        ProfilerSnapshot snapshot = ProfilerSnapshot::Gather(m_latestGpuSamples);
        snapshot.frameHistory = std::move(m_pendingFrames);
        m_pendingFrames.clear();
        float sampledMs = 0.f;
        for (const ProfilerFrameSample &sample : snapshot.frameHistory)
            sampledMs += sample.frameMs;
        if (sampledMs > 0.f)
            snapshot.fps = 1000.f * static_cast<float>(snapshot.frameHistory.size()) / sampledMs;
        const std::string json = snapshot.ToJson();
        SendFrame(json);
    }

    bool ProfilerStreamClient::Connect(const char *host, int port)
    {
        Disconnect();
        EnsureWsa();

        SocketFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == kInvalidSocket)
            return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port > 0 ? port : ProfilerStreamServer::kDefaultPort));
        if (inet_pton(AF_INET, host && host[0] ? host : "127.0.0.1", &addr.sin_addr) != 1)
        {
            CloseSocketFd(fd);
            return false;
        }

        if (SetNonBlocking(fd) != 0)
        {
            CloseSocketFd(fd);
            return false;
        }

        if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            if (!ConnectInProgress(LastSocketError()))
            {
                CloseSocketFd(fd);
                return false;
            }

            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(fd, &writeSet);
            // ponytail: the stream is loopback-only, so bound this UI-thread attempt instead of adding async state.
            timeval timeout{0, 10000};
            if (select(static_cast<int>(fd + 1), nullptr, &writeSet, nullptr, &timeout) <= 0)
            {
                CloseSocketFd(fd);
                return false;
            }

            int connectError = 0;
#if defined(PE_WIN32)
            int optionLength = sizeof(connectError);
#else
            socklen_t optionLength = sizeof(connectError);
#endif
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&connectError), &optionLength) != 0 ||
                connectError != 0)
            {
                CloseSocketFd(fd);
                return false;
            }
        }

        m_fd = ToStoredFd(fd);
        m_buf.clear();
        m_have = 0;
        m_pendingLen = 0;
        m_haveLen = false;
        return true;
    }

    void ProfilerStreamClient::Disconnect()
    {
        if (m_fd < 0)
            return;
        CloseSocketFd(FromStoredFd(m_fd));
        m_fd = -1;
        m_buf.clear();
        m_have = 0;
        m_haveLen = false;
    }

    bool ProfilerStreamClient::SetRefreshRate(ProfilerRefreshRate rate)
    {
        return SendCommand(static_cast<uint8_t>(rate));
    }

    bool ProfilerStreamClient::TriggerRenderDocCapture(uint8_t frameCount)
    {
        frameCount = std::clamp<uint8_t>(frameCount, 1, kProfilerMaxRenderDocCaptureFrames);
        return SendCommand(static_cast<uint8_t>(kProfilerRenderDocCaptureBase + frameCount - 1));
    }

    bool ProfilerStreamClient::SendCommand(uint8_t value)
    {
        if (m_fd < 0)
            return false;

        const int sent = static_cast<int>(send(FromStoredFd(m_fd), reinterpret_cast<const char *>(&value), 1, 0));
        if (sent == 1)
            return true;
        if (sent < 0 && WouldBlock(LastSocketError()))
            return false;

        Disconnect();
        return false;
    }

    bool ProfilerStreamClient::TryRecvFrame(std::string &outJson)
    {
        if (m_fd < 0)
            return false;

        const SocketFd fd = FromStoredFd(m_fd);
        uint8_t tmp[4096];
        for (;;)
        {
            const int n = static_cast<int>(recv(fd, reinterpret_cast<char *>(tmp), sizeof(tmp), 0));
            if (n > 0)
            {
                m_buf.insert(m_buf.end(), tmp, tmp + n);
                continue;
            }
            if (n == 0)
            {
                Disconnect();
                return false;
            }
            const int err = LastSocketError();
            if (WouldBlock(err))
                break;
            Disconnect();
            return false;
        }

        while (true)
        {
            if (!m_haveLen)
            {
                if (m_buf.size() < 4)
                    return false;
                m_pendingLen = static_cast<uint32_t>(m_buf[0]) |
                               (static_cast<uint32_t>(m_buf[1]) << 8) |
                               (static_cast<uint32_t>(m_buf[2]) << 16) |
                               (static_cast<uint32_t>(m_buf[3]) << 24);
                m_buf.erase(m_buf.begin(), m_buf.begin() + 4);
                m_haveLen = true;
                if (m_pendingLen > 16u * 1024u * 1024u)
                {
                    Disconnect();
                    return false;
                }
            }

            if (m_buf.size() < m_pendingLen)
                return false;

            outJson.assign(reinterpret_cast<const char *>(m_buf.data()), m_pendingLen);
            m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_pendingLen));
            m_haveLen = false;
            m_pendingLen = 0;
            return true;
        }
    }

    std::optional<int> ParseProfilerStreamPortArg(int argc, char *argv[])
    {
        int port = ProfilerStreamServer::kDefaultPort;
        bool enabled = false;

        if (const char *env = std::getenv("PE_PROFILER"))
        {
            if (env[0] != '\0' && std::strcmp(env, "0") != 0 && std::strcmp(env, "false") != 0)
            {
                enabled = true;
                if (std::isdigit(static_cast<unsigned char>(env[0])))
                    port = std::atoi(env);
            }
        }

        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--profiler") == 0)
            {
                enabled = true;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    port = std::atoi(argv[++i]);
                    if (port <= 0)
                        port = ProfilerStreamServer::kDefaultPort;
                }
            }
            else if (std::strncmp(argv[i], "--profiler=", 11) == 0)
            {
                enabled = true;
                port = std::atoi(argv[i] + 11);
                if (port <= 0)
                    port = ProfilerStreamServer::kDefaultPort;
            }
            else if (std::strcmp(argv[i], "--profiler-port") == 0 && i + 1 < argc)
            {
                enabled = true;
                port = std::atoi(argv[++i]);
                if (port <= 0)
                    port = ProfilerStreamServer::kDefaultPort;
            }
        }

        if (!enabled)
            return std::nullopt;
        return std::clamp(port, 1, 65535);
    }
} // namespace pe
