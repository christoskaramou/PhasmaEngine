#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <atomic>

// PhasmaAgent - standalone, provider-agnostic AI agent library.
// Namespace: pagent
// No engine, ImGui, Vulkan, or SDL dependencies.
// Single public header - all internal implementation is hidden via Pimpl.

namespace pagent
{
    class VectorStore;
    class BM25Index;
    class IncludeGraph;

    // ---------------------------------------------------------------------------
    // Enums
    // ---------------------------------------------------------------------------

    enum class Provider
    {
        Anthropic, // Claude models via api.anthropic.com
        OpenAI,    // GPT models via api.openai.com
        Google,    // Google Gemini via native Gemini API
        Ollama,    // Local Ollama instance (OpenAI-compatible)
    };

    enum class SchemaType
    {
        String,
        Number,
        Boolean,
        Integer,
        Array,
        Object,
    };

    enum class AgentEventType
    {
        TextDelta,        // Streaming text fragment; read event.text
        TextComplete,     // Full assistant turn text accumulated; read event.text
        ThinkingDelta,    // Streaming reasoning/thinking fragment; read event.text
        ThinkingComplete, // Full reasoning text accumulated; read event.text
        ToolCallBegin,    // Model requested a tool; read event.tool_name, event.tool_call_id
        ToolCallComplete, // Tool arguments fully received; read event.tool_input_json
        ToolResult,       // Tool handler executed; read event.tool_name, event.text (result JSON)
        Usage,            // Token usage stats; read event.input_tokens, event.output_tokens
        TurnComplete,     // Full agentic loop finished for this Send call
        Error,            // Fatal error; read event.error_message
        Info,             // Informational message; read event.text
    };

    // ---------------------------------------------------------------------------
    // Tool definition
    // ---------------------------------------------------------------------------

    struct SchemaProperty
    {
        std::string name;
        std::string description;
        SchemaType type = SchemaType::String;
        bool required = false;
    };

    struct ToolDefinition
    {
        std::string name;
        std::string description;
        std::vector<SchemaProperty> properties;

        // Receives: raw JSON object string of arguments the model provided.
        // Returns:  raw JSON string of the result (e.g. "{\"status\":\"ok\"}").
        // RUNS ON WORKER THREAD - must be thread-safe.
        // For main-thread-only operations use the deferred std::promise pattern.
        std::function<std::string(const std::string &json_input)> handler;
    };

    // ---------------------------------------------------------------------------
    // Events
    // ---------------------------------------------------------------------------

    struct AgentEvent
    {
        AgentEventType type = AgentEventType::Error;
        std::string text;              // TextDelta, TextComplete, ToolResult content
        std::string tool_name;         // ToolCallBegin, ToolCallComplete, ToolResult
        std::string tool_call_id;      // Matches the model's tool use id
        std::string tool_input_json;   // ToolCallComplete: fully assembled arguments
        std::string thought_signature; // Specific to Gemini 3.1
        std::string error_message;     // Error
        int input_tokens = 0;          // Usage
        int output_tokens = 0;         // Usage
        int cache_read_tokens = 0;     // Usage (Anthropic prompt caching)
        int cache_creation_tokens = 0; // Usage
    };

    // ---------------------------------------------------------------------------
    // Conversation history (neutral format - no provider-specific types)
    // ---------------------------------------------------------------------------

    // Multimodal content part - text or base64-encoded image
    struct ContentPart
    {
        enum class Type
        {
            Text,
            ImageBase64
        };
        Type type = Type::Text;
        std::string data;      // text content or base64-encoded image bytes
        std::string mime_type; // e.g. "image/png", "image/jpeg" (for ImageBase64)
    };

    struct NeutralMessage
    {
        enum class Role
        {
            System,
            User,
            Assistant,
            Tool
        };

        Role role = Role::User;
        std::string content;
        std::vector<ContentPart> parts; // Multimodal parts. When non-empty, backends use this instead of content.
        std::string reasoning;          // Reasoning/thinking process (e.g. for Gemini 3.1, DeepSeek-R1)
        std::string thought_signature;  // Specific to Gemini 3.1, required to be echoed back

        struct ToolCall
        {
            std::string id;
            std::string name;
            std::string arguments_json;
            std::string thought_signature;
        };
        std::vector<ToolCall> tool_calls; // populated when role == Assistant

        // Populated when role == Tool
        std::string tool_call_id;
        std::string tool_name;
        std::string tool_result_json;
    };

    struct HistoryEntry
    {
        NeutralMessage message;
        uint64_t timestamp_ms = 0;
    };

    // ---------------------------------------------------------------------------
    // IProviderBackend - implement to add a new LLM provider
    // ---------------------------------------------------------------------------

    class IProviderBackend
    {
    public:
        virtual ~IProviderBackend() = default;

        // Build the full HTTP request body JSON for one agentic turn.
        virtual std::string BuildRequestJson(
            const std::string &model,
            const std::string &system_prompt,
            int max_tokens,
            float temperature,
            const std::vector<NeutralMessage> &messages,
            const std::string &tools_schema_json) const = 0;

