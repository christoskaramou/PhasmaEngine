#pragma once

#include "PhasmaAgent/Agent.h"

namespace pagent
{
    class AnthropicBackend : public IProviderBackend
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
        // per stream state (reset at start of each request)
        struct ToolAccumulator
        {
            std::string id;
            std::string name;
            std::string arguments;
        };

        mutable std::string m_textAccumulator;
        mutable std::vector<ToolAccumulator> m_toolAccumulators;
        mutable int m_currentToolIndex = -1;
    };
} // namespace pagent
