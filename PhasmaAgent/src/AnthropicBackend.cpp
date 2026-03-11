#include "AnthropicBackend.h"
#include <nlohmann/json.hpp>

namespace pagent
{
    using json = nlohmann::json;

    static json MessageToAnthropicJson(const NeutralMessage &msg)
    {
        switch (msg.role)
        {
        case NeutralMessage::Role::User:
        {
            return {{"role", "user"}, {"content", msg.content}};
        }
        case NeutralMessage::Role::Assistant:
        {
            json content = json::array();
            if (!msg.content.empty())
                content.push_back({{"type", "text"}, {"text", msg.content}});
            for (const auto &tc : msg.tool_calls)
            {
                json input = json::object();
                try
                {
                    input = json::parse(tc.arguments_json);
                }
                catch (...)
                {
                }
                content.push_back({
                    {"type", "tool_use"},
                    {"id", tc.id},
                    {"name", tc.name},
                    {"input", input},
                });
            }
            return {{"role", "assistant"}, {"content", content}};
        }
        case NeutralMessage::Role::Tool:
        {
            // tool results go back as a user message containing a tool_result block.
            json result_content = msg.tool_result_json;
            try
            {
                result_content = json::parse(msg.tool_result_json);
            }
            catch (...)
            {
            }
            json content = json::array();
            content.push_back({
                {"type", "tool_result"},
                {"tool_use_id", msg.tool_call_id},
                {"content", result_content.is_string()
                                ? json(msg.tool_result_json)
                                : result_content},
            });
            return {{"role", "user"}, {"content", content}};
        }
        default:
            return {};
        }
    }

    std::string AnthropicBackend::BuildRequestJson(
        const std::string &model,
        const std::string &system_prompt,
        int max_tokens,
        float temperature,
        const std::vector<NeutralMessage> &messages,
        const std::string &tools_schema_json) const
    {
        json body;
        body["model"] = model.empty() ? "claude-sonnet-4-6" : model;
        body["max_tokens"] = max_tokens;
        body["temperature"] = temperature;
        body["stream"] = true;

        if (!system_prompt.empty())
            body["system"] = system_prompt;

        json msgs = json::array();
        for (const auto &msg : messages)
        {
            if (msg.role == NeutralMessage::Role::System)
                continue; // system goes in top-level field, not in messages
            auto j = MessageToAnthropicJson(msg);
            if (!j.is_null())
                msgs.push_back(std::move(j));
        }
        body["messages"] = std::move(msgs);

        if (!tools_schema_json.empty())
        {
            try
            {
                body["tools"] = json::parse(tools_schema_json);
            }
            catch (...)
            {
            }
        }

        return body.dump();
    }

    std::string AnthropicBackend::BuildToolsJson(const std::vector<ToolDefinition> &tools) const
    {
        if (tools.empty())
            return "[]";

        json arr = json::array();
        for (const auto &tool : tools)
        {
            json properties = json::object();
            json required = json::array();

            for (const auto &prop : tool.properties)
            {
                static const char *typeNames[] = {
                    "string", "number", "boolean", "integer", "array", "object"};
                json p;
                p["type"] = typeNames[static_cast<int>(prop.type)];
                p["description"] = prop.description;
                properties[prop.name] = p;

                if (prop.required)
                    required.push_back(prop.name);
            }

            arr.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"input_schema", {
                                     {"type", "object"},
                                     {"properties", properties},
                                     {"required", required},
                                 }},
            });
        }
        return arr.dump();
    }

    void AnthropicBackend::ResetStreamState()
    {
        m_textAccumulator.clear();
        m_toolAccumulators.clear();
        m_currentToolIndex = -1;
    }

    bool AnthropicBackend::ParseStreamEvent(
        const std::string &event_data,
        std::vector<AgentEvent> &out_events)
    {
        if (event_data.empty())
            return true;

        json j;
        try
        {
            j = json::parse(event_data);
        }
        catch (...)
        {
            return true;
        }

        const auto type = j.value("type", std::string{});

        if (type == "content_block_start")
        {
            const auto &block = j["content_block"];
            const auto btype = block.value("type", std::string{});

            if (btype == "tool_use")
            {
                ToolAccumulator acc;
                acc.id = block.value("id", std::string{});
                acc.name = block.value("name", std::string{});
                m_toolAccumulators.push_back(std::move(acc));
                m_currentToolIndex = static_cast<int>(m_toolAccumulators.size()) - 1;

                AgentEvent ev;
                ev.type = AgentEventType::ToolCallBegin;
                ev.tool_name = m_toolAccumulators.back().name;
                ev.tool_call_id = m_toolAccumulators.back().id;
                out_events.push_back(std::move(ev));
            }
            else
            {
                m_currentToolIndex = -1;
            }
        }
        else if (type == "content_block_delta")
        {
            const auto &delta = j["delta"];
            const auto dtype = delta.value("type", std::string{});

            if (dtype == "text_delta")
            {
                const auto text = delta.value("text", std::string{});
                m_textAccumulator += text;

                AgentEvent ev;
                ev.type = AgentEventType::TextDelta;
                ev.text = text;
                out_events.push_back(std::move(ev));
            }
            else if (dtype == "input_json_delta" && m_currentToolIndex >= 0)
            {
                m_toolAccumulators[m_currentToolIndex].arguments +=
                    delta.value("partial_json", std::string{});
            }
        }
        else if (type == "content_block_stop")
        {
            if (m_currentToolIndex >= 0)
            {
                const auto &acc = m_toolAccumulators[m_currentToolIndex];
                AgentEvent ev;
                ev.type = AgentEventType::ToolCallComplete;
                ev.tool_name = acc.name;
                ev.tool_call_id = acc.id;
                ev.tool_input_json = acc.arguments;
                out_events.push_back(std::move(ev));
            }
            else if (!m_textAccumulator.empty())
            {
                // text block finished, emit TextComplete
                AgentEvent ev;
                ev.type = AgentEventType::TextComplete;
                ev.text = m_textAccumulator;
                out_events.push_back(std::move(ev));
                m_textAccumulator.clear();
            }
            m_currentToolIndex = -1;
        }
        else if (type == "message_stop")
        {
            return false; // stream done
        }
        else if (type == "error")
        {
            AgentEvent ev;
            ev.type = AgentEventType::Error;
            ev.error_message = j.value("error", json{}).value("message", "Anthropic stream error");
            out_events.push_back(std::move(ev));
            return false;
        }

        return true;
    }

    NeutralMessage AnthropicBackend::FormatToolResult(
        const std::string &tool_call_id,
        const std::string &tool_name,
        const std::string &result_json) const
    {
        NeutralMessage msg;
        msg.role = NeutralMessage::Role::Tool;
        msg.tool_call_id = tool_call_id;
        msg.tool_result_json = result_json;
        (void)tool_name;
        return msg;
    }

    std::string AnthropicBackend::GetEndpointPath() const
    {
        return "/v1/messages";
    }

    std::pair<std::string, std::string> AnthropicBackend::GetAuthHeader(
        const std::string &api_key) const
    {
        return {"x-api-key", api_key};
    }
} // namespace pagent
