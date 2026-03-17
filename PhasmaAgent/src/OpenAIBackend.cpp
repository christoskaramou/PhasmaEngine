#include "OpenAIBackend.h"
#include <nlohmann/json.hpp>

namespace pagent
{
    using json = nlohmann::json;

    static json MessageToOpenAIJson(const NeutralMessage &msg)
    {
        switch (msg.role)
        {
        case NeutralMessage::Role::System:
            return {{"role", "system"}, {"content", msg.content}};

        case NeutralMessage::Role::User:
        {
            if (!msg.parts.empty())
            {
                json content = json::array();
                for (const auto &part : msg.parts)
                {
                    if (part.type == ContentPart::Type::Text)
                        content.push_back({{"type", "text"}, {"text", part.data}});
                    else if (part.type == ContentPart::Type::ImageBase64)
                        content.push_back({{"type", "image_url"}, {"image_url", {{"url", "data:" + part.mime_type + ";base64," + part.data}}}});
                }
                return {{"role", "user"}, {"content", content}};
            }
            return {{"role", "user"}, {"content", msg.content}};
        }

        case NeutralMessage::Role::Assistant:
        {
            json j = {{"role", "assistant"}};

            if (!msg.content.empty())
                j["content"] = msg.content;
            else
                j["content"] = nullptr;

            if (!msg.tool_calls.empty())
            {
                json tcs = json::array();
                for (const auto &tc : msg.tool_calls)
                {
                    tcs.push_back({
                        {"id", tc.id},
                        {"type", "function"},
                        {"function", {
                                         {"name", tc.name},
                                         {"arguments", tc.arguments_json},
                                     }},
                    });
                }
                j["tool_calls"] = std::move(tcs);
            }
            return j;
        }

        case NeutralMessage::Role::Tool:
            return {{"role", "tool"}, {"tool_call_id", msg.tool_call_id}, {"name", msg.tool_name}, {"content", msg.content}};

        default:
            return {};
        }
    }

