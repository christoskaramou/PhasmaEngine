#include "GoogleBackend.h"
#include <nlohmann/json.hpp>

namespace pagent
{
    using json = nlohmann::json;

    // Convert a NeutralMessage to Gemini native "contents" entry.
    // Returns null for System messages (handled separately via system_instruction).
    static json MessageToGeminiJson(const NeutralMessage &msg)
    {
        switch (msg.role)
        {
        case NeutralMessage::Role::User:
        {
            json parts = json::array();
            if (!msg.parts.empty())
            {
                for (const auto &part : msg.parts)
                {
                    if (part.type == ContentPart::Type::Text)
                        parts.push_back({{"text", part.data}});
                    else if (part.type == ContentPart::Type::ImageBase64)
                        parts.push_back({{"inlineData", {{"mimeType", part.mime_type}, {"data", part.data}}}});
                }
            }
            else
            {
                parts.push_back({{"text", msg.content}});
            }
            return {{"role", "user"}, {"parts", parts}};
        }

        case NeutralMessage::Role::Assistant:
        {
            json parts = json::array();
            if (!msg.content.empty())
                parts.push_back({{"text", msg.content}});

            for (const auto &tc : msg.tool_calls)
            {
                json args = json::object();
                try
                {
                    args = json::parse(tc.arguments_json);
                }
                catch (...)
                {
                }
                json part = {{"functionCall", {{"name", tc.name}, {"args", args}}}};
                if (!tc.thought_signature.empty())
                    part["thoughtSignature"] = tc.thought_signature;
                parts.push_back(std::move(part));
            }
            return {{"role", "model"}, {"parts", parts}};
        }

        case NeutralMessage::Role::Tool:
        {
            json response = json::object();
            try
            {
                response = json::parse(msg.content);
            }
            catch (...)
            {
                response = {{"result", msg.content}};
            }
            json part = {{"functionResponse", {{"name", msg.tool_name}, {"response", response}}}};
            return {{"role", "user"}, {"parts", json::array({part})}};
        }

        default:
            return {};
        }
    }

    std::string GoogleBackend::BuildRequestJson(
        const std::string &model,
        const std::string &system_prompt,
        int max_tokens,
        float temperature,
        const std::vector<NeutralMessage> &messages,
        const std::string &tools_schema_json) const
    {
        json body;

        m_model = model.empty() ? "gemini-2.5-flash" : model;

        // System instruction
        if (!system_prompt.empty())
            body["system_instruction"] = {{"parts", json::array({{{"text", system_prompt}}})}};

        // Generation config
        json genConfig;
        genConfig["maxOutputTokens"] = max_tokens;
        genConfig["temperature"] = temperature;

        // Thinking models get a thinking budget
        if (m_model.find("2.5") != std::string::npos ||
            m_model.find("3.") != std::string::npos ||
            m_model.find("-thinking") != std::string::npos)
        {
            genConfig["thinkingConfig"] = {{"thinkingBudget", max_tokens}};
        }
        body["generationConfig"] = genConfig;

        // Contents (messages)
        json contents = json::array();
        for (const auto &msg : messages)
        {
            if (msg.role == NeutralMessage::Role::System)
                continue;

            auto j = MessageToGeminiJson(msg);
            if (j.is_null())
                continue;

            // Merge consecutive tool results into a single user turn
            // (Gemini requires all functionResponse parts in one user entry)
            if (msg.role == NeutralMessage::Role::Tool && !contents.empty() &&
                contents.back().value("role", "") == "user" &&
                contents.back()["parts"].is_array() &&
                !contents.back()["parts"].empty() &&
                contents.back()["parts"][0].contains("functionResponse"))
            {
                for (auto &part : j["parts"])
                    contents.back()["parts"].push_back(std::move(part));
            }
            else
            {
                contents.push_back(std::move(j));
            }
        }
        body["contents"] = std::move(contents);

        // Tools
        if (!tools_schema_json.empty())
        {
            try
            {
                auto toolsArr = json::parse(tools_schema_json);
                if (toolsArr.is_array() && !toolsArr.empty())
                    body["tools"] = toolsArr;
            }
            catch (...)
            {
            }
        }

        ResetStreamState();
        return body.dump(-1, ' ', false, json::error_handler_t::replace);
    }

