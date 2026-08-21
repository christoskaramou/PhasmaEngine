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

#if defined(PE_TLS)
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h> // for the MBEDTLS_ERR_NET_* codes the BIO callbacks return
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

namespace pe
{
    // Play against another machine from a scene script: ONE TCP link, plus UDP broadcast so a
    // game on the same network can be found without typing an address (runtime_ui has no text
    // entry, so discovery is the only workable join flow there).
    //
    // Over the internet the same link dials OUT to a relay instead, and net.join takes a pin:
    // the connection is then TLS and the server's public key must hash to exactly that pin, or
    // nothing is sent. Client side only — this binding never terminates TLS itself.
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
    // ponytail: exactly ONE connection, like proc's one child. Two players is one link; a third
    // party (a spectator) would be a change at the relay, not here.
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

#if defined(PE_TLS)
        // TLS, client side only. The game never terminates TLS: LAN play is plain TCP between
        // two machines that can already see each other, and internet play dials OUT to a relay
        // that does the terminating (Chess/relay/). So there is no server certificate here, no
        // listener changes, and no CA bundle to ship — because the caller pins the relay's
        // public key instead of trusting the public PKI. A pinned key is strictly stronger: a
        // certificate signed by a real CA for the same name is still refused.
        //
        // Everything is non-blocking, like the rest of this file: the handshake advances a bit
        // per frame from Status(), and no application byte is read or written until the pin has
        // been checked.
        class TlsChannel
        {
        public:
            TlsChannel()
            {
                mbedtls_ssl_init(&m_ssl);
                mbedtls_ssl_config_init(&m_conf);
                mbedtls_ctr_drbg_init(&m_drbg);
                mbedtls_entropy_init(&m_entropy);
            }

            ~TlsChannel()
            {
                mbedtls_ssl_free(&m_ssl);
                mbedtls_ssl_config_free(&m_conf);
                mbedtls_ctr_drbg_free(&m_drbg);
                mbedtls_entropy_free(&m_entropy);
            }

            TlsChannel(const TlsChannel &) = delete;
            TlsChannel &operator=(const TlsChannel &) = delete;

            bool Begin(socket_t &sock, const std::string &host, const std::string &pin, std::string &error)
            {
                m_sock = &sock;
                m_pin = pin;
                static const char kSeed[] = "phasma-net-tls";
                if (mbedtls_ctr_drbg_seed(&m_drbg, mbedtls_entropy_func, &m_entropy,
                                          reinterpret_cast<const unsigned char *>(kSeed), sizeof(kSeed) - 1) != 0)
                {
                    error = "no entropy for TLS";
                    return false;
                }
                if (mbedtls_ssl_config_defaults(&m_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                MBEDTLS_SSL_PRESET_DEFAULT) != 0)
                {
                    error = "TLS config failed";
                    return false;
                }
                // VERIFY_NONE is not "no verification": the pin below is the verification, and it
                // is done before a single application byte moves. Chain validation would only add
                // the public PKI's opinion about a self-signed relay we already know by key.
                mbedtls_ssl_conf_authmode(&m_conf, MBEDTLS_SSL_VERIFY_NONE);
                mbedtls_ssl_conf_rng(&m_conf, mbedtls_ctr_drbg_random, &m_drbg);
                mbedtls_ssl_conf_min_tls_version(&m_conf, MBEDTLS_SSL_VERSION_TLS1_2);
                if (mbedtls_ssl_setup(&m_ssl, &m_conf) != 0)
                {
                    error = "TLS setup failed";
                    return false;
                }
                mbedtls_ssl_set_hostname(&m_ssl, host.c_str()); // SNI only; the pin is what decides
                mbedtls_ssl_set_bio(&m_ssl, m_sock, BioSend, BioRecv, nullptr);
                return true;
            }

            // 0 = still handshaking, 1 = up and pinned, -1 = refused (error says why)
            int Handshake(std::string &error)
            {
                const int rc = mbedtls_ssl_handshake(&m_ssl);
                if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                    return 0;
                if (rc != 0)
                {
                    error = "TLS handshake failed (" + std::to_string(-rc) + ")";
                    return -1;
                }
                if (!PinMatches(error))
                    return -1;
                return 1;
            }

            // Same contract as recv(): >0 bytes, 0 would-block, -1 gone.
            int Read(char *buf, size_t len)
            {
                const int rc = mbedtls_ssl_read(&m_ssl, reinterpret_cast<unsigned char *>(buf), len);
                if (rc > 0)
                    return rc;
                if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                    return 0;
                return -1;
            }

            int Write(const char *buf, size_t len)
            {
                const int rc = mbedtls_ssl_write(&m_ssl, reinterpret_cast<const unsigned char *>(buf), len);
                if (rc > 0)
                    return rc;
                if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                    return 0;
                return -1;
            }

