#include "Script/ScriptSystem.h"

#if defined(PE_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pe
{
    // LAN play for scene scripts: ONE TCP link between two machines, plus UDP broadcast so a
    // game can be found without typing an address (runtime_ui has no text entry, so discovery
    // is the only workable join flow).
    //
    // The Lua state opens only base/math/string/table/coroutine — no io, no os — so a script
    // cannot do this itself. Everything here is non-blocking: read_line returns nil rather than
    // waiting, exactly like proc.read_line, and connect completes over later frames.
    //
    // This binding moves BYTES and never interprets them. The caps below are the trust boundary
    // it does enforce, because a peer that never sends a newline must not be able to grow our
    // buffer forever:
    //   - a line longer than kMaxLine, or a buffer past kMaxBuffer with no newline, drops the link
    //   - datagrams over kMaxDatagram are truncated
    // Everything above that — is this a legal move, is it even this peer's turn — is the caller's
    // job, and the caller must do it against its own game state before touching anything.
    //
    // ponytail: exactly ONE connection, like proc's one child. Two players is one link; spectators
    // and lobbies with three parties are a relay feature, and the relay does not exist yet.
    namespace
    {
#if defined(PE_WIN32)
        using socket_t = SOCKET;
        constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
        using socket_t = int;
        constexpr socket_t kInvalidSocket = -1;
#endif
        constexpr size_t kMaxLine = 512;     // one protocol line; a move is a dozen bytes
        constexpr size_t kMaxBuffer = 8192;  // unparsed backlog before we give up on the peer
        constexpr size_t kMaxDatagram = 256; // one discovery beacon

        void CloseSocket(socket_t &s)
        {
            if (s == kInvalidSocket)
                return;
#if defined(PE_WIN32)
            closesocket(s);
#else
            ::close(s);
#endif
            s = kInvalidSocket;
        }

        bool WouldBlock()
        {
#if defined(PE_WIN32)
            const int e = WSAGetLastError();
            return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
#else
            return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
        }

        void SetNonBlocking(socket_t s)
        {
#if defined(PE_WIN32)
            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);
#else
            const int flags = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
#endif
        }

        void EnsureStack()
        {
#if defined(PE_WIN32)
            // Function-local static: no WSACleanup, deliberately. Static destruction order is not
            // guaranteed, and tearing down Winsock while a socket is still open is worse than
            // letting process exit reclaim it.
            static const bool ready = []
            {
                WSADATA data{};
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            (void)ready;
#endif
        }

        std::string SocketError(const char *what)
        {
#if defined(PE_WIN32)
            return std::string(what) + " failed (" + std::to_string(WSAGetLastError()) + ")";
#else
            return std::string(what) + " failed (" + std::to_string(errno) + ")";
#endif
        }

        class LanLink
        {
        public:
            ~LanLink() { CloseAll(); }

            // ── TCP ────────────────────────────────────────────────────────────────────
            bool Host(uint16_t port, std::string &error)
            {
                CloseAll();
                EnsureStack();
                m_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (m_listen == kInvalidSocket)
                {
                    error = SocketError("socket");
                    return false;
                }
                int yes = 1;
                setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
                SetNonBlocking(m_listen);

                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = INADDR_ANY;
                addr.sin_port = htons(port);
                if (bind(m_listen, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
                    listen(m_listen, 2) != 0)
                {
                    error = SocketError("bind/listen");
                    CloseSocket(m_listen);
                    return false;
                }
                m_state = State::Listening;
                return true;
            }

            // True once a peer is connected. Any further caller is closed immediately: this link
            // holds one game, and a second connection would silently play against the wrong peer.
            bool Accept()
            {
                if (m_state == State::Connected)
                    return true;
                if (m_listen == kInvalidSocket)
                    return false;
                sockaddr_in from{};
#if defined(PE_WIN32)
                int len = sizeof(from);
#else
                socklen_t len = sizeof(from);
#endif
                const socket_t s = accept(m_listen, reinterpret_cast<sockaddr *>(&from), &len);
                if (s == kInvalidSocket)
                    return false;
                if (m_conn != kInvalidSocket)
                {
                    socket_t extra = s;
                    CloseSocket(extra);
                    return true;
                }
                m_conn = s;
                SetNonBlocking(m_conn);
                NoDelay(m_conn);
                m_peer = Ipv4Text(from);
                m_state = State::Connected;
                CloseSocket(m_listen); // the game is full; stop advertising a door that is shut
                return true;
            }

            bool Join(const std::string &host, uint16_t port, std::string &error)
            {
                CloseAll();
                EnsureStack();
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
                {
                    error = "not an IPv4 address: " + host;
                    return false;
                }
                m_conn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (m_conn == kInvalidSocket)
                {
                    error = SocketError("socket");
                    return false;
                }
                SetNonBlocking(m_conn);
                NoDelay(m_conn);
                m_peer = host;
                if (connect(m_conn, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
                {
                    m_state = State::Connected;
                    return true;
                }
                if (!WouldBlock())
                {
                    error = SocketError("connect");
                    CloseSocket(m_conn);
                    return false;
                }
                // Still handshaking. Status() finishes it on a later frame rather than stalling
                // this one for however long an unreachable host takes to time out.
                m_state = State::Connecting;
                return true;
            }

            const char *Status()
            {
                if (m_state == State::Connecting)
                    PollConnect();
                else if (m_state == State::Connected)
                    Drain(); // a peer that hung up is only discovered by reading, and a status
                             // that still says "connected" after the socket died is a trap
                switch (m_state)
                {
                case State::Listening:
                    return "listening";
                case State::Connecting:
                    return "connecting";
                case State::Connected:
                    return "connected";
                default:
                    return "idle";
                }
            }

            bool Send(const std::string &line)
            {
                if (m_state != State::Connected || m_conn == kInvalidSocket)
                    return false;
                if (line.size() > kMaxLine)
                    return false;
                std::string out = line;
                out.push_back('\n');
                size_t sent = 0;
                while (sent < out.size())
                {
                    const int n = send(m_conn, out.data() + sent, static_cast<int>(out.size() - sent), 0);
                    if (n > 0)
                    {
                        sent += static_cast<size_t>(n);
                        continue;
                    }
                    if (n < 0 && WouldBlock())
                        continue; // a move is a few dozen bytes; the buffer drains immediately
                    Drop();
                    return false;
                }
                return true;
            }

            // One buffered line, or nullopt when none is complete. Never blocks. Poll in a loop
            // until it returns nullopt, the way bot.lua drains Stockfish.
            std::optional<std::string> ReadLine()
            {
                Drain();
                const size_t nl = m_buffer.find('\n');
                if (nl == std::string::npos)
                {
                    // No newline and the buffer is already past what any honest peer sends: this
                    // is either a broken client or someone trying to make us allocate.
                    if (m_buffer.size() > kMaxBuffer)
                        Drop();
                    return std::nullopt;
                }
                std::string line = m_buffer.substr(0, nl);
                m_buffer.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.size() > kMaxLine)
                {
                    Drop();
                    return std::nullopt;
                }
                return line;
            }

            const std::string &Peer() const { return m_peer; }

            void CloseAll()
            {
                CloseSocket(m_listen);
                CloseSocket(m_conn);
                m_buffer.clear();
                m_peer.clear();
                m_state = State::Idle;
            }

            // ── UDP discovery ──────────────────────────────────────────────────────────
            bool Discover(uint16_t port, std::string &error)
            {
                if (m_udp != kInvalidSocket && m_udpPort == port)
                    return true;
                CloseSocket(m_udp);
                EnsureStack();
                m_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (m_udp == kInvalidSocket)
                {
                    error = SocketError("socket");
                    return false;
                }
                int yes = 1;
                setsockopt(m_udp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
                setsockopt(m_udp, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&yes), sizeof(yes));
                SetNonBlocking(m_udp);

                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = INADDR_ANY;
                addr.sin_port = htons(port);
                if (bind(m_udp, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
                {
                    error = SocketError("bind");
                    CloseSocket(m_udp);
                    return false;
                }
                m_udpPort = port;
                return true;
            }

            bool Advertise(uint16_t port, const std::string &text, std::string &error)
            {
                if (!Discover(port, error)) // the same socket sends and receives
                    return false;
                sockaddr_in to{};
                to.sin_family = AF_INET;
                to.sin_port = htons(port);
                to.sin_addr.s_addr = INADDR_BROADCAST;
                const std::string payload = text.substr(0, kMaxDatagram);
                return sendto(m_udp, payload.data(), static_cast<int>(payload.size()), 0,
                              reinterpret_cast<sockaddr *>(&to), sizeof(to)) >= 0;
            }

            bool PollDiscover(std::string &text, std::string &from)
            {
                if (m_udp == kInvalidSocket)
                    return false;
                char buf[kMaxDatagram];
                sockaddr_in addr{};
#if defined(PE_WIN32)
                int len = sizeof(addr);
#else
                socklen_t len = sizeof(addr);
#endif
                const int n = recvfrom(m_udp, buf, static_cast<int>(sizeof(buf)), 0,
                                       reinterpret_cast<sockaddr *>(&addr), &len);
                if (n <= 0)
                    return false;
                text.assign(buf, static_cast<size_t>(n));
                from = Ipv4Text(addr);
                return true;
            }

            void DiscoverStop()
            {
                CloseSocket(m_udp);
                m_udpPort = 0;
            }

        private:
            enum class State
            {
                Idle,
                Listening,
                Connecting,
                Connected
            };

            static void NoDelay(socket_t s)
            {
                int yes = 1;
                setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&yes), sizeof(yes));
            }

            static std::string Ipv4Text(const sockaddr_in &addr)
            {
                char text[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text));
                return text;
            }

            void PollConnect()
            {
                if (m_conn == kInvalidSocket)
                    return;
                fd_set write{}, except{};
                FD_ZERO(&write);
                FD_ZERO(&except);
                FD_SET(m_conn, &write);
                FD_SET(m_conn, &except);
                timeval now{0, 0}; // poll, never wait: this runs inside a frame
#if defined(PE_WIN32)
                const int ready = select(0, nullptr, &write, &except, &now);
#else
                const int ready = select(static_cast<int>(m_conn) + 1, nullptr, &write, &except, &now);
#endif
                if (ready <= 0)
                    return;
                if (FD_ISSET(m_conn, &except))
                {
                    Drop();
                    return;
                }
                // Writable means the handshake finished, but SO_ERROR is what says whether it
                // finished by connecting or by being refused.
                int err = 0;
#if defined(PE_WIN32)
                int len = sizeof(err);
#else
                socklen_t len = sizeof(err);
#endif
                getsockopt(m_conn, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &len);
                if (err != 0)
                    Drop();
                else
                    m_state = State::Connected;
            }

            void Drain()
            {
                if (m_state != State::Connected || m_conn == kInvalidSocket)
                    return;
                char chunk[1024];
                for (;;)
                {
                    const int n = recv(m_conn, chunk, static_cast<int>(sizeof(chunk)), 0);
                    if (n > 0)
                    {
                        m_buffer.append(chunk, static_cast<size_t>(n));
                        continue;
                    }
                    if (n == 0)
                        Drop(); // orderly close by the peer
                    else if (!WouldBlock())
                        Drop();
                    return;
                }
            }

            void Drop()
            {
                CloseSocket(m_conn);
                m_buffer.clear();
                m_state = State::Idle;
            }

            std::string m_buffer;
            std::string m_peer;
            socket_t m_listen = kInvalidSocket;
            socket_t m_conn = kInvalidSocket;
            socket_t m_udp = kInvalidSocket;
            uint16_t m_udpPort = 0;
            State m_state = State::Idle;
        };

        LanLink &Link()
        {
            static LanLink link;
            return link;
        }
    } // namespace

    static struct NetBindings
    {
        NetBindings()
        {
            ScriptSystem::AddBindings(
                [](sol::state &lua)
                {
                    sol::table net = lua.create_named_table("net");

                    // net.host(port) -> true | false, error
                    net.set_function("host",
                                     [](int port, sol::this_state ts) -> std::tuple<bool, sol::object>
                                     {
                                         sol::state_view view(ts);
                                         std::string error;
                                         if (port <= 0 || port > 65535)
                                             return {false, sol::make_object(view, "port out of range")};
                                         if (!Link().Host(static_cast<uint16_t>(port), error))
                                             return {false, sol::make_object(view, error)};
                                         return {true, sol::nil};
                                     });

                    // net.join(ip, port) -> true | false, error. Completes over later frames;
                    // poll net.status() until it reads "connected".
                    net.set_function("join",
                                     [](const std::string &host, int port,
                                        sol::this_state ts) -> std::tuple<bool, sol::object>
                                     {
                                         sol::state_view view(ts);
                                         std::string error;
                                         if (port <= 0 || port > 65535)
                                             return {false, sol::make_object(view, "port out of range")};
                                         if (!Link().Join(host, static_cast<uint16_t>(port), error))
                                             return {false, sol::make_object(view, error)};
                                         return {true, sol::nil};
                                     });

                    net.set_function("accept", []()
                                     { return Link().Accept(); });
                    net.set_function("status", []()
                                     { return std::string(Link().Status()); });
                    net.set_function("peer", []()
                                     { return Link().Peer(); });
                    net.set_function("send", [](const std::string &line)
                                     { return Link().Send(line); });

                    net.set_function("read_line",
                                     [](sol::this_state ts) -> sol::object
                                     {
                                         sol::state_view view(ts);
                                         std::optional<std::string> line = Link().ReadLine();
                                         return line ? sol::make_object(view, *line) : sol::make_object(view, sol::nil);
                                     });

                    net.set_function("close", []()
                                     { Link().CloseAll(); });

                    // net.advertise(port, text): one broadcast datagram. Call it about once a
                    // second while hosting — a beacon nobody repeats is a lobby nobody finds.
                    net.set_function("advertise",
                                     [](int port, const std::string &text)
                                     {
                                         std::string error;
                                         return port > 0 && port <= 65535 &&
                                                Link().Advertise(static_cast<uint16_t>(port), text, error);
                                     });

                    net.set_function("discover",
                                     [](int port, sol::this_state ts) -> std::tuple<bool, sol::object>
                                     {
                                         sol::state_view view(ts);
                                         std::string error;
                                         if (port <= 0 || port > 65535)
                                             return {false, sol::make_object(view, "port out of range")};
                                         if (!Link().Discover(static_cast<uint16_t>(port), error))
                                             return {false, sol::make_object(view, error)};
                                         return {true, sol::nil};
                                     });

                    // net.poll_discover() -> text, ip | nil
                    net.set_function("poll_discover",
                                     [](sol::this_state ts) -> std::tuple<sol::object, sol::object>
                                     {
                                         sol::state_view view(ts);
                                         std::string text, from;
                                         if (!Link().PollDiscover(text, from))
                                             return {sol::nil, sol::nil};
                                         return {sol::make_object(view, text), sol::make_object(view, from)};
                                     });

                    net.set_function("discover_stop", []()
                                     { Link().DiscoverStop(); });
                });
        }
    } g_netBindings;
} // namespace pe
