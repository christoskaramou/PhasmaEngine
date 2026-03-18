#include "RequestWorker.h"
#include "EventQueue.h"
#include "ConversationHistory.h"
#include "ToolRegistry.h"
#include "ImageDescriber.h"
#include "PhasmaAgent/VectorStore.h"

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

namespace pagent
{
    // Extract a human-readable message from a JSON HTTP error body.
    // Falls back to the raw body (truncated) if parsing fails.
    static std::string FormatHttpError(int status, const std::string &body)
    {
        try
        {
            auto j = nlohmann::json::parse(body);
            // Standard pattern: {"error": {"message": "..."}}
            if (j.contains("error") && j["error"].is_object() && j["error"].contains("message"))
                return "HTTP " + std::to_string(status) + ": " + j["error"]["message"].get<std::string>();
            // Gemini quota pattern: {"error": {"status": "...", "message": "..."}}
            if (j.contains("message"))
                return "HTTP " + std::to_string(status) + ": " + j["message"].get<std::string>();
        }
        catch (...) {}
        return "HTTP " + std::to_string(status) + ": " + body.substr(0, 200);
    }
    // Strip non-UTF-8 bytes to avoid json serialization errors (type_error.316)
    static std::string SanitizeUTF8(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80)
            {
                out += s[i++];
                continue;
            }
            int len = (c < 0xE0) ? 2 : (c < 0xF0) ? 3
                                                  : 4;
            if (i + len > s.size())
            {
                i++;
                continue;
            }
            bool ok = true;
            for (int j = 1; j < len; ++j)
                if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
                {
                    ok = false;
                    break;
                }
            if (ok)
            {
                out.append(s, i, len);
                i += len;
            }
            else
            {
                i++;
            }
        }
        return out;
    }

    // Resolve the API host from config, with provider-specific defaults
    static std::string ResolveHost(const AgentConfig &config)
    {
        if (!config.base_url.empty())
            return config.base_url;
        switch (config.provider)
        {
        case Provider::Anthropic:
            return "https://api.anthropic.com";
        case Provider::OpenAI:
            return "https://api.openai.com";
        case Provider::Google:
            return "https://generativelanguage.googleapis.com";
        case Provider::Ollama:
            return "http://localhost:11434";
        }
        return {};
    }

    // Build auth + provider-specific headers
    static std::map<std::string, std::string> BuildHeaders(const AgentConfig &config, IProviderBackend *backend)
    {
        std::map<std::string, std::string> headers;
        if (!config.api_key.empty())
        {
            const auto [authKey, authVal] = backend->GetAuthHeader(config.api_key);
            headers[authKey] = authVal;
        }
        if (config.provider == Provider::Anthropic)
        {
            headers["anthropic-version"] = "2023-06-01";
            headers["anthropic-beta"] = "prompt-caching-2024-07-31";
        }
        return headers;
    }

    RequestWorker::RequestWorker(
        const AgentConfig &config,
        IProviderBackend *backend,
        ConversationHistory &history,
        ToolRegistry &toolRegistry,
        EventQueue &eventQueue)
        : m_config(config), m_backend(backend), m_history(history), m_toolRegistry(toolRegistry), m_eventQueue(eventQueue)
    {
        m_thread = std::thread([this]
                               { ThreadFunc(); });
    }

    RequestWorker::~RequestWorker()
    {
        // Abort any in-flight HTTP request so the thread unblocks immediately.
        Cancel();
        {
            std::lock_guard lock(m_mutex);
            m_stop.store(true, std::memory_order_relaxed);
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    void RequestWorker::Cancel()
    {
        m_cancel.store(true, std::memory_order_relaxed);
        std::lock_guard lock(m_clientMutex);
        if (m_stopActiveClient)
            m_stopActiveClient();
    }

    bool RequestWorker::Submit(const std::string &user_message)
    {
        return Submit(user_message, {});
    }

    bool RequestWorker::Submit(const std::string &user_message, const std::vector<ContentPart> &attachments)
    {
        if (m_busy.load(std::memory_order_relaxed))
            return false;

        {
            std::lock_guard lock(m_mutex);
            m_pendingMessage = user_message;
            m_pendingAttachments = attachments;
            m_cancel.store(false, std::memory_order_relaxed);
            m_busy.store(true, std::memory_order_relaxed);
        }
        m_cv.notify_one();
        return true;
    }

    void RequestWorker::ThreadFunc()
    {
        while (true)
        {
            std::string message;
            std::vector<ContentPart> attachments;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this]
                          { return m_pendingMessage.has_value() || m_stop.load(std::memory_order_relaxed); });

                if (m_stop.load(std::memory_order_relaxed))
                    return;

                message = std::move(*m_pendingMessage);
                m_pendingMessage.reset();
                attachments.swap(m_pendingAttachments);
            }

            try
            {
                RunAgenticLoop(message, attachments);
            }
            catch (const std::exception &ex)
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = std::string("Unhandled exception in agent loop: ") + ex.what();
                PushEvent(std::move(ev));
            }
            catch (...)
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = "Unknown exception in agent loop";
                PushEvent(std::move(ev));
            }

            m_busy.store(false, std::memory_order_release);
        }
    }

    void RequestWorker::MaybeSummarize()
    {
        if (m_config.summarize_after_messages <= 0)
            return;

        const size_t count = m_history.EntryCount();
        if (static_cast<int>(count) <= m_config.summarize_after_messages)
            return;

        // Keep the most recent half of the threshold as intact messages
        const size_t keepRecent = static_cast<size_t>(m_config.summarize_after_messages / 2);
        const auto oldText = m_history.BuildOldMessagesText(keepRecent);
        if (oldText.empty())
            return;

        Log("Summarizing conversation (" + std::to_string(count) + " msgs, keeping last " + std::to_string(keepRecent) + ")");

        // Build a minimal summarization request
        std::vector<NeutralMessage> sumMessages;
        NeutralMessage userMsg;
        userMsg.role = NeutralMessage::Role::User;
        userMsg.content = "Summarize this conversation in 2-3 concise sentences, focusing on what was accomplished and what the user wants:\n\n" + oldText;
        sumMessages.push_back(std::move(userMsg));

        const std::string body = m_backend->BuildRequestJson(
            m_config.model, "You are a summarizer. Output only the summary, nothing else.",
            256, 0.0f, sumMessages, "");

        std::vector<AgentEvent> events;
        const auto err = HttpPost(ResolveHost(m_config), m_backend->GetEndpointPath(),
                                  BuildHeaders(m_config, m_backend), body, events);

        if (!err.empty())
        {
            Log("Summarization failed: " + err + ", falling back to compaction");
            return; // Fall back to normal compaction
        }

        // Extract text from response events
        std::string summary;
        for (const auto &ev : events)
        {
            if (ev.type == AgentEventType::TextComplete)
                summary = ev.text;
        }

        if (!summary.empty())
        {
            m_history.ReplaceOldWithSummary(summary, keepRecent);
            Log("Summarized to: " + std::to_string(summary.size()) + " chars");
        }
    }

    void RequestWorker::RunAgenticLoop(const std::string &user_message, const std::vector<ContentPart> &attachments)
    {
        // append the user turn to history.
        {
            NeutralMessage msg;
            msg.role = NeutralMessage::Role::User;
            msg.content = user_message;
            if (!attachments.empty())
            {
                // Build multimodal parts: text first, then attachments
                if (!user_message.empty())
                    msg.parts.push_back({ContentPart::Type::Text, user_message, ""});
                for (const auto &att : attachments)
                    msg.parts.push_back(att);
            }
            m_history.Append(std::move(msg));
        }

        // Summarize old messages if history is getting large
        MaybeSummarize();

        const int maxRounds = m_config.max_tool_rounds > 0 ? m_config.max_tool_rounds : 10;

        // RAG: search codebase store for relevant context
        std::string ragContext;
        if (m_codebaseStore && m_codebaseStore->Size() > 0 && m_config.embedding_provider)
        {
            std::string queryText;
            auto messages = m_history.GetMessages(m_config.max_history_messages);
            for (auto it = messages.rbegin(); it != messages.rend(); ++it)
            {
                if (it->role == NeutralMessage::Role::User)
                {
                    queryText = it->content;
                    break;
                }
            }
            if (!queryText.empty())
            {
                auto queryVec = m_config.embedding_provider->Embed(queryText);
                if (queryVec.empty())
                    Log("RAG: embedding query failed (empty result)");
                if (!queryVec.empty())
                {
                    auto results = m_codebaseStore->Search(queryVec, m_config.rag_top_k, m_config.rag_min_score);

                    if (!results.empty())
                    {
                        ragContext = "\n\n[Relevant context:\n";
                        int totalChars = 0;
                        for (const auto &r : results)
                        {
                            std::string entry = "- " + SanitizeUTF8(r.entry->content) + "\n";
                            if (totalChars + static_cast<int>(entry.size()) > m_config.rag_max_context_chars)
                                break;
                            ragContext += entry;
                            totalChars += static_cast<int>(entry.size());
                        }
                        ragContext += "]";
                        Log("RAG: injected " + std::to_string(results.size()) + " codebase context entries");

                        AgentEvent ragEv;
                        ragEv.type = AgentEventType::Info;
                        ragEv.text = "RAG: " + std::to_string(results.size()) + " codebase entries injected";
                        PushEvent(std::move(ragEv));
                    }
                }
            }
        }

        for (int round = 0; round < maxRounds; ++round)
        {
            if (m_cancel.load(std::memory_order_relaxed))
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = "cancelled";
                PushEvent(std::move(ev));
                return;
            }

            // build request
            auto messages = m_history.GetMessages(m_config.max_history_messages);

            // If backend doesn't support vision, describe images via Gemini fallback
            if (!m_backend->SupportsVision() && !m_config.gemini_api_key_for_vision.empty())
            {
                for (auto &msg : messages)
                {
                    if (msg.parts.empty())
                        continue;
                    bool hasImages = false;
                    for (const auto &p : msg.parts)
                        if (p.type == ContentPart::Type::ImageBase64)
                        {
                            hasImages = true;
                            break;
                        }
                    if (!hasImages)
                        continue;

                    // Replace image parts with text descriptions
                    std::vector<ContentPart> newParts;
                    for (const auto &p : msg.parts)
                    {
                        if (p.type == ContentPart::Type::ImageBase64)
                        {
                            std::string desc = ImageDescriber::Describe(
                                p.data, p.mime_type, m_config.gemini_api_key_for_vision,
                                m_config.custom_http_handler);
                            newParts.push_back({ContentPart::Type::Text, "[Image: " + desc + "]", ""});
                        }
                        else
                        {
                            newParts.push_back(p);
                        }
                    }
                    msg.parts = std::move(newParts);
                }
            }

            const auto toolsSchemaJson = m_toolRegistry.GenerateSchemaJson(*m_backend);

            // Append precomputed RAG context to system prompt for every round
            std::string systemPrompt = SanitizeUTF8(m_config.system_prompt + ragContext);

            if (m_config.log_callback)
                m_config.log_callback("[PAgent] Round " + std::to_string(round) + ": " + std::to_string(m_toolRegistry.GetToolCount()) + " tools, " + std::to_string(messages.size()) + " msgs, model='" + m_config.model + "', provider=" + std::to_string(static_cast<int>(m_config.provider)));

            const std::string body = m_backend->BuildRequestJson(
                m_config.model,
                systemPrompt,
                m_config.max_tokens,
                m_config.temperature,
                messages,
                toolsSchemaJson);

            if (m_config.log_callback)
                m_config.log_callback("[PAgent] Body size=" + std::to_string(body.size()));

            const std::string host = ResolveHost(m_config);
            const std::string path = m_backend->GetEndpointPath();
            const auto headers = BuildHeaders(m_config, m_backend);

            // HTTP POST + stream parse
            if (m_config.log_callback)
            {
                m_config.log_callback("[PAgent] POST " + host + path + " ...");
                m_config.log_callback("[PAgent] Body: " + body);
            }
            std::vector<AgentEvent> turnEvents;
            const auto errMsg = HttpPost(host, path, headers, body, turnEvents);
            if (m_config.log_callback)
                m_config.log_callback("[PAgent] POST done, err='" + errMsg + "', events=" + std::to_string(turnEvents.size()));

            // If the model doesn't support tools, retry without them
            if (!errMsg.empty() && (errMsg.find("does not support tools") != std::string::npos ||
                                    errMsg.find("not enabled for model") != std::string::npos ||
                                    errMsg.find("calling is not enabled") != std::string::npos))
            {
                if (m_config.log_callback)
                    m_config.log_callback("[PAgent] Model does not support tools, retrying without tools");

                const std::string bodyNoTools = m_backend->BuildRequestJson(
                    m_config.model, m_config.system_prompt, m_config.max_tokens,
                    m_config.temperature, messages, "");

                turnEvents.clear();
                const auto retryErr = HttpPost(host, path, headers, bodyNoTools, turnEvents);
                if (!retryErr.empty())
                {
                    AgentEvent ev;
                    ev.type = AgentEventType::Error;
                    ev.error_message = retryErr;
                    PushEvent(std::move(ev));
                    return;
                }
            }
            else if (!errMsg.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = errMsg;
                PushEvent(std::move(ev));
                return;
            }

            if (m_config.log_callback && !errMsg.empty())
                m_config.log_callback("[PAgent] Response error: " + errMsg);

            // push all streamed events and collect tool calls + full text
            std::string fullText;
            std::string fullReasoning;
            std::string fullThoughtSignature;
            std::vector<AgentEvent> toolCallCompletes;
            bool hasContent = false;

            for (auto &ev : turnEvents)
            {
                if (!ev.thought_signature.empty())
                    fullThoughtSignature = ev.thought_signature;

                if (ev.type == AgentEventType::TextComplete)
                {
                    fullText = ev.text;
                    hasContent = true;
                }
                else if (ev.type == AgentEventType::ThinkingComplete)
                {
                    fullReasoning = ev.text;
                    hasContent = true;
                }
                else if (ev.type == AgentEventType::ToolCallComplete)
                {
                    toolCallCompletes.push_back(ev);
                    hasContent = true;
                }

                PushEvent(ev);
            }

            // If the response had events (e.g. Usage) but no actual content or tool calls,
            // push a dummy complete event to satisfy the consumer.
            if (!hasContent && !turnEvents.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TextComplete;
                ev.text = "";
                PushEvent(std::move(ev));

                // Don't append empty responses to history as it might confuse certain models
                return;
            }

            // build and append the assistant turn to history
            {
                NeutralMessage assistantMsg;
                assistantMsg.role = NeutralMessage::Role::Assistant;
                assistantMsg.content = fullText;
                assistantMsg.reasoning = fullReasoning;
                assistantMsg.thought_signature = fullThoughtSignature;
                for (const auto &tc : toolCallCompletes)
                {
                    NeutralMessage::ToolCall call;
                    call.id = tc.tool_call_id;
                    call.name = tc.tool_name;
                    call.arguments_json = tc.tool_input_json;
                    call.thought_signature = tc.thought_signature; // Per-call signature
                    assistantMsg.tool_calls.push_back(std::move(call));
                }
                m_history.Append(std::move(assistantMsg));
            }

            // if no tool calls, the model is done
            if (toolCallCompletes.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TurnComplete;
                PushEvent(std::move(ev));
                return;
            }

            // dispatch each tool call
            for (const auto &tc : toolCallCompletes)
            {
                if (m_cancel.load(std::memory_order_relaxed))
                {
                    AgentEvent ev;
                    ev.type = AgentEventType::Error;
                    ev.error_message = "cancelled";
                    PushEvent(std::move(ev));
                    return;
                }

                Log("Dispatching tool: " + tc.tool_name);
                auto resultJson = SanitizeUTF8(m_toolRegistry.Dispatch(tc.tool_name, tc.tool_input_json));

                // Truncate large tool results to save tokens (skip read-type tools that need full content)
                if (m_config.max_tool_result_chars > 0 &&
                    static_cast<int>(resultJson.size()) > m_config.max_tool_result_chars &&
                    tc.tool_name.find("read") == std::string::npos)
                {
                    resultJson = resultJson.substr(0, m_config.max_tool_result_chars) +
                                 "...(truncated, " + std::to_string(resultJson.size()) + " total chars)";
                }

                AgentEvent resultEv;
                resultEv.type = AgentEventType::ToolResult;
                resultEv.tool_name = tc.tool_name;
                resultEv.tool_call_id = tc.tool_call_id;
                resultEv.text = resultJson;
                PushEvent(resultEv);

                m_history.Append(m_backend->FormatToolResult(tc.tool_call_id, tc.tool_name, resultJson));
            }
        }

        // hit max_tool_rounds
        AgentEvent ev;
        ev.type = AgentEventType::Error;
        ev.error_message = "Reached max_tool_rounds limit";
        PushEvent(std::move(ev));
    }

    std::string RequestWorker::HttpPost(
        const std::string &host,
        const std::string &path,
        const std::map<std::string, std::string> &headers,
        const std::string &body,
        std::vector<AgentEvent> &out_events)
    {
        // use custom_http_handler if provided
        if (m_config.custom_http_handler)
        {
            auto [status, response] = m_config.custom_http_handler("POST", host + path, headers, body);
            if (status != 200)
                return FormatHttpError(status, response);

            StreamParser parser(m_backend);
            parser.Reset(m_backend);
            parser.Feed(response.data(), response.size(), out_events);
            return {};
        }

        // strip protocol prefix and detect http vs https
        std::string cleanHost = host;
        bool useHttps = true;
        const std::string httpsPrefix = "https://";
        const std::string httpPrefix = "http://";
        if (cleanHost.rfind(httpsPrefix, 0) == 0)
            cleanHost = cleanHost.substr(httpsPrefix.size());
        else if (cleanHost.rfind(httpPrefix, 0) == 0)
        {
            cleanHost = cleanHost.substr(httpPrefix.size());
            useHttps = false;
        }
        else if (!m_config.base_url.empty())
        {
            useHttps = false;
        }

        // split host from any path prefix in base_url (e.g. "generativelanguage.googleapis.com/v1beta/openai")
        std::string pathPrefix;
        {
            auto slashPos = cleanHost.find('/');
            if (slashPos != std::string::npos)
            {
                pathPrefix = cleanHost.substr(slashPos); // e.g. "/v1beta/openai"
                cleanHost = cleanHost.substr(0, slashPos);
            }
        }
        // When a path prefix is set, it already contains the version (e.g. /v1beta/openai).
        // Strip the leading /v1 from the endpoint path to avoid duplication:
        //   pathPrefix="/v1beta/openai" + path="/v1/chat/completions" -> "/v1beta/openai/chat/completions"
        std::string endpointSuffix = path;
        if (!pathPrefix.empty() && endpointSuffix.rfind("/v1/", 0) == 0)
            endpointSuffix = endpointSuffix.substr(3); // strip "/v1"
        const std::string fullPath = pathPrefix + endpointSuffix;

        // build httplib headers
        httplib::Headers hlHeaders;
        for (const auto &[k, v] : headers)
            hlHeaders.emplace(k, v);

        StreamParser parser(m_backend);
        parser.Reset(m_backend);

        std::string responseBody;
        std::string errorMsg;

        if (!useHttps)
        {
            // plain HTTP — always available (e.g. Ollama on localhost)
            httplib::Client cli(cleanHost);
            cli.set_read_timeout(120);
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = [&cli]
                { cli.stop(); };
            }
            auto res = cli.Post(fullPath, hlHeaders, body, "application/json");
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = nullptr;
            }
            if (!res || res->status != 200)
                errorMsg = res ? FormatHttpError(res->status, res->body)
                               : ("Connection failed: " + httplib::to_string(res.error()));
            else
                responseBody = std::move(res->body);
        }
        else
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(cleanHost);
            cli.set_read_timeout(120);
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = [&cli]
                { cli.stop(); };
            }
            auto res = cli.Post(fullPath, hlHeaders, body, "application/json");
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = nullptr;
            }
            if (!res || res->status != 200)
                errorMsg = res ? FormatHttpError(res->status, res->body) : "Connection failed (SSL)";
            else
                responseBody = std::move(res->body);
#else
            errorMsg = "HTTPS not available: install OpenSSL (vcpkg install openssl:x64-windows) "
                       "or use http:// base_url for local endpoints (e.g. Ollama).";
#endif
        }

        if (errorMsg.empty() && !responseBody.empty())
        {
            parser.Feed(responseBody.data(), responseBody.size(), out_events);

            // If streaming response produced no events, the response may be a raw JSON error
            // (e.g. Gemini returns {"error": {...}} without SSE framing)
            if (out_events.empty())
                return "Empty response from API: " + responseBody.substr(0, 500);
        }

        return errorMsg;
    }

    void RequestWorker::PushEvent(AgentEvent ev)
    {
        m_eventQueue.Push(std::move(ev));
    }

    void RequestWorker::Log(const std::string &msg) const
    {
        if (m_config.log_callback)
            m_config.log_callback("[PhasmaAgent] " + msg);
    }
} // namespace pagent
