// PhasmaEditor/Code/GUI/AI/AICompletionService.cpp
#include "AICompletionService.h"
#include "Base/Log.h"
#include <sstream>

namespace pe
{
    static constexpr const char *kLuaSystem =
        "You are a Lua autocomplete assistant for PhasmaEngine.\n"
        "Engine API:\n"
        "  transform:set_position(vec3(x,y,z)), transform:get_position() -> vec3\n"
        "  transform:set_rotation(vec3(rx,ry,rz)), transform:set_scale(vec3(sx,sy,sz))\n"
        "  self:find_child(name), vec3(x,y,z), vec4(x,y,z,w)\n"
        "  hooks{update=function() end, init=fn, destroy=fn}\n"
        "  exposed{key=default}\n"
        "  engine.get_metrics() returns {fps, delta_ms}\n"
        "IMPORTANT: hooks update takes NO arguments. Use engine.get_metrics().delta_ms for delta time.\n"
        "IMPORTANT: set_position takes a vec3, NOT separate floats.\n"
        "The user's code has a <cursor> marker showing where the caret is.\n"
        "Reply ONLY with the new text to INSERT at <cursor>.\n"
        "Never repeat or rewrite existing code. Never include the <cursor> marker.\n"
        "If the cursor is on or after a comment, generate the code the comment describes.\n"
        "Always produce at least a short completion. No explanation.";

    static constexpr const char *kHlslSystem =
        "You are an HLSL autocomplete assistant for PhasmaEngine (Vulkan, HLSL 2021).\n"
        "Conventions: ByteAddressBuffer for mesh/material data, push constants via [[vk::push_constant]].\n"
        "GBuffer layout: albedo R8G8B8A8, normal R16G16B16A16, depth D32.\n"
        "PBR material fields: albedo, roughness, metallic, emissive, transmissionFactor.\n"
        "The user's code has a <cursor> marker showing where the caret is.\n"
        "Reply ONLY with the new text to INSERT at <cursor>.\n"
        "Never repeat or rewrite existing code. Never include the <cursor> marker.\n"
        "If the cursor is on or after a comment, generate the code the comment describes.\n"
        "Always produce at least a short completion. No explanation.";

    static constexpr const char *kPropSystem =
        "You are a game engine property value assistant for PhasmaEngine.\n"
        "Suggest one valid replacement value appropriate for a 3D game engine scene.\n"
        "Reply with the value only. No explanation, no units, no quotes.";

    pagent::Provider AICompletionService::ParseProvider(const std::string &s)
    {
        if (s == "OpenAI")
            return pagent::Provider::OpenAI;
        if (s == "Google")
            return pagent::Provider::Google;
        if (s == "Ollama")
            return pagent::Provider::Ollama;
        if (s == "GoogleVertex")
            return pagent::Provider::GoogleVertex;
        return pagent::Provider::Anthropic;
    }

    pagent::AgentConfig AICompletionService::BuildConfig(const std::string &provider_str,
                                                         const std::string &api_key,
                                                         const std::string &model,
                                                         const std::string &base_url,
                                                         const std::string &system_prompt,
                                                         int max_tokens)
    {
        pagent::AgentConfig cfg;
        cfg.provider = ParseProvider(provider_str);
        cfg.api_key = api_key;
        cfg.model = model;
        cfg.base_url = base_url;
        cfg.system_prompt = system_prompt;
        cfg.max_tokens = max_tokens;
        cfg.temperature = 0.2f;
        cfg.max_tool_rounds = 1;
        cfg.max_history_messages = 2;
        return cfg;
    }

    void AICompletionService::SetupSlot(AgentSlot &slot,
                                        const std::string &provider_str,
                                        const std::string &api_key,
                                        const std::string &model,
                                        const std::string &base_url,
                                        const std::string &system_prompt,
                                        int max_tokens)
    {
        slot.agent.emplace(BuildConfig(provider_str, api_key, model, base_url, system_prompt, max_tokens));
        slot.generation = 0;
        slot.cbGeneration = 0;
        slot.callback = nullptr;

        slot.agent->SetEventCallback([&slot](const pagent::AgentEvent &e)
                                     {
            PE_INFO("[Completion] Event: type=%d text_len=%d err='%s'",
                    (int)e.type, (int)e.text.size(), e.error_message.c_str());

            if (e.type == pagent::AgentEventType::TextComplete)
            {
                if (slot.callback && slot.cbGeneration == slot.generation)
                {
                    PE_INFO("[Completion] TextComplete → firing callback (%d chars)", (int)e.text.size());
                    auto cb = std::move(slot.callback);
                    slot.callback = nullptr;
                    cb(e.text);
                }
                else
                {
                    PE_INFO("[Completion] TextComplete → stale (cb=%d gen=%u cbGen=%u)",
                            (bool)slot.callback, slot.generation, slot.cbGeneration);
                }
            }
            else if (e.type == pagent::AgentEventType::Error ||
                     e.type == pagent::AgentEventType::TurnComplete)
            {
                if (e.type == pagent::AgentEventType::Error && slot.callback &&
                    slot.cbGeneration == slot.generation)
                {
                    PE_WARN("[Completion] Error → firing callback with empty: %s", e.error_message.c_str());
                    auto cb = std::move(slot.callback);
                    slot.callback = nullptr;
                    cb("");
                }
                else
                {
                    slot.callback = nullptr;
                }
            } });
    }

