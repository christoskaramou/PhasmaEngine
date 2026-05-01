#pragma once

#include <memory>

namespace pmcp
{
    class Server;
    class HttpTransport;
} // namespace pmcp

namespace pe
{
    class GUI;
    class EditorToolRuntime;

    // Thin engine-side glue: builds a pmcp::Server + HttpTransport, wires the editor's tool
    // catalog as the per-request tool provider, and routes MCP server logs through PE_INFO/PE_WARN.
    // Replaces the previous EditorToolServer + EditorToolSchemaUtils pair: protocol/transport
    // logic lives in PhasmaMCP, this class only stitches it to the running editor.
    class EditorMcp
    {
    public:
        EditorMcp(EditorToolRuntime *runtime, GUI *gui);
        ~EditorMcp();

        EditorMcp(const EditorMcp &) = delete;
        EditorMcp &operator=(const EditorMcp &) = delete;

        void Start();
        void Stop();
        bool IsRunning() const;
        int GetPort() const;

    private:
        EditorToolRuntime *m_runtime = nullptr;
        GUI *m_gui = nullptr;
        std::unique_ptr<pmcp::Server> m_server;
        std::unique_ptr<pmcp::HttpTransport> m_transport;
    };
} // namespace pe
