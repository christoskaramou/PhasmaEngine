#pragma once

#include "PhasmaAgent/Agent.h"
#include "PhasmaAgent/BM25Index.h"
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
    class VectorStore;
    class BM25Index;
    class IncludeGraph;

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
        bool Submit(const std::string &user_message, const std::vector<ContentPart> &attachments);
        bool IsBusy() const { return m_busy.load(std::memory_order_relaxed); }
        void Cancel();
        void CancelAfterCurrentRound();
        // Summarize the full history now, keeping only the most recent keepRecent messages intact.
        // Runs synchronously; only call when IsBusy() == false.
        bool ForceCompact(size_t keepRecent = 4);
        void SetBackend(IProviderBackend *backend) { m_backend = backend; }
        void SetVectorStore(VectorStore *store) { m_vectorStore = store; }
        void SetCodebaseStore(VectorStore *store) { m_codebaseStore = store; }
        void SetCodebaseBM25(BM25Index *index) { m_codebaseBM25 = index; }
        void SetIncludeGraph(IncludeGraph *graph) { m_includeGraph = graph; }

    private:
        void ThreadFunc();
        void RunAgenticLoop(const std::string &user_message, const std::vector<ContentPart> &attachments = {});
        void MaybeSummarize();
        std::string HttpPost(const std::string &host,
                             const std::string &path,
                             const std::map<std::string, std::string> &headers,
                             const std::string &body,
                             std::vector<AgentEvent> &out_events);
        void PushEvent(AgentEvent ev);
        void Log(const std::string &msg) const;
        static std::string StripCommentsAndBlanks(const std::string &code);

        // Non-streaming HTTP POST. Returns {status_code, response_body}.
        std::pair<int, std::string> SimplePost(const std::string &host,
                                               const std::string &path,
                                               const std::map<std::string, std::string> &headers,
                                               const std::string &body);

        const AgentConfig &m_configRef; // owned by AgentImpl; read only at Submit() time
        AgentConfig m_activeConfig;     // snapshot taken at Submit() — worker reads this
        IProviderBackend *m_backend;
        ConversationHistory &m_history;
        ToolRegistry &m_toolRegistry;
        EventQueue &m_eventQueue;

        std::thread m_thread;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::optional<std::string> m_pendingMessage;
        std::vector<ContentPart> m_pendingAttachments;
        std::atomic<bool> m_busy{false};
        std::atomic<bool> m_cancel{false};
        std::atomic<bool> m_cancelAfterCurrentRound{false};
        std::atomic<bool> m_stop{false};

        // Set while an HTTP request is in flight so Cancel/destructor can abort it immediately.
        std::function<void()> m_stopActiveClient;
        std::mutex m_clientMutex;

        VectorStore *m_vectorStore = nullptr;
        VectorStore *m_codebaseStore = nullptr;
        BM25Index *m_codebaseBM25 = nullptr;
        IncludeGraph *m_includeGraph = nullptr;

        // Gemini persistent cache (system prompt + repo_map + tools).
        // Reused across Submit() calls; rebuilt only when static content changes.
        std::string m_geminiCacheName;
        std::string m_geminiCacheKey; // fingerprint of what was cached
    };
} // namespace pagent
