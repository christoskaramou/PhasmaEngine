#include "GUI/Agent/EditorMcp.h"

#include "GUI/Agent/EditorToolCatalog.h"
#include "GUI/Agent/EditorToolRuntime.h"
#include "GUI/GUI.h"
#include "PhasmaMCP/HttpTransport.h"
#include "PhasmaMCP/Server.h"

namespace pe
{
    namespace
    {
        constexpr int kDefaultMcpPort = 8765;

        // Route pmcp logs into PhasmaCore's logging facility so MCP traffic shows up in the
        // editor Console alongside engine logs. Debug is silenced by default.
        //
        // Important: Error level uses Log::Error directly, NOT the PE_ERROR macro. PE_ERROR
        // throws std::runtime_error; the MCP server logs at Error severity for recoverable
        // failures (e.g. tool handler exceptions) and immediately wraps them as a normal
        // CallToolResult with isError=true. Throwing here would unwind out of the httplib
        // worker thread and replace the spec-compliant error response with a connection drop.
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
    } // namespace

    EditorMcp::EditorMcp(EditorToolRuntime *runtime, GUI *gui)
        : m_runtime(runtime), m_gui(gui)
    {
        pmcp::ServerConfig serverCfg;
        serverCfg.name = "phasmaeditor-tools";
        serverCfg.title = "PhasmaEditor Tools";
        serverCfg.version = "0.1.0";
        serverCfg.instructions = "Use tools/list and tools/call to inspect and interact with the running PhasmaEditor instance.";
        serverCfg.log = &RouteLog;

        m_server = std::make_unique<pmcp::Server>(std::move(serverCfg));

        // Per-request tool list: we rebuild it on every dispatch so newly-acquired state
        // (codebase BM25 instance becoming ready, runtime swap during hot-reload) is picked up.
        m_server->SetToolProvider([this]()
                                  {
            EditorToolCatalogContext ctx;
            ctx.projectRoot = GetEditorRepoRootFromAssets().string();
            ctx.gui = m_gui;
            ctx.runtime = m_runtime;
            if (m_gui)
                ctx.codebaseBM25 = m_gui->GetCodebaseBM25Shared();
            return BuildEditorToolDefinitions(ctx); });

        pmcp::HttpTransportConfig transportCfg;
        transportCfg.bindAddress = "127.0.0.1";
        transportCfg.port = kDefaultMcpPort;
        // Claude Code (and other MCP HTTP clients) probe for OAuth metadata on every connect
        // and refuse to attach without it. Opt in to the compatibility shim so they stop
        // prompting "Needs Auth"; the transport's real safety boundary is loopback-only bind
        // enforcement plus the Origin check on mutating endpoints.
        transportCfg.enableLocalOauthShim = true;
        transportCfg.log = &RouteLog;
        m_transport = std::make_unique<pmcp::HttpTransport>(m_server.get(), std::move(transportCfg));
    }

    EditorMcp::~EditorMcp()
    {
        Stop();
    }

    void EditorMcp::Start()
    {
        if (m_transport)
            m_transport->Start();
    }

    void EditorMcp::Stop()
    {
        if (m_transport)
            m_transport->Stop();
    }

    bool EditorMcp::IsRunning() const
    {
        return m_transport && m_transport->IsRunning();
    }

    int EditorMcp::GetPort() const
    {
        return m_transport ? m_transport->Port() : 0;
    }
} // namespace pe
