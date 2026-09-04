#include "Agent/PlayerMcp.h"

#include "Agent/AgentToolRegistry.h"
#include "Phasma/MCP/HttpTransport.h"
#include "Phasma/MCP/Server.h"
#include "Phasma/MCP/Tool.h"

#include "ECS/Context.h"
#include "Script/ScriptSystem.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace pe
{
    namespace
    {
        // Route pmcp logs into PhasmaCore's logging facility (same policy as EditorMcp). Error uses
        // Log::Error directly, NOT PE_ERROR — PE_ERROR throws, which would unwind out of the httplib
        // worker thread instead of producing a spec-compliant error response.
        void RouteLog(pmcp::LogLevel level, const std::string &message)
        {
            switch (level)
            {
            case pmcp::LogLevel::Debug:
                break;
            case pmcp::LogLevel::Info:
                Log::Info(message);
                break;
            case pmcp::LogLevel::Warn:
                Log::Warn(message);
                break;
            case pmcp::LogLevel::Error:
                Log::Error(message);
                break;
            }
        }

        // Run `fn` on the main thread (via the host action queue) and block this worker thread until it
        // completes or the timeout elapses. Shared state is heap-allocated so a timed-out call can't leave
        // the queued lambda writing into a freed stack. Returns false on timeout.
        bool RunOnMain(const PlayerMcp::QueueActionFn &queue, std::function<void()> fn, int timeoutSeconds = 10)
        {
            struct State
            {
                std::mutex mtx;
                std::condition_variable cv;
                bool done = false;
            };
            auto state = std::make_shared<State>();
            queue([state, fn = std::move(fn)]()
                  {
                fn();
                {
                    std::lock_guard lock(state->mtx);
                    state->done = true;
                }
                state->cv.notify_one(); });

            std::unique_lock lock(state->mtx);
            return state->cv.wait_for(lock, std::chrono::seconds(timeoutSeconds), [&state]
                                      { return state->done; });
        }

        using pmcp::CallToolResult;
        using pmcp::Context;
        using pmcp::ToolDefinition;
        namespace schema = pmcp::schema;

        ToolDefinition MakeExecuteLuaTool(const PlayerMcp::QueueActionFn &queue)
        {
            ToolDefinition tool;
            tool.name = "execute_lua";
            tool.description = "Execute Lua in the running player's ScriptSystem (the same VM the game scripts "
                               "use). Use this to drive and inspect the game: move the camera, query/mutate "
                               "scene state, call any engine binding, or `return` a value. Returns captured "
                               "output or the returned value.";
            tool.inputSchema = schema::Object({
                {"code", "Lua code to execute", schema::String(), true},
            });
            tool.handler = [queue](const nlohmann::json &args, Context &) -> CallToolResult
            {
                const std::string code = args.value("code", "");
                if (code.empty())
                    return CallToolResult::Error("missing code");

                auto out = std::make_shared<std::string>();
                const bool done = RunOnMain(queue, [out, code]()
                                            {
                    if (!HasGlobalSystem<ScriptSystem>())
                    {
                        *out = "error: ScriptSystem not available";
                        return;
                    }
                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (!ss || !ss->IsInitialized())
                        *out = "error: ScriptSystem not initialized";
                    else
                        *out = ss->ExecuteLua(code); });
                if (!done)
                    return CallToolResult::Error("timeout waiting for Lua execution");
                if (out->rfind("error:", 0) == 0)
                    return CallToolResult::Error(*out);
                return CallToolResult::Json({{"output", *out}});
            };
            return tool;
        }

        ToolDefinition MakeScreenshotTool()
        {
            ToolDefinition tool;
            tool.name = "take_screenshot";
            tool.description = "Capture the current player frame to a PNG and return its file path. Use to "
                               "visually verify game state.";
            tool.inputSchema = schema::Object({});
            tool.handler = [](const nlohmann::json &, Context &) -> CallToolResult
            {
                namespace fs = std::filesystem;
                static std::atomic<int> s_counter{0};
                const int n = ++s_counter;
                fs::path path = fs::temp_directory_path() / ("phasma_player_shot_" + std::to_string(n) + ".png");
                std::error_code ec;
                fs::remove(path, ec);

                // EventSystem::PushEvent is mutex-guarded, so pushing from this worker thread is safe; the
                // main-thread frame pump drains it and writes the PNG during a later Draw.
                EventSystem::PushEvent(EventType::Screenshot, path.string());

                for (int i = 0; i < 120; ++i) // ~6s
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (fs::exists(path, ec) && fs::file_size(path, ec) > 0)
                        return CallToolResult::Json({{"path", path.string()}});
                }
                return CallToolResult::Error("screenshot timed out (no file written)");
            };
            return tool;
        }

        ToolDefinition MakeGetStateTool(const PlayerMcp::QueueActionFn &queue)
        {
            ToolDefinition tool;
            tool.name = "get_state";
            tool.description = "Return runtime state: fps and average frame time (ms). For richer scene data, "
                               "call execute_lua with `return scene.digest()` or `return engine.get_metrics()`.";
            tool.inputSchema = schema::Object({});
            tool.handler = [queue](const nlohmann::json &, Context &) -> CallToolResult
            {
                auto fps = std::make_shared<double>(0.0);
                auto frameMs = std::make_shared<double>(0.0);
                const bool done = RunOnMain(queue, [fps, frameMs]()
                                            {
                    const double dt = FrameTimer::Instance().GetDelta();
                    *fps = dt > 0.0 ? 1.0 / dt : 0.0;
                    *frameMs = dt * 1000.0; });
                if (!done)
                    return CallToolResult::Error("timeout reading state");
                return CallToolResult::Json({{"fps", *fps}, {"frame_ms", *frameMs}});
            };
            return tool;
        }

        // Wrap a script-registered tool (AgentToolRegistry) as an MCP tool. The handler hops to the main
        // thread to call the Lua handler with the marshaled args.
        ToolDefinition MakeScriptTool(const PlayerMcp::QueueActionFn &queue, const AgentToolRegistry::Meta &meta)
        {
            ToolDefinition tool;
            tool.name = meta.name;
            tool.description = meta.description;
            tool.inputSchema = meta.inputSchema.is_null()
                                   ? nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}}
                                   : meta.inputSchema;
            const std::string name = meta.name;
            tool.handler = [queue, name](const nlohmann::json &args, Context &) -> CallToolResult
            {
                auto out = std::make_shared<nlohmann::json>();
                auto ok = std::make_shared<bool>(false);
                auto err = std::make_shared<std::string>();
                const bool done = RunOnMain(queue, [=]()
                                            {
                    if (!HasGlobalSystem<ScriptSystem>())
                    {
                        *err = "ScriptSystem not available";
                        return;
                    }
                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (!ss || !ss->IsInitialized())
                    {
                        *err = "ScriptSystem not initialized";
                        return;
                    }
                    *out = AgentToolRegistry::Instance().Invoke(ss->GetLua(), name, args, *ok, *err); });
                if (!done)
                    return CallToolResult::Error("timeout invoking tool: " + name);
                if (!*ok)
                    return CallToolResult::Error(*err);
                return CallToolResult::Json(*out);
            };
            return tool;
        }
    } // namespace

    PlayerMcp::PlayerMcp(QueueActionFn queueAction, int port)
        : m_queueAction(std::move(queueAction))
    {
        pmcp::ServerConfig serverCfg;
        serverCfg.name = "phasmaplayer-tools";
        serverCfg.title = "Phasma Player Tools";
        serverCfg.version = "0.1.0";
        serverCfg.instructions = "Drive and inspect the running PhasmaPlayer: execute_lua, take_screenshot, "
                                 "get_state, plus any tools the game registered via mcp.register_tool.";
        serverCfg.log = &RouteLog;
        m_server = std::make_unique<pmcp::Server>(std::move(serverCfg));

        // Rebuilt on every tools/list and tools/call so newly script-registered tools appear immediately.
        const QueueActionFn queue = m_queueAction;
        m_server->SetToolProvider([queue]()
                                  {
            std::vector<ToolDefinition> tools;
            tools.push_back(MakeExecuteLuaTool(queue));
            tools.push_back(MakeScreenshotTool());
            tools.push_back(MakeGetStateTool(queue));
            for (const AgentToolRegistry::Meta &meta : AgentToolRegistry::Instance().SnapshotMeta())
                tools.push_back(MakeScriptTool(queue, meta));
            return tools; });

        pmcp::HttpTransportConfig transportCfg;
        transportCfg.bindAddress = "127.0.0.1"; // loopback only — never expose the player's Lua RCE off-host
        transportCfg.port = port;
        transportCfg.enableLocalOauthShim = true;
        transportCfg.log = &RouteLog;
        m_transport = std::make_unique<pmcp::HttpTransport>(m_server.get(), std::move(transportCfg));
    }

    PlayerMcp::~PlayerMcp()
    {
        Stop();
    }

    void PlayerMcp::Start()
    {
        if (m_transport)
            m_transport->Start();
    }

    void PlayerMcp::Stop()
    {
        if (m_transport)
            m_transport->Stop();
    }

    bool PlayerMcp::IsRunning() const
    {
        return m_transport && m_transport->IsRunning();
    }

    int PlayerMcp::GetPort() const
    {
        return m_transport ? m_transport->Port() : 0;
    }
} // namespace pe
