#pragma once

#include "GUI/Widget.h"
#include "Script/ScriptSystem.h"
#include "PhasmaAgent/Agent.h"
#include <vector>
#include <string>
#include <mutex>
#include <functional>
#include <optional>
#include <memory>

namespace pagent
{
    class VectorStore;
}

namespace pe
{
    // An image attached to a chat message (for display in the UI)
    struct ChatImage
    {
        void *imguiDescriptor = nullptr; // ImTextureID for ImGui::Image
        int width = 0;
        int height = 0;
    };

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
        std::string thinking;          // reasoning/thinking content (if any)
        std::vector<ChatImage> images; // attached images (for display)
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
        void FetchAvailableModels(bool fetchRemote = false);
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
        bool m_ollamaModelLoaded = false;
        pagent::Agent::CancelToken m_pullCancel;
        char m_modelFilter[128] = {};

        // Cached model lists per provider index
        struct CachedModels
        {
            std::vector<std::string> names;
            std::vector<bool> local;
        };
        std::unordered_map<int, CachedModels> m_modelCache;

        // Provider management (from PhasmaAgent)
        std::vector<pagent::ProviderInfo> m_providers;
        int m_selectedProviderIndex = 0;

        std::mutex m_chatMutex;
        std::vector<ChatMessage> m_chat;
        std::string m_streamingText;
        std::string m_streamingThinking;
        std::mutex m_actionMutex;
        std::vector<std::function<void()>> m_pendingActions;
        std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
        std::atomic<bool> m_isFetchingModels{false};

        // Input history (up/down arrow)
        std::vector<std::string> m_inputHistory;
        int m_historyIndex = -1;

        // Image paste support
        struct PendingImage
        {
            std::string base64;              // base64-encoded PNG
            std::string mime_type;           // "image/png"
            void *imguiDescriptor = nullptr; // ImTextureID for preview
            int width = 0;
            int height = 0;
        };
        std::vector<PendingImage> m_pendingImages;

        // File paste support
        struct PendingFile
        {
            std::string name;    // filename
            std::string content; // file text content
        };
        std::vector<PendingFile> m_pendingFiles;

        void HandlePaste();
        void RenderPendingAttachments();

        // RAG / Embedding
        std::shared_ptr<pagent::VectorStore> m_vectorStore;
        int m_turnsSinceSave = 0;
        bool m_embeddingEnabled = false;
        int m_selectedEmbeddingProvider = 0; // 0=Google, 1=OpenAI, 2=Ollama
        int m_selectedEmbeddingModel = 0;
        std::vector<std::string> m_embeddingModels;
        std::vector<bool> m_embeddingModelIsLocal;
        std::shared_ptr<pagent::IEmbeddingProvider> CreateEmbeddingProvider();
        void UpdateEmbeddingModels(bool fetchRemote = false);
        void SaveConfig();
        void LoadConfig();
        std::string GetVectorStorePath() const;
        bool m_isFetchingEmbeddingModels = false;
        bool m_isPullingEmbedding = false;
        pagent::Agent::CancelToken m_pullEmbeddingCancel;

        // External AI file-based provider (Claude Code, Cursor, etc.)
        bool m_isExternalAI = false;
        char m_externalFile[256] = "chat_input.txt";
        std::string m_externalResponsePath;
        void PollExternalResponse();
        void UpdateExternalFileWatch();
        void WriteExternalHistory();
        std::string GetExternalResponsePath() const;

        pe::ScriptSystem m_agentScriptSystem;
    };
} // namespace pe
