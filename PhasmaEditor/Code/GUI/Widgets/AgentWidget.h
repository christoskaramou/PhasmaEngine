#pragma once

#include "GUI/Widget.h"
#include "PhasmaAgent/Agent.h"
#include <vector>
#include <string>
#include <mutex>
#include <functional>
#include <optional>

namespace pe
{
    struct ChatMessage
    {
        enum class Role
        {
            User,
            Assistant,
            System
        };
        Role role;
        std::string text;
        std::string thinking; // reasoning/thinking content (if any)
    };

    class AgentWidget : public Widget
    {
    public:
        AgentWidget();
        ~AgentWidget();
        void Init(GUI *gui) override;
        void Update() override;

    private:
        void SubmitInput();
        void RenderMessage(const ChatMessage &msg);
        void OnAgentEvent(const pagent::AgentEvent &event);
        void RegisterTools();
        void QueueAction(std::function<void()> fn);
        void FlushActions();
        void FetchAvailableModels();
        void ConfigureAgent(pagent::Provider provider);

        std::optional<pagent::Agent> m_agent;
        char m_inputBuf[2048] = {};
        bool m_scrollToBottom = false;
        bool m_isStreaming = false;
        bool m_agentConfigured = false;
        std::string m_modelName;
        std::vector<std::string> m_availableModels;
        std::vector<bool> m_modelIsLocal;
        int m_selectedModelIndex = 0;
        bool m_isPulling = false;
        pagent::Agent::CancelToken m_pullCancel;
        char m_modelFilter[128] = {};

        // Provider management (from PhasmaAgent)
        std::vector<pagent::ProviderInfo> m_providers;
        int m_selectedProviderIndex = 0;

        std::mutex m_chatMutex;
        std::vector<ChatMessage> m_chat;
        std::string m_streamingText;
        std::string m_streamingThinking;
        std::mutex m_actionMutex;
        std::vector<std::function<void()>> m_pendingActions;
    };
} // namespace pe
