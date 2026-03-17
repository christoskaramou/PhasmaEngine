#pragma once

#include "PhasmaAgent/Agent.h"
#include "EventQueue.h"
#include "ConversationHistory.h"
#include "ToolRegistry.h"
#include "AnthropicBackend.h"
#include "OpenAIBackend.h"
#include "GoogleBackend.h"
#include "RequestWorker.h"
#include "OllamaProcess.h"
#include <mutex>
#include <memory>

namespace pagent
{
    struct Agent::Impl
    {
        explicit Impl(AgentConfig config);
        ~Impl() = default;

        void RegisterTool(ToolDefinition tool);
        void UnregisterTool(const std::string &name);
        void ClearTools();

        bool Send(const std::string &user_message);
        bool Send(const std::string &user_message, const std::vector<ContentPart> &attachments);
        void Poll();
        void SetEventCallback(Agent::EventCallback cb);

        bool IsBusy() const;
        void CancelPending();
        void ClearHistory();
        std::vector<HistoryEntry> GetHistory() const;
        void InjectSystemMessage(const std::string &content);
        void SetModel(const std::string &model);
        Provider GetProvider() const { return m_config.provider; }
        TokenUsage GetUsage() const;
        void SetVectorStore(VectorStore *store);
        void SetCodebaseStore(VectorStore *store);

    private:
        AgentConfig m_config;
        ConversationHistory m_history;
        ToolRegistry m_toolRegistry;
        EventQueue m_eventQueue;
        std::shared_ptr<IProviderBackend> m_backend;
        std::unique_ptr<RequestWorker> m_worker;
        Agent::EventCallback m_eventCallback;
        std::mutex m_callbackMutex;
        OllamaProcess m_ollamaProcess;
        TokenUsage m_usage;
    };
} // namespace pagent
