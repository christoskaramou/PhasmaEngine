// PhasmaEditor/Code/GUI/AI/AICompletionService.h
#pragma once
#include "PhasmaAgent/Agent.h"
#include <functional>
#include <string>
#include <optional>
#include <atomic>

namespace pe
{
    enum class CompletionLanguage
    {
        Lua,
        HLSL
    };

    // All public methods must be called from the main thread only.
    // Poll() drains pagent event queues and fires stored callbacks on the main thread.
    class AICompletionService
    {
    public:
        AICompletionService() = default;

        // Call from GUI::LoadAgentConfig(). Startup-only, no hot-reload.
        void Init(const std::string &provider_str,
                  const std::string &api_key,
                  const std::string &model,
                  const std::string &base_url);

        bool IsEnabled() const { return m_enabled; }

        // context: ~50 lines around cursor with <cursor> marker injected.
        // cursorPos: saved TextEditor cursor position for insertion later.
        // Cancels any in-flight request for the same language. Callback receives empty string on error.
        void RequestScriptCompletion(const std::string &context,
                                     CompletionLanguage lang,
                                     std::function<void(std::string)> callback);

        void RequestPropertySuggestion(const std::string &node_type,
                                       const std::string &field_name,
                                       const std::string &current_value,
                                       std::function<void(std::string)> callback);

        void Poll();

        // Helper: extract ~50 lines around cursor from full text, inject <cursor> marker.
        static std::string BuildContext(const std::string &fullText, int cursorLine, int radius = 25);

    private:
        static pagent::AgentConfig BuildConfig(const std::string &provider_str,
                                               const std::string &api_key,
                                               const std::string &model,
                                               const std::string &base_url,
                                               const std::string &system_prompt,
                                               int max_tokens);
        static pagent::Provider ParseProvider(const std::string &s);

        struct AgentSlot
        {
            std::optional<pagent::Agent> agent;
            std::function<void(std::string)> callback;
            uint32_t generation = 0;   // incremented on each new request
            uint32_t cbGeneration = 0; // generation when callback was set
        };

        void SetupSlot(AgentSlot &slot, const std::string &provider_str,
                       const std::string &api_key, const std::string &model,
                       const std::string &base_url, const std::string &system_prompt,
                       int max_tokens);

        void SendToSlot(AgentSlot &slot, const std::string &userMessage,
                        std::function<void(std::string)> callback);

        bool m_enabled = false;
        AgentSlot m_lua;
        AgentSlot m_hlsl;
        AgentSlot m_prop;
    };
} // namespace pe