        // Parse one complete SSE event (the data: payload).
        // Append any resulting AgentEvents to out_events.
        // Return false when the stream is done (e.g. [DONE] or message_stop).
        virtual bool ParseStreamEvent(
            const std::string &event_data,
            std::vector<AgentEvent> &out_events) = 0;

        // Reset per-stream mutable state. Called by RequestWorker before each request.
        virtual void ResetStreamState() const = 0;

        // Build the provider-specific tools array JSON.
        virtual std::string BuildToolsJson(
            const std::vector<ToolDefinition> &tools) const = 0;

        // Format a tool result as a NeutralMessage to append to history.
        virtual NeutralMessage FormatToolResult(
            const std::string &tool_call_id,
            const std::string &tool_name,
            const std::string &result_json) const = 0;

        // HTTP endpoint path, e.g. "/v1/messages".
        virtual std::string GetEndpointPath() const = 0;

        // Returns {header-name, header-value} for authentication.
        virtual std::pair<std::string, std::string> GetAuthHeader(
            const std::string &api_key) const = 0;

        // Whether this backend supports image content parts natively.
        virtual bool SupportsVision() const { return false; }
    };

    // ---------------------------------------------------------------------------
    // Provider discovery
    // ---------------------------------------------------------------------------

    struct ProviderInfo
    {
        Provider provider;
        std::string name; // "Anthropic", "OpenAI", etc.
        std::string apiKey;
        std::string defaultModel;
    };

    // Discovers available providers from environment variables.
    // Reads PAGENT_ANTHROPIC_API_KEY, PAGENT_OPENAI_API_KEY, PAGENT_GEMINI_API_KEY (Google).
    // Ollama is always included (no key needed).
    // Returns them in order; optionally selects one based on PAGENT_PROVIDER env var.
    std::vector<ProviderInfo> DiscoverProviders();

    // Returns the index into the DiscoverProviders() result matching PAGENT_PROVIDER,
    // or 0 if not set.
    int GetDefaultProviderIndex(const std::vector<ProviderInfo> &providers);

    // ---------------------------------------------------------------------------
    // Token usage
    // ---------------------------------------------------------------------------

    struct TokenUsage
    {
        int totalInput = 0;
        int totalOutput = 0;
        int totalCacheRead = 0;
        int totalCacheWrite = 0;
    };

    // ---------------------------------------------------------------------------
    // IEmbeddingProvider - implement to add an embedding model
    // ---------------------------------------------------------------------------

    class IEmbeddingProvider
    {
    public:
        virtual ~IEmbeddingProvider() = default;

        // Embed a text string. Returns a float vector of size Dimensions().
        virtual std::vector<float> Embed(const std::string &text) = 0;

        // Number of dimensions in the output embedding.
        virtual int Dimensions() const = 0;

        // Cancel support: when set, Embed should return {} immediately
        void SetCancel(std::atomic<bool> *cancel) { m_cancel = cancel; }
        bool IsCancelled() const { return m_cancel && m_cancel->load(); }

    protected:
        std::atomic<bool> *m_cancel = nullptr;
    };

    // ---------------------------------------------------------------------------
    // Model Routing
    // ---------------------------------------------------------------------------

    enum class QueryComplexity
    {
        Simple,  // Explain, find, simple Q&A -> cheapest model
        Medium,  // Refactor, change, tool use -> default model
        Complex, // Architecture, multi-file rewrites -> most capable model
    };

    // Classify a user query into a complexity tier based on keywords.
    QueryComplexity ClassifyQuery(const std::string &query);

    // Optional per-tier model overrides. Empty string = use default AgentConfig::model.
    struct ModelRouting
    {
        bool enabled = false;
        std::string simple_model;  // e.g. "gemini-2.5-flash-lite-preview-06-17"
        std::string complex_model; // e.g. "gemini-2.5-pro-preview-06-05"
        // Medium tier always uses AgentConfig::model (the default).
    };

    // ---------------------------------------------------------------------------
    // AgentConfig
    // ---------------------------------------------------------------------------

    struct AgentConfig
    {
        Provider provider = Provider::Anthropic;
        std::string api_key;
        std::string model;    // "claude-sonnet-4-6", "gpt-4o", "llama3", etc.
        std::string base_url; // Override base host, e.g. "http://localhost:11434" for Ollama
        std::string system_prompt;
        int max_tokens = 2048;
        float temperature = 0.7f;
        int max_tool_rounds = 10;         // Hard cap on agentic loop iterations
        int max_history_messages = 40;    // Keep last N messages (0 = unlimited). System message always kept.
        int max_tool_result_chars = 4000; // Truncate tool results beyond this (0 = unlimited)
        int summarize_after_messages = 0; // Summarize old messages when history exceeds this (0 = disabled)

        // Model routing: automatically select cheaper/better models based on query complexity
        ModelRouting routing;

        // Gemini API key for vision fallback (image description when main provider lacks vision)
        std::string gemini_api_key_for_vision;

        // Optional: provide your own HTTP transport.
        // When set, httplib and OpenSSL are completely bypassed.
        // Signature: (method, url, headers, body) -> (http_status_code, response_body)
        using HttpHandler = std::function<
            std::pair<int, std::string>(
                const std::string &method,
                const std::string &url,
                const std::map<std::string, std::string> &headers,
                const std::string &body)>;
        HttpHandler custom_http_handler;