            void Bye() { mbedtls_ssl_close_notify(&m_ssl); }

        private:
            static int BioSend(void *ctx, const unsigned char *buf, size_t len)
            {
                const socket_t s = *static_cast<socket_t *>(ctx);
                const int n = send(s, reinterpret_cast<const char *>(buf), static_cast<int>(len), 0);
                if (n >= 0)
                    return n;
                return WouldBlock() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
            }

            static int BioRecv(void *ctx, unsigned char *buf, size_t len)
            {
                const socket_t s = *static_cast<socket_t *>(ctx);
                const int n = recv(s, reinterpret_cast<char *>(buf), static_cast<int>(len), 0);
                if (n > 0)
                    return n;
                if (n == 0)
                    return MBEDTLS_ERR_NET_CONN_RESET; // the peer hung up mid-stream
                return WouldBlock() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
            }

            // SHA-256 of the peer's SubjectPublicKeyInfo, base64 — the same string make_cert.sh
            // prints, so the pin in the game config can be compared byte for byte.
            bool PinMatches(std::string &error)
            {
                const mbedtls_x509_crt *crt = mbedtls_ssl_get_peer_cert(&m_ssl);
                if (crt == nullptr)
                {
                    error = "the server sent no certificate";
                    return false;
                }
                unsigned char der[1024];
                // mbedtls writes the DER at the END of the buffer and returns its length.
                const int len = mbedtls_pk_write_pubkey_der(const_cast<mbedtls_pk_context *>(&crt->pk), der,
                                                            sizeof(der));
                if (len <= 0)
                {
                    error = "could not read the server key";
                    return false;
                }
                unsigned char digest[32];
                if (mbedtls_sha256(der + sizeof(der) - len, static_cast<size_t>(len), digest, 0) != 0)
                {
                    error = "could not hash the server key";
                    return false;
                }
                unsigned char b64[64];
                size_t written = 0;
                if (mbedtls_base64_encode(b64, sizeof(b64), &written, digest, sizeof(digest)) != 0)
                {
                    error = "could not encode the server key";
                    return false;
                }
                const std::string got(reinterpret_cast<char *>(b64), written);
                if (got != m_pin)
                {
                    // Loud on purpose: this is either the wrong server or the relay's key was
                    // replaced, and both mean "do not play here".
                    error = "the server key does not match the pin (got " + got + ")";
                    return false;
                }
                return true;
            }

            mbedtls_ssl_context m_ssl{};
            mbedtls_ssl_config m_conf{};
            mbedtls_ctr_drbg_context m_drbg{};
            mbedtls_entropy_context m_entropy{};
            socket_t *m_sock = nullptr;
            std::string m_pin;
        };
#endif

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

            // `pin` non-empty means TLS: the link is wrapped before any byte moves and the
            // server's key must hash to exactly that pin. Empty means plain TCP, which is what
            // LAN play uses between two machines on the same wire.
            bool Join(const std::string &host, uint16_t port, const std::string &pin, std::string &error)
            {
                CloseAll();
                EnsureStack();
                m_lastError.clear();
#if !defined(PE_TLS)
                if (!pin.empty())
                {
                    error = "this build has no TLS support";
                    return false;
                }
#endif
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 && !ResolveIpv4(host, addr, error))
                    return false;
                m_pin = pin;
                m_host = host;
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
                    return StartSecureOrConnected(error);
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
                if (m_state == State::Handshaking)
                    PollHandshake();
                else if (m_state == State::Connected)
                    Drain(); // a peer that hung up is only discovered by reading, and a status
                             // that still says "connected" after the socket died is a trap
                switch (m_state)
                {
                case State::Listening:
                    return "listening";
                case State::Connecting:
                case State::Handshaking:
                    return "connecting"; // the caller waits for "connected" either way
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
                    const int n = Push(out.data() + sent, out.size() - sent);
                    if (n > 0)
                    {
                        sent += static_cast<size_t>(n);
                        continue;
                    }
                    if (n == 0)
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

            // Why the last link did not come up — a pin mismatch reads very differently from
            // "nobody answered", and the player is the one who has to act on the difference.
            const std::string &LastError() const { return m_lastError; }

            void CloseAll()
            {
#if defined(PE_TLS)
                if (m_tls && m_state == State::Connected)
                    m_tls->Bye();
                m_tls.reset();
#endif
                CloseSocket(m_listen);
                CloseSocket(m_conn);
                m_buffer.clear();
                m_peer.clear();
                m_host.clear();
                m_pin.clear();
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
                Connecting,  // TCP
                Handshaking, // TLS, and the pin is checked before this ends
                Connected
            };

            // Accepts a hostname as well as a literal address, because a relay moves house and a
            // pinned key does not care what it is called. This blocks the frame for as long as
            // the lookup takes — once, on a click, which is cheaper than an async DNS resolver.
            static bool ResolveIpv4(const std::string &host, sockaddr_in &addr, std::string &error)
            {
                addrinfo hints{};
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                addrinfo *found = nullptr;
                if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr)
                {
                    error = "cannot find " + host;
                    return false;
                }
                addr.sin_addr = reinterpret_cast<sockaddr_in *>(found->ai_addr)->sin_addr;
                freeaddrinfo(found);
                return true;
            }