    void AICompletionService::SendToSlot(AgentSlot &slot,
                                         const std::string &userMessage,
                                         std::function<void(std::string)> callback)
    {
        if (!slot.agent)
            return;

        // Bump generation so any in-flight callback for previous request is ignored
        slot.generation++;

        if (slot.agent->IsBusy())
            slot.agent->CancelPending();

        // ClearHistory before new request (safe: called outside Poll(), on main thread)
        slot.agent->ClearHistory();

        PE_INFO("[Completion] SendToSlot: gen=%u msg_len=%d", slot.generation, (int)userMessage.size());
        if (slot.agent->Send(userMessage))
        {
            PE_INFO("[Completion] Send() succeeded, callback stored");
            slot.callback = std::move(callback);
            slot.cbGeneration = slot.generation;
        }
        else
        {
            PE_WARN("[Completion] Send() failed (agent busy)");
            // Send failed (still busy after cancel) — immediately notify caller
            callback("");
        }
    }

    void AICompletionService::Init(const std::string &provider_str,
                                   const std::string &api_key,
                                   const std::string &model,
                                   const std::string &base_url)
    {
        m_enabled = false;
        m_lua.agent.reset();
        m_hlsl.agent.reset();
        m_prop.agent.reset();

        auto p = ParseProvider(provider_str);
        bool needsKey = (p != pagent::Provider::Ollama) && base_url.empty();
        if (needsKey && api_key.empty())
        {
            PE_INFO("[Completion] Disabled: provider '%s' requires api_key.", provider_str.c_str());
            return;
        }

        SetupSlot(m_lua, provider_str, api_key, model, base_url, kLuaSystem, 200);
        SetupSlot(m_hlsl, provider_str, api_key, model, base_url, kHlslSystem, 200);
        SetupSlot(m_prop, provider_str, api_key, model, base_url, kPropSystem, 100);

        m_enabled = true;
        PE_INFO("[Completion] Enabled: provider=%s model=%s", provider_str.c_str(), model.c_str());
    }

    std::string AICompletionService::BuildContext(const std::string &fullText, int cursorLine, int radius)
    {
        std::istringstream stream(fullText);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(stream, line))
            lines.push_back(line);

        // Handle empty buffer or cursor past last line (EOF blank line)
        if (lines.empty())
            return "<cursor>\n";

        // Ensure cursorLine is within bounds; if at EOF, append a blank line
        if (cursorLine >= (int)lines.size())
        {
            lines.push_back("");
        }

        int start = std::max(0, cursorLine - radius);
        int end = std::min((int)lines.size(), cursorLine + radius + 1);

        std::string result;
        for (int i = start; i < end; ++i)
        {
            if (i == cursorLine)
                result += lines[i] + "<cursor>\n";
            else
                result += lines[i] + "\n";
        }
        return result;
    }

    void AICompletionService::RequestScriptCompletion(const std::string &context,
                                                      CompletionLanguage lang,
                                                      std::function<void(std::string)> callback)
    {
        if (!m_enabled)
            return;
        if (lang == CompletionLanguage::Lua)
            SendToSlot(m_lua, context, std::move(callback));
        else
            SendToSlot(m_hlsl, context, std::move(callback));
    }

    void AICompletionService::RequestPropertySuggestion(const std::string &node_type,
                                                        const std::string &field_name,
                                                        const std::string &current_value,
                                                        std::function<void(std::string)> callback)
    {
        if (!m_enabled)
            return;
        std::string prompt = "Node type: " + node_type +
                             ". Field: " + field_name +
                             ". Current value: " + current_value + ".";
        SendToSlot(m_prop, prompt, std::move(callback));
    }

    void AICompletionService::Poll()
    {
        if (m_lua.agent)
            m_lua.agent->Poll();
        if (m_hlsl.agent)
            m_hlsl.agent->Poll();
        if (m_prop.agent)
            m_prop.agent->Poll();
    }
} // namespace pe
