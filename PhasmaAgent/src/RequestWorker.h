#pragma once

#include "PhasmaAgent/Agent.h"
#include "StreamParser.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <string>

namespace pagent
{
    class EventQueue;
    class ConversationHistory;
    class ToolRegistry;

    class RequestWorker
    {
    public:
        RequestWorker(const AgentConfig &config,
                      IProviderBackend *backend,
                      ConversationHistory &history,
                      ToolRegistry &toolRegistry,
                      EventQueue &eventQueue);
        ~RequestWorker();

        bool Submit(const std::string &user_message);
        bool IsBusy() const { return m_busy.load(std::memory_order_relaxed); }
        void Cancel();
        void SetBackend(IProviderBackend *backend) { m_backend = backend; }

    private:
        void ThreadFunc();
        void RunAgenticLoop(const std::string &user_message);
        std::string HttpPost(const std::string &host,
                             const std::string &path,
                             const std::map<std::string, std::string> &headers,
                             const std::string &body,
                             std::vector<AgentEvent> &out_events);
        void PushEvent(AgentEvent ev);
        void Log(const std::string &msg) const;

        const AgentConfig &m_config;
        IProviderBackend *m_backend;
        ConversationHistory &m_history;
        ToolRegistry &m_toolRegistry;
        EventQueue &m_eventQueue;

        std::thread m_thread;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::optional<std::string> m_pendingMessage;
        std::atomic<bool> m_busy{false};
        std::atomic<bool> m_cancel{false};
        std::atomic<bool> m_stop{false};

        // Set while an HTTP request is in flight so Cancel/destructor can abort it immediately.
        std::function<void()> m_stopActiveClient;
        std::mutex m_clientMutex;
    };
} // namespace pagent