            // The TCP link is up. With a pin, nothing is "connected" until TLS agrees.
            bool StartSecureOrConnected(std::string &error)
            {
                if (m_pin.empty())
                {
                    m_state = State::Connected;
                    return true;
                }
#if defined(PE_TLS)
                m_tls = std::make_unique<TlsChannel>();
                if (!m_tls->Begin(m_conn, m_host, m_pin, error))
                {
                    m_lastError = error;
                    Drop();
                    return false;
                }
                m_state = State::Handshaking;
                return true;
#else
                error = "this build has no TLS support";
                m_lastError = error;
                Drop();
                return false;
#endif
            }

            void PollHandshake()
            {
#if defined(PE_TLS)
                if (!m_tls)
                {
                    Drop();
                    return;
                }
                std::string error;
                const int rc = m_tls->Handshake(error);
                if (rc == 1)
                    m_state = State::Connected;
                else if (rc < 0)
                {
                    m_lastError = error;
                    Drop();
                }
#else
                Drop();
#endif
            }

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
                {
                    m_lastError = "could not reach " + m_peer;
                    Drop();
                    return;
                }
                std::string error;
                StartSecureOrConnected(error);
            }

            // The two places bytes actually cross the wire, so TLS is folded in here and nowhere
            // else. Both keep recv()'s contract: >0 moved, 0 would-block, <0 the link is gone.
            int Push(const char *data, size_t len)
            {
#if defined(PE_TLS)
                if (m_tls)
                    return m_tls->Write(data, len);
#endif
                const int n = send(m_conn, data, static_cast<int>(len), 0);
                if (n >= 0)
                    return n;
                return WouldBlock() ? 0 : -1;
            }

            int Pull(char *data, size_t len)
            {
#if defined(PE_TLS)
                if (m_tls)
                    return m_tls->Read(data, len);
#endif
                const int n = recv(m_conn, data, static_cast<int>(len), 0);
                if (n > 0)
                    return n;
                if (n == 0)
                    return -1; // orderly close by the peer
                return WouldBlock() ? 0 : -1;
            }

            void Drain()
            {
                if (m_state != State::Connected || m_conn == kInvalidSocket)
                    return;
                char chunk[1024];
                for (;;)
                {
                    const int n = Pull(chunk, sizeof(chunk));
                    if (n > 0)
                    {
                        m_buffer.append(chunk, static_cast<size_t>(n));
                        continue;
                    }
                    if (n < 0)
                        Drop();
                    return;
                }
            }

            void Drop()
            {
#if defined(PE_TLS)
                m_tls.reset();
#endif
                CloseSocket(m_conn);
                // The buffer deliberately SURVIVES: a peer that says why it is leaving and then
                // hangs up sends both in the same breath, and throwing the line away here would
                // turn "no game with that code" into a mystery disconnect. CloseAll and the next
                // Join clear it; until then read_line still hands over what already arrived.
                m_state = State::Idle;
            }

            std::string m_buffer;
            std::string m_peer;
            std::string m_host;      // what we dialled, for SNI
            std::string m_pin;       // set means TLS
            std::string m_lastError; // why the last link refused to come up, for the UI to show
#if defined(PE_TLS)
            std::unique_ptr<TlsChannel> m_tls;
#endif
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

                    // net.join(host, port [, {pin = "<base64 sha256 of the server's public key>"}])
                    //   -> true | false, error
                    // Completes over later frames; poll net.status() until it reads "connected".
                    // With a pin the link is TLS and the server's key must match it exactly — that
                    // is how internet play reaches a relay without trusting the public PKI. Without
                    // one it is plain TCP, which is all LAN play needs.
                    net.set_function("join",
                                     [](const std::string &host, int port, sol::optional<sol::table> opts,
                                        sol::this_state ts) -> std::tuple<bool, sol::object>
                                     {
                                         sol::state_view view(ts);
                                         std::string error;
                                         if (port <= 0 || port > 65535)
                                             return {false, sol::make_object(view, "port out of range")};
                                         std::string pin;
                                         if (opts)
                                             pin = (*opts)["pin"].get_or(std::string());
                                         if (!Link().Join(host, static_cast<uint16_t>(port), pin, error))
                                             return {false, sol::make_object(view, error)};
                                         return {true, sol::nil};
                                     });

                    net.set_function("last_error", []()
                                     { return Link().LastError(); });

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
