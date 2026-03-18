#pragma once

#include "GUI/Widget.h"
#include "Script/ScriptSystem.h"
#include "PhasmaAgent/Agent.h"

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

    struct ChatFileAttachment
    {
        std::string name;       // filename
        void *iconDS = nullptr; // ImTextureID for file type icon (borrowed, not owned)
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
        std::string thinking;                        // reasoning/thinking content (if any)
        std::vector<ChatImage> images;               // attached images (for display)
        std::vector<ChatFileAttachment> attachments; // attached files (for display)
    };

    class AgentWidget : public Widget
    {
    public:
        AgentWidget();
        ~AgentWidget();
        void Init(GUI *gui) override;
        void Update() override;

        // Public accessors for codebase indexing
        pagent::IEmbeddingProvider *GetEmbeddingProvider() const { return m_embeddingProvider.get(); }
        std::shared_ptr<pagent::VectorStore> GetCodebaseStoreShared() const { return m_codebaseStore; }
        std::shared_ptr<pagent::IEmbeddingProvider> GetEmbeddingProviderShared() const { return m_embeddingProvider; }
        std::string GetCodebaseStorePath() const;
        const std::vector<std::string> &GetIndexDirectories() const { return m_indexDirectories; }
        const std::vector<std::string> &GetIncludeFiles() const { return m_includeFiles; }
        const std::vector<std::string> &GetSkipDirectories() const { return m_skipDirectories; }
        const std::vector<std::string> &GetSkipFiles() const { return m_skipFiles; }
        const std::vector<std::string> &GetSkipExtensions() const { return m_skipExtensions; }
        const std::vector<std::string> &GetSkipRegex() const { return m_skipRegex; }

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
        std::string m_pendingHistoryText; // applied inside InputText callback on next frame
        bool m_pendingHistoryUpdate = false;

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
            std::string name;       // filename
            std::string content;    // file text content
            void *iconDS = nullptr; // ImTextureID for file type icon (borrowed, not owned)
        };
        std::vector<PendingFile> m_pendingFiles;

        void HandlePaste();
        int m_lastPasteFrame = -2;
        void RenderPendingAttachments();
        void *UploadChatImage(const uint8_t *rgba, int width, int height);
        void HandleScreenshotEvent();

        // RAG / Embedding
        std::shared_ptr<pagent::IEmbeddingProvider> m_embeddingProvider;
        std::shared_ptr<pagent::VectorStore> m_codebaseStore; // Codebase index vectors
        bool m_embeddingEnabled = false;
        int m_selectedEmbeddingProvider = 0; // 0=Google, 1=OpenAI, 2=Ollama
        int m_selectedEmbeddingModel = 0;
        std::vector<std::string> m_embeddingModels;
        std::vector<bool> m_embeddingModelIsLocal;
        std::shared_ptr<pagent::IEmbeddingProvider> CreateEmbeddingProvider();
        void UpdateEmbeddingModels(bool fetchRemote = false);
        void SaveConfig();
        void LoadConfig();
        std::string GetEmbeddingModelFileBase() const;
        bool m_isFetchingEmbeddingModels = false;
        bool m_isPullingEmbedding = false;
        std::vector<std::string> m_indexDirectories;
        std::vector<std::string> m_includeFiles;
        std::vector<std::string> m_skipDirectories;
        std::vector<std::string> m_skipFiles;
        std::vector<std::string> m_skipExtensions;
        std::vector<std::string> m_skipRegex;

        // Index status tracking
        std::atomic<bool> m_indexStatusChecking{false};
        std::atomic<bool> m_indexStatusReady{false};
        int m_indexStatusOutdated = 0;
        int m_indexStatusTotal = 0;
        std::vector<std::string> m_indexStatusFiles; // outdated file list for tooltip
        bool m_wasIndexing = false;
        void CheckIndexStatus();
        std::atomic<bool> m_codebaseLoading{false};
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

        // Tracked GPU images for chat thumbnails (cleaned up on destroy)
        std::vector<Image *> m_chatGpuImages;
        std::vector<void *> m_chatImguiDescriptors;

        // File type icons for pending attachments
        Image *m_fileIcon = nullptr;
        void *m_fileIconDS = nullptr;
        Image *m_txtIcon = nullptr;
        void *m_txtIconDS = nullptr;
        Image *m_shaderIcon = nullptr;
        void *m_shaderIconDS = nullptr;
        Image *m_modelIcon = nullptr;
        void *m_modelIconDS = nullptr;
        Image *m_scriptIcon = nullptr;
        void *m_scriptIconDS = nullptr;

        // Full-size image popup
        void *m_popupImageDescriptor = nullptr;
        void *m_prevPopupImageDescriptor = nullptr;
        int m_popupImageWidth = 0;
        int m_popupImageHeight = 0;
    };
} // namespace pe
