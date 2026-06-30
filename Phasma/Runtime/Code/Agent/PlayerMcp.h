#pragma once

#include <functional>
#include <memory>

namespace pmcp
{
    class Server;
    class HttpTransport;
} // namespace pmcp

namespace pe
{
    // Loopback MCP server for the PLAYER runtime (PhasmaPlayer). Mirrors EditorMcp but exposes only a
    // small runtime tool set — execute_lua, take_screenshot, get_state — plus any tools a game script
    // registered via mcp.register_tool (AgentToolRegistry). Tool handlers run on httplib worker threads,
    // so each one hops onto the main thread through the supplied QueueAction before touching engine state.
    //
    // Security: bound to 127.0.0.1 only and started solely when the project opts in (agent_config.json
    // "mcp": true). execute_lua is arbitrary code execution, so this must never be enabled in a shipped
    // build a player downloads — gate the start at the call site.
    class PlayerMcp
    {
    public:
        // queueAction posts a callable to be run on the main thread (drained once per frame). It must
        // stay valid for the lifetime of this PlayerMcp.
        using QueueActionFn = std::function<void(std::function<void()>)>;

        explicit PlayerMcp(QueueActionFn queueAction, int port = 8765);
        ~PlayerMcp();

        void Start();
        void Stop();
        bool IsRunning() const;
        int GetPort() const;

    private:
        QueueActionFn m_queueAction;
        std::unique_ptr<pmcp::Server> m_server;
        std::unique_ptr<pmcp::HttpTransport> m_transport;
    };
} // namespace pe