        // Optional: inject a fully custom backend (overrides provider enum).
        std::shared_ptr<IProviderBackend> custom_backend;

        // Optional: embedding provider for RAG.
        std::shared_ptr<IEmbeddingProvider> embedding_provider;

        // RAG settings
        int rag_top_k = 3;
        float rag_min_score = 0.1f;
        int rag_max_context_chars = 10000;
        int rag_max_entry_chars = 8000; // Max chars per indexed RAG entry (0 = unlimited)

        // Repo map: compact codebase overview (~1K tokens) prepended to system prompt.
        // Generated by RepoMap::Generate() after indexing. Empty = disabled.
        std::string repo_map;

        // Optional: log callback for internal debug/warning messages.
        std::function<void(const std::string &)> log_callback;
    };

    // ---------------------------------------------------------------------------
    // Agent
    // ---------------------------------------------------------------------------

    class Agent
    {
    public:
        explicit Agent(AgentConfig config);
        ~Agent();

        Agent(const Agent &) = delete;
        Agent &operator=(const Agent &) = delete;
        Agent(Agent &&) noexcept;
        Agent &operator=(Agent &&) noexcept;

        // --- Tool management ---
        // Register/unregister before calling Send. Not thread-safe with Send.
        void RegisterTool(ToolDefinition tool);
        void UnregisterTool(const std::string &name);
        void ClearTools();

        // --- Messaging ---
        // Non-blocking. Returns false if a request is already in-flight.
        bool Send(const std::string &user_message);
        bool Send(const std::string &user_message, const std::vector<ContentPart> &attachments);

        // Call each frame on the main thread.
        // Drains the event queue and fires the stored event callback for each event.
        // Does NOT hold any internal mutex while firing callbacks.
        void Poll();

        // Set the callback fired by Poll() for each AgentEvent.
        using EventCallback = std::function<void(const AgentEvent &)>;
        void SetEventCallback(EventCallback callback);

        // --- State ---
        bool IsBusy() const;
        void CancelPending(); // Best-effort; may not abort an in-flight HTTP call immediately
        TokenUsage GetUsage() const;

        // --- History ---
        void ClearHistory();
        std::vector<HistoryEntry> GetHistory() const;
        void LoadHistory(const std::vector<HistoryEntry> &entries); // Restore a saved session
        void InjectSystemMessage(const std::string &content);       // Inject without sending a request
        // Summarize old history and replace it with a compact summary.
        // keepRecent: number of most-recent messages to preserve verbatim.
        // Returns true if summarization succeeded. Only call when !IsBusy().
        bool ForceCompact(size_t keepRecent = 4);
        void SetModel(const std::string &model);
        void SetRepoMap(const std::string &repoMap);
        Provider GetProvider() const;
        void SetVectorStore(VectorStore *store);
        void SetCodebaseStore(VectorStore *store);
        void SetCodebaseBM25(BM25Index *index);
        void SetIncludeGraph(IncludeGraph *graph);
        void SetEmbeddingProvider(std::shared_ptr<IEmbeddingProvider> provider);

        struct ModelInfo
        {
            std::string name;
            bool local = true;          // false for Ollama cloud/remote models that need pulling
            bool supportsVision = true; // model accepts image input
            bool supportsTools = true;  // model supports function/tool calling
        };
        static std::vector<ModelInfo> FetchModelInfos(Provider provider,
                                                      const std::string &api_key,
                                                      const std::string &base_url = "",
                                                      bool local_only = false);

        // Fetch Ollama embedding models.
        // When local_only=true, only lists locally installed models.
        // When local_only=false, also fetches remote models from ollama.com.
        static std::vector<ModelInfo> FetchOllamaEmbeddingModels(const std::string &base_url = "",
                                                                 bool local_only = false);

        // Pull/download an Ollama model in the background.
        // progressCb fires with status strings, completeCb fires with success/failure.
        // Returns a cancel token; call CancelPull() to abort.
        using ProgressCallback = std::function<void(const std::string &status)>;
        using CompleteCallback = std::function<void(bool success)>;
        using CancelToken = std::shared_ptr<std::atomic<bool>>;
        static CancelToken PullModel(const std::string &model,
                                     ProgressCallback progressCb,
                                     CompleteCallback completeCb);
        static void CancelPull(const CancelToken &token);

        // Query model capabilities (vision, tools) via Ollama /api/show.
        // For non-Ollama providers, returns {true, true}.
        struct ModelCaps
        {
            bool vision = true;
            bool tools = true;
        };
        static ModelCaps QueryCapabilities(Provider provider, const std::string &model,
                                           const std::string &base_url = "");

        // Convenience: check if a model supports tool calling.
        static bool SupportsTools(Provider provider, const std::string &model,
                                  const std::string &base_url = "");

        // Unload an Ollama model from GPU memory. No-op for other providers.
        static void UnloadModel(Provider provider, const std::string &model,
                                const std::string &base_url = "");

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace pagent
