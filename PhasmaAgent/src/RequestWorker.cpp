#include "RequestWorker.h"
#include "EventQueue.h"
#include "ConversationHistory.h"
#include "ToolRegistry.h"

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib/httplib.h>

namespace pagent
{
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
        {
            std::lock_guard lock(m_mutex);
            m_stop.store(true, std::memory_order_relaxed);
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    bool RequestWorker::Submit(const std::string &user_message)
    {
        if (m_busy.load(std::memory_order_relaxed))
            return false;

        {
            std::lock_guard lock(m_mutex);
            m_pendingMessage = user_message;
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
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this]
                          { return m_pendingMessage.has_value() || m_stop.load(std::memory_order_relaxed); });

                if (m_stop.load(std::memory_order_relaxed))
                    return;

                message = std::move(*m_pendingMessage);
                m_pendingMessage.reset();
            }

            try
            {
                RunAgenticLoop(message);
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

    void RequestWorker::RunAgenticLoop(const std::string &user_message)
    {
        // append the user turn to history.
        {
            NeutralMessage msg;
            msg.role = NeutralMessage::Role::User;
            msg.content = user_message;
            m_history.Append(std::move(msg));
        }

        const int maxRounds = m_config.max_tool_rounds > 0 ? m_config.max_tool_rounds : 10;

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
            const auto messages = m_history.GetMessages();
            const auto toolsSchemaJson = m_toolRegistry.GenerateSchemaJson(*m_backend);

            const std::string body = m_backend->BuildRequestJson(
                m_config.model,
                m_config.system_prompt,
                m_config.max_tokens,
                m_config.temperature,
                messages,
                toolsSchemaJson);

            // determine host/path
            std::string host = m_config.base_url.empty()
                                   ? "api.anthropic.com"
                                   : m_config.base_url;
            const std::string path = m_backend->GetEndpointPath();

            // build headers
            const auto [authKey, authVal] = m_backend->GetAuthHeader(m_config.api_key);
            std::map<std::string, std::string> headers = {
                {authKey, authVal},
                {"content-type", "application/json"},
            };
            // anthropic requires this header, harmless for other providers
            if (m_config.provider == Provider::Anthropic)
                headers["anthropic-version"] = "2023-06-01";

            // HTTP POST + stream parse
            std::vector<AgentEvent> turnEvents;
            const auto errMsg = HttpPost(host, path, headers, body, turnEvents);

            if (!errMsg.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = errMsg;
                PushEvent(std::move(ev));
                return;
            }

            // push all streamed events and collect tool calls + full text
            std::string fullText;
            std::vector<AgentEvent> toolCallCompletes;

            for (auto &ev : turnEvents)
            {
                if (ev.type == AgentEventType::TextComplete)
                    fullText = ev.text;
                else if (ev.type == AgentEventType::ToolCallComplete)
                    toolCallCompletes.push_back(ev);

                PushEvent(ev);
            }

            // build and append the assistant turn to history
            {
                NeutralMessage assistantMsg;
                assistantMsg.role = NeutralMessage::Role::Assistant;
                assistantMsg.content = fullText;
                for (const auto &tc : toolCallCompletes)
                {
                    NeutralMessage::ToolCall call;
                    call.id = tc.tool_call_id;
                    call.name = tc.tool_name;
                    call.arguments_json = tc.tool_input_json;
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
                const auto resultJson = m_toolRegistry.Dispatch(tc.tool_name, tc.tool_input_json);

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
                return "HTTP error " + std::to_string(status) + ": " + response;

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
            cli.set_read_timeout(30);
            auto res = cli.Post(path, hlHeaders, body, "application/json");
            if (!res || res->status != 200)
                errorMsg = res ? ("HTTP " + std::to_string(res->status) + ": " + res->body) : "Connection failed";
            else
                responseBody = std::move(res->body);
        }
        else
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(cleanHost);
            cli.set_read_timeout(30);
            auto res = cli.Post(path, hlHeaders, body, "application/json");
            if (!res || res->status != 200)
                errorMsg = res ? ("HTTP " + std::to_string(res->status) + ": " + res->body) : "Connection failed (SSL)";
            else
                responseBody = std::move(res->body);
#else
            errorMsg = "HTTPS not available: install OpenSSL (vcpkg install openssl:x64-windows) "
                       "or use http:// base_url for local endpoints (e.g. Ollama).";
#endif
        }

        if (errorMsg.empty() && !responseBody.empty())
            parser.Feed(responseBody.data(), responseBody.size(), out_events);

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
