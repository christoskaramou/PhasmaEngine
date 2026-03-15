#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <atomic>

// PhasmaAgent — standalone, provider-agnostic AI agent library.
// Namespace: pagent
// No engine, ImGui, Vulkan, or SDL dependencies.
// Single public header — all internal implementation is hidden via Pimpl.

namespace pagent
{

    // ---------------------------------------------------------------------------
    // Enums
    // ---------------------------------------------------------------------------

    enum class Provider
    {
        Anthropic, // Claude models via api.anthropic.com
        OpenAI,    // GPT models via api.openai.com
        Gemini,    // Google Gemini via OpenAI-compatible endpoint
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
        // RUNS ON WORKER THREAD — must be thread-safe.
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
    // Conversation history (neutral format — no provider-specific types)
    // ---------------------------------------------------------------------------

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
        std::string reasoning; // Reasoning/thinking process (e.g. for Gemini 3.1, DeepSeek-R1)
        std::string thought_signature; // Specific to Gemini 3.1, required to be echoed back

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
    // IProviderBackend — implement to add a new LLM provider
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
    // Reads PAGENT_ANTHROPIC_API_KEY, PAGENT_OPENAI_API_KEY, PAGENT_GEMINI_API_KEY.
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
        int turnInput = 0;
        int turnOutput = 0;
        int totalInput = 0;
        int totalOutput = 0;
        int totalCacheRead = 0;
        int totalCacheWrite = 0;
    };

    // Estimate cost in USD based on provider, model, and token counts.
    // Uses approximate per-model pricing. Returns 0 for local/free models.
    float EstimateCostUSD(Provider provider, const std::string &model,
                          int inputTokens, int outputTokens,
                          int cacheReadTokens = 0, int cacheWriteTokens = 0);

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
        int max_tokens = 4096;
        float temperature = 0.7f;
        int max_tool_rounds = 10;         // Hard cap on agentic loop iterations
        int max_history_messages = 40;    // Keep last N messages (0 = unlimited). System message always kept.
        int max_tool_result_chars = 4000; // Truncate tool results beyond this (0 = unlimited)
        int summarize_after_messages = 0; // Summarize old messages when history exceeds this (0 = disabled)

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
        void InjectSystemMessage(const std::string &content); // Inject without sending a request
        void SetModel(const std::string &model);
        Provider GetProvider() const;

        // Fetch available model names from the provider. Blocking HTTP call.
        // For Anthropic returns a hardcoded list (no listing endpoint).
        static std::vector<std::string> FetchModels(Provider provider,
                                                    const std::string &api_key,
                                                    const std::string &base_url = "");

        struct ModelInfo
        {
            std::string name;
            bool local = true; // false for Ollama cloud/remote models that need pulling
        };
        static std::vector<ModelInfo> FetchModelInfos(Provider provider,
                                                      const std::string &api_key,
                                                      const std::string &base_url = "");

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

        // Check if a local Ollama model supports tool calling.
        // Blocking HTTP call to /api/show. Returns true for non-Ollama providers.
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
