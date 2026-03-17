#pragma once

#include "PhasmaAgent/Agent.h"

namespace pagent
{
    class GoogleBackend : public IProviderBackend
    {
    public:
        std::string BuildRequestJson(
            const std::string &model,
            const std::string &system_prompt,
            int max_tokens,
            float temperature,
            const std::vector<NeutralMessage> &messages,
            const std::string &tools_schema_json) const override;

        bool ParseStreamEvent(
            const std::string &event_data,
            std::vector<AgentEvent> &out_events) override;

        void ResetStreamState() const override;

        std::string BuildToolsJson(
            const std::vector<ToolDefinition> &tools) const override;

        NeutralMessage FormatToolResult(
            const std::string &tool_call_id,
            const std::string &tool_name,
            const std::string &result_json) const override;

        std::string GetEndpointPath() const override;
        std::pair<std::string, std::string> GetAuthHeader(const std::string &api_key) const override;
        bool SupportsVision() const override { return true; }

    private:
        struct ToolCallAccumulator
        {
            std::string name;
            std::string arguments;         // JSON string
            std::string thought_signature; // Required by Gemini thinking models
        };

        mutable std::string m_model;
        mutable std::string m_textAccumulator;
        mutable std::string m_thinkingAccumulator;
        mutable std::vector<ToolCallAccumulator> m_toolAccumulators;
        mutable int m_toolCallCounter = 0;
    };
} // namespace pagent
