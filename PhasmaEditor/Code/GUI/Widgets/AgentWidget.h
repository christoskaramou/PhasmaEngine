#pragma once

#include "GUI/Widget.h"
#include "PhasmaAgent/Agent.h"
#include <vector>
#include <string>
#include <mutex>
#include <functional>

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
        void Init(GUI *gui) override;
        void Update() override;

    private:
        void SubmitInput();
        void RenderMessage(const ChatMessage &msg);
        void OnAgentEvent(const pagent::AgentEvent &event);
        void RegisterTools();
        void QueueAction(std::function<void()> fn);
        void FlushActions();

        pagent::Agent m_agent;
        char m_inputBuf[2048] = {};
        bool m_scrollToBottom = false;
        bool m_isStreaming = false;
        bool m_agentConfigured = false;
        std::string m_modelName;

        std::mutex m_chatMutex;
        std::vector<ChatMessage> m_chat;
        std::string m_streamingText;
        std::string m_streamingThinking;
        std::mutex m_actionMutex;
        std::vector<std::function<void()>> m_pendingActions;
    };
} // namespace pe
