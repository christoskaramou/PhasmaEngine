#pragma once

namespace httplib
{
    class Server;
}

namespace pe
{
    class GUI;
    class EditorToolRuntime;

    class EditorToolServer
    {
    public:
        EditorToolServer(EditorToolRuntime *runtime, GUI *gui);
        ~EditorToolServer();

        void Start();
        void Stop();
        bool IsRunning() const { return m_running.load(); }
        int GetPort() const { return m_port; }

    private:
        void ConfigureRoutes();

        EditorToolRuntime *m_runtime = nullptr;
        GUI *m_gui = nullptr;
        std::unique_ptr<httplib::Server> m_server;
        std::thread m_thread;
        std::atomic<bool> m_running{false};
        int m_port = 8765;
    };
} // namespace pe