    std::string GoogleBackend::BuildToolsJson(const std::vector<ToolDefinition> &tools) const
    {
        if (tools.empty())
            return "[]";

        // Gemini format: [{functionDeclarations: [{name, description, parameters}]}]
        json declarations = json::array();
        for (const auto &tool : tools)
        {
            json properties = json::object();
            json required = json::array();

            for (const auto &prop : tool.properties)
            {
                static const char *typeNames[] = {
                    "STRING", "NUMBER", "BOOLEAN", "INTEGER", "ARRAY", "OBJECT"};
                json p;
                p["type"] = typeNames[static_cast<int>(prop.type)];
                p["description"] = prop.description;
                properties[prop.name] = p;

                if (prop.required)
                    required.push_back(prop.name);
            }

            json decl = {
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", {
                                   {"type", "OBJECT"},
                                   {"properties", properties},
                                   {"required", required},
                               }},
            };
            declarations.push_back(std::move(decl));
        }

        json arr = json::array();
        arr.push_back({{"functionDeclarations", declarations}});
        return arr.dump(-1, ' ', false, json::error_handler_t::replace);
    }

    void GoogleBackend::ResetStreamState() const
    {
        m_textAccumulator.clear();
        m_thinkingAccumulator.clear();
        m_toolAccumulators.clear();
        m_toolCallCounter = 0;
    }

    bool GoogleBackend::ParseStreamEvent(
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

        // Error response
        if (j.contains("error"))
        {
            AgentEvent ev;
            ev.type = AgentEventType::Error;
            ev.error_message = j["error"].value("message", "Google API error");
            out_events.push_back(std::move(ev));
            return false;
        }

        // Usage metadata
        if (j.contains("usageMetadata") && !j["usageMetadata"].is_null())
        {
            const auto &usage = j["usageMetadata"];
            AgentEvent ev;
            ev.type = AgentEventType::Usage;
            ev.input_tokens = usage.value("promptTokenCount", 0);
            ev.output_tokens = usage.value("candidatesTokenCount", 0);
            ev.cache_read_tokens = usage.value("cachedContentTokenCount", 0);
            out_events.push_back(std::move(ev));
        }

        // Candidates
        const auto &candidates = j.value("candidates", json::array());
        if (candidates.empty())
            return true;

        const auto &candidate = candidates[0];
        const auto &content = candidate.value("content", json::object());
        const auto &parts = content.value("parts", json::array());

        for (const auto &part : parts)
        {
            // Thinking/reasoning text (thought: true)
            if (part.contains("thought") && part["thought"].get<bool>() && part.contains("text"))
            {
                const auto text = part["text"].get<std::string>();
                m_thinkingAccumulator += text;

                AgentEvent ev;
                ev.type = AgentEventType::ThinkingDelta;
                ev.text = text;
                out_events.push_back(std::move(ev));
            }
            // Regular text
            else if (part.contains("text") && !part.contains("thought"))
            {
                const auto text = part["text"].get<std::string>();
                m_textAccumulator += text;

                AgentEvent ev;
                ev.type = AgentEventType::TextDelta;
                ev.text = text;
                out_events.push_back(std::move(ev));
            }
            // Function call
            else if (part.contains("functionCall"))
            {
                const auto &fc = part["functionCall"];
                ToolCallAccumulator acc;
                acc.name = fc.value("name", "");
                acc.arguments = fc.value("args", json::object()).dump();
                // Extract thought signature if present (required by Gemini thinking models)
                if (part.contains("thoughtSignature") && part["thoughtSignature"].is_string())
                    acc.thought_signature = part["thoughtSignature"].get<std::string>();
                m_toolAccumulators.push_back(std::move(acc));

                // Emit begin event
                AgentEvent beginEv;
                beginEv.type = AgentEventType::ToolCallBegin;
                beginEv.tool_name = m_toolAccumulators.back().name;
                beginEv.tool_call_id = "google_tc_" + std::to_string(m_toolCallCounter++);
                beginEv.thought_signature = m_toolAccumulators.back().thought_signature;
                out_events.push_back(std::move(beginEv));
            }
        }

        // Check finish reason
        const auto finishReason = candidate.value("finishReason", std::string{});
        if (!finishReason.empty())
        {
            // Emit thinking complete
            if (!m_thinkingAccumulator.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::ThinkingComplete;
                ev.text = m_thinkingAccumulator;
                out_events.push_back(std::move(ev));
                m_thinkingAccumulator.clear();
            }

            // Emit tool call completes
            int tcIdx = 0;
            for (const auto &acc : m_toolAccumulators)
            {
                AgentEvent ev;
                ev.type = AgentEventType::ToolCallComplete;
                ev.tool_name = acc.name;
                ev.tool_call_id = "google_tc_" + std::to_string(tcIdx++);
                ev.tool_input_json = acc.arguments;
                ev.thought_signature = acc.thought_signature;
                out_events.push_back(std::move(ev));
            }
            m_toolAccumulators.clear();

            // Emit text complete
            if (!m_textAccumulator.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TextComplete;
                ev.text = m_textAccumulator;
                out_events.push_back(std::move(ev));
                m_textAccumulator.clear();
            }

            return false; // stream done
        }

        return true;
    }

    NeutralMessage GoogleBackend::FormatToolResult(
        const std::string &tool_call_id,
        const std::string &tool_name,
        const std::string &result_json) const
    {
        NeutralMessage msg;
        msg.role = NeutralMessage::Role::Tool;
        msg.tool_call_id = tool_call_id;
        msg.tool_name = tool_name.empty() ? "unknown_tool" : tool_name;
        msg.content = result_json;
        return msg;
    }

    std::string GoogleBackend::GetEndpointPath() const
    {
        return "/v1beta/models/" + m_model + ":streamGenerateContent?alt=sse";
    }

    std::pair<std::string, std::string> GoogleBackend::GetAuthHeader(
        const std::string &api_key) const
    {
        return {"x-goog-api-key", api_key};
    }
} // namespace pagent