    std::string OpenAIBackend::BuildRequestJson(
        const std::string &model,
        const std::string &system_prompt,
        int max_tokens,
        float temperature,
        const std::vector<NeutralMessage> &messages,
        const std::string &tools_schema_json) const
    {
        json body;
        std::string m = model.empty() ? "gpt-4.1-mini" : model;
        body["model"] = m;

        // Newer OpenAI models (o-series, gpt-5.x) require max_completion_tokens
        bool useMaxCompletionTokens = m.rfind("o1-", 0) == 0 ||
                                      m.rfind("o3-", 0) == 0 ||
                                      m.rfind("o4-", 0) == 0 ||
                                      m.rfind("gpt-5", 0) == 0;
        if (useMaxCompletionTokens)
            body["max_completion_tokens"] = max_tokens;
        else
            body["max_tokens"] = max_tokens;

        // Reasoning models (o-series, gpt-5.x) only support temperature=1
        if (!useMaxCompletionTokens)
            body["temperature"] = temperature;
        body["stream"] = true;
        body["stream_options"] = {{"include_usage", true}};

        json msgs = json::array();

        if (!system_prompt.empty())
            msgs.push_back({{"role", "system"}, {"content", system_prompt}});

        for (const auto &msg : messages)
        {
            if (msg.role == NeutralMessage::Role::System)
                continue; // already handled above
            auto j = MessageToOpenAIJson(msg);
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

        ResetStreamState(); // clear any stale accumulators/signatures

        return body.dump();
    }

    std::string OpenAIBackend::BuildToolsJson(const std::vector<ToolDefinition> &tools) const
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
                {"type", "function"},
                {"function", {
                                 {"name", tool.name},
                                 {"description", tool.description},
                                 {"parameters", {
                                                    {"type", "object"},
                                                    {"properties", properties},
                                                    {"required", required},
                                                }},
                             }},
            });
        }
        return arr.dump();
    }

    void OpenAIBackend::ResetStreamState() const
    {
        m_textAccumulator.clear();
        m_thinkingAccumulator.clear();
        m_toolAccumulators.clear();
    }

    bool OpenAIBackend::ParseStreamEvent(
        const std::string &event_data,
        std::vector<AgentEvent> &out_events)
    {
        if (event_data == "[DONE]")
        {
            if (!m_thinkingAccumulator.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::ThinkingComplete;
                ev.text = m_thinkingAccumulator;
                out_events.push_back(std::move(ev));
                m_thinkingAccumulator.clear();
            }
            if (!m_toolAccumulators.empty())
            {
                for (const auto &acc : m_toolAccumulators)
                {
                    AgentEvent ev;
                    ev.type = AgentEventType::ToolCallComplete;
                    ev.tool_name = acc.name;
                    ev.tool_call_id = acc.id;
                    ev.tool_input_json = acc.arguments;
                    out_events.push_back(std::move(ev));
                }
                m_toolAccumulators.clear();
            }
            if (!m_textAccumulator.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TextComplete;
                ev.text = m_textAccumulator;
                out_events.push_back(std::move(ev));
                m_textAccumulator.clear();
            }
            return false;
        }

        json j;
        try
        {
            j = json::parse(event_data);
        }
        catch (...)
        {
            return true;
        }

        // error response
        if (j.contains("error"))
        {
            AgentEvent ev;
            ev.type = AgentEventType::Error;
            ev.error_message = j["error"].value("message", "OpenAI stream error");
            out_events.push_back(std::move(ev));
            return false;
        }

        // Usage info (OpenAI includes it in the final chunk, or with stream_options)
        if (j.contains("usage") && !j["usage"].is_null())
        {
            const auto &usage = j["usage"];
            AgentEvent ev;
            ev.type = AgentEventType::Usage;
            ev.input_tokens = usage.value("prompt_tokens", 0);
            ev.output_tokens = usage.value("completion_tokens", 0);
            // OpenAI reports cached tokens inside prompt_tokens_details
            if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].is_object())
                ev.cache_read_tokens = usage["prompt_tokens_details"].value("cached_tokens", 0);
            out_events.push_back(std::move(ev));
        }

        const auto &choices = j.value("choices", json::array());
        if (choices.empty())
            return true;

        const auto &choice = choices[0];
        const auto &delta = choice.value("delta", json::object());

        const auto finish = (choice.contains("finish_reason") && choice["finish_reason"].is_string())
                                ? choice["finish_reason"].get<std::string>()
                                : std::string{};

        // reasoning/thinking delta (Qwen3, DeepSeek, etc. via OpenAI-compatible APIs)
        if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null())
        {
            const auto text = delta["reasoning_content"].get<std::string>();
            m_thinkingAccumulator += text;

            AgentEvent ev;
            ev.type = AgentEventType::ThinkingDelta;
            ev.text = text;
            out_events.push_back(std::move(ev));
        }

        // text delta
        if (delta.contains("content") && !delta["content"].is_null())
        {
            const auto text = delta["content"].get<std::string>();
            m_textAccumulator += text;

            AgentEvent ev;
            ev.type = AgentEventType::TextDelta;
            ev.text = text;
            out_events.push_back(std::move(ev));
        }

        // tool call deltas
        if (delta.contains("tool_calls"))
        {
            for (const auto &tc_delta : delta["tool_calls"])
            {
                const int index = tc_delta.value("index", 0);
                while (static_cast<int>(m_toolAccumulators.size()) <= index)
                    m_toolAccumulators.emplace_back();

                auto &acc = m_toolAccumulators[index];

                if (tc_delta.contains("function"))
                {
                    const auto &f = tc_delta["function"];
                    if (f.contains("name"))
                    {
                        const std::string name = f["name"].get<std::string>();
                        if (!name.empty())
                            acc.name = name;
                    }
                    if (f.contains("arguments"))
                        acc.arguments += f["arguments"].get<std::string>();
                }

                if (tc_delta.contains("id"))
                {
                    acc.id = tc_delta["id"].get<std::string>();

                    AgentEvent ev;
                    ev.type = AgentEventType::ToolCallBegin;
                    ev.tool_name = acc.name;
                    ev.tool_call_id = acc.id;
                    out_events.push_back(std::move(ev));
                }
            }
        }

        // finish reasons
        if (finish == "tool_calls")
        {
            for (const auto &acc : m_toolAccumulators)
            {
                AgentEvent ev;
                ev.type = AgentEventType::ToolCallComplete;
                ev.tool_name = acc.name;
                ev.tool_call_id = acc.id;
                ev.tool_input_json = acc.arguments;
                out_events.push_back(std::move(ev));
            }
            m_toolAccumulators.clear();
        }
        else if (finish == "stop" || finish == "end_turn")
        {
            if (!m_thinkingAccumulator.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::ThinkingComplete;
                ev.text = m_thinkingAccumulator;
                out_events.push_back(std::move(ev));
                m_thinkingAccumulator.clear();
            }
            if (!m_toolAccumulators.empty())
            {
                for (const auto &acc : m_toolAccumulators)
                {
                    AgentEvent ev;
                    ev.type = AgentEventType::ToolCallComplete;
                    ev.tool_name = acc.name;
                    ev.tool_call_id = acc.id;
                    ev.tool_input_json = acc.arguments;
                    out_events.push_back(std::move(ev));
                }
                m_toolAccumulators.clear();
            }
            if (!m_textAccumulator.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TextComplete;
                ev.text = m_textAccumulator;
                out_events.push_back(std::move(ev));
                m_textAccumulator.clear();
            }
            return false;
        }

        return true;
    }

    NeutralMessage OpenAIBackend::FormatToolResult(const std::string &tool_call_id,
                                                   const std::string &tool_name,
                                                   const std::string &result_json) const
    {
        NeutralMessage msg;
        msg.role = NeutralMessage::Role::Tool;
        msg.tool_call_id = tool_call_id;
        msg.tool_name = tool_name;
        msg.content = result_json;
        return msg;
    }

    std::string OpenAIBackend::GetEndpointPath() const
    {
        return "/v1/chat/completions";
    }

    std::pair<std::string, std::string> OpenAIBackend::GetAuthHeader(
        const std::string &api_key) const
    {
        return {"Authorization", "Bearer " + api_key};
    }
} // namespace pagent
