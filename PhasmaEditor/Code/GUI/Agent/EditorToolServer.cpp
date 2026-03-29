#include "GUI/Agent/EditorToolServer.h"
#include "GUI/Agent/EditorToolCatalog.h"
#include "GUI/Agent/EditorToolRuntime.h"
#include "GUI/Agent/EditorToolSchemaUtils.h"
#include "GUI/GUI.h"
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

namespace pe
{
    namespace
    {
        static constexpr const char *kMcpVersion = "2025-06-18";

        static std::string DumpJson(const nlohmann::json &j)
        {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        static bool IsLocalOrigin(const std::string &origin)
        {
            if (origin.empty())
                return true;
            // Parse the hostname from origin (scheme://hostname or scheme://hostname:port)
            std::string host = origin;
            const auto schemeEnd = host.find("://");
            if (schemeEnd != std::string::npos)
                host = host.substr(schemeEnd + 3);
            // Strip port and any trailing path
            const auto portOrPath = host.find_first_of(":/");
            if (portOrPath != std::string::npos)
                host = host.substr(0, portOrPath);
            return host == "localhost" || host == "127.0.0.1" || host == "[::1]" || host == "::1";
        }

        static nlohmann::json MakeJsonRpcResult(const nlohmann::json &id, const nlohmann::json &result)
        {
            return nlohmann::json{
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", result},
            };
        }

        static nlohmann::json MakeJsonRpcError(const nlohmann::json &id, int code, const std::string &message)
        {
            return nlohmann::json{
                {"jsonrpc", "2.0"},
                {"id", id},
                {"error", {{"code", code}, {"message", message}}},
            };
        }

        static nlohmann::json MakeTextToolResult(const nlohmann::json &structuredContent, bool isError = false)
        {
            return nlohmann::json{
                {"content", nlohmann::json::array({
                                {
                                    {"type", "text"},
                                    {"text", DumpJson(structuredContent)},
                                },
                            })},
                {"structuredContent", structuredContent},
                {"isError", isError},
            };
        }

        static EditorToolCatalogContext BuildCatalogContext(GUI *gui, EditorToolRuntime *runtime)
        {
            EditorToolCatalogContext context;
            context.projectRoot = GetEditorRepoRootFromAssets().string();
            context.gui = gui;
            context.runtime = runtime;

            if (gui)
                context.codebaseBM25 = gui->GetCodebaseBM25Shared();

            return context;
        }
    } // namespace

    EditorToolServer::EditorToolServer(EditorToolRuntime *runtime, GUI *gui)
        : m_runtime(runtime), m_gui(gui), m_server(std::make_unique<httplib::Server>())
    {
    }

    EditorToolServer::~EditorToolServer()
    {
        Stop();
    }

    void EditorToolServer::ConfigureRoutes()
    {
        if (!m_server)
            return;

        // OAuth 2.0 Authorization Server Metadata (RFC 8414)
        // Claude Code discovers this endpoint on every session to check if auth is required.
        // Returning a valid token_endpoint lets CC obtain a persistent long-lived token
        // so it stops prompting "Needs Auth" on every launch.
        m_server->Get("/.well-known/oauth-authorization-server", [this](const httplib::Request &, httplib::Response &res)
                      {
            const std::string base = "http://127.0.0.1:" + std::to_string(m_port);
            res.set_content(DumpJson(nlohmann::json{
                {"issuer", base},
                {"authorization_endpoint", base + "/oauth/authorize"},
                {"token_endpoint", base + "/oauth/token"},
                {"registration_endpoint", base + "/oauth/register"},
                {"token_endpoint_auth_methods_supported", nlohmann::json::array({"none"})},
                {"grant_types_supported", nlohmann::json::array({"authorization_code", "client_credentials"})},
                {"response_types_supported", nlohmann::json::array({"code", "token"})},
                {"code_challenge_methods_supported", nlohmann::json::array({"S256", "plain"})},
            }), "application/json"); });

        // Authorization endpoint — immediately redirects with a static code (no user interaction needed locally).
        m_server->Get("/oauth/authorize", [](const httplib::Request &req, httplib::Response &res)
                      {
            const std::string redirectUri = req.get_param_value("redirect_uri");
            const std::string state       = req.get_param_value("state");
            if (redirectUri.empty())
            {
                res.status = 400;
                res.set_content(DumpJson(nlohmann::json{{"error", "missing redirect_uri"}}), "application/json");
                return;
            }
            std::string location = redirectUri + (redirectUri.find('?') == std::string::npos ? "?" : "&");
            location += "code=phasmaeditor-local-auth-code";
            if (!state.empty())
                location += "&state=" + state;
            res.status = 302;
            res.set_header("Location", location); });

        // Dynamic client registration (RFC 7591) — required by Claude Code.
        // Returns a static client_id; no secret needed for public clients.
        m_server->Post("/oauth/register", [](const httplib::Request &req, httplib::Response &res)
                       {
            nlohmann::json regBody;
            try { regBody = nlohmann::json::parse(req.body.empty() ? "{}" : req.body); }
            catch (...) { regBody = nlohmann::json::object(); }

            // Echo back redirect_uris if provided, otherwise use a localhost fallback
            nlohmann::json redirectUris = regBody.contains("redirect_uris") && regBody["redirect_uris"].is_array()
                                              ? regBody["redirect_uris"]
                                              : nlohmann::json::array({"http://127.0.0.1/callback"});

            res.status = 201;
            res.set_content(DumpJson(nlohmann::json{
                {"client_id", "phasmaeditor-local-client"},
                {"client_secret", ""},
                {"redirect_uris", redirectUris},
                {"token_endpoint_auth_method", "none"},
                {"grant_types", nlohmann::json::array({"client_credentials"})},
            }), "application/json"); });

        // Returns a static bearer token valid for ~10 years.
        // Claude Code stores this and will not re-authenticate until it expires.
        m_server->Post("/oauth/token", [](const httplib::Request &, httplib::Response &res)
                       { res.set_content(DumpJson(nlohmann::json{
                                             {"access_token", "phasmaeditor-local-static-token"},
                                             {"token_type", "Bearer"},
                                             {"expires_in", 315360000},
                                         }),
                                         "application/json"); });

        m_server->Get("/mcp", [](const httplib::Request &, httplib::Response &res)
                      {
            res.status = 405;
            res.set_header("Allow", "POST");
            res.set_content(DumpJson(nlohmann::json{{"error", "Method Not Allowed — use POST /mcp"}}), "application/json"); });

        m_server->Post("/mcp", [this](const httplib::Request &req, httplib::Response &res)
                       {

            const std::string userAgent = req.get_header_value("User-Agent");
            const std::string clientId = userAgent.empty() ? "unknown" : userAgent;

            nlohmann::json body;
            try
            {
                body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            }
            catch (...)
            {
                // JSON-RPC 2.0: parse errors return HTTP 200 with an error object
                res.set_content(DumpJson(MakeJsonRpcError(nullptr, -32700, "Parse error")), "application/json");
                return;
            }

            const auto handleMessage = [this, &clientId](const nlohmann::json &message) -> std::optional<nlohmann::json>
            {
                if (!message.is_object())
                    return MakeJsonRpcError(nullptr, -32600, "Invalid Request");

                const bool hasId = message.contains("id");
                const nlohmann::json id = hasId ? message["id"] : nlohmann::json(nullptr);
                const std::string method = message.value("method", "");

                if (method.empty())
                    return hasId ? std::optional<nlohmann::json>(MakeJsonRpcError(id, -32600, "Invalid Request")) : std::nullopt;

                if (method == "notifications/initialized")
                    return std::nullopt;

                if (method == "initialize")
                {
                    // Read the client's requested version; respond with ours regardless
                    const auto &initParams = message.contains("params") && message["params"].is_object()
                                                 ? message["params"]
                                                 : nlohmann::json::object();
                    (void)initParams.value("protocolVersion", kMcpVersion); // noted, only one version supported

                    const std::string clientName = initParams.contains("clientInfo") && initParams["clientInfo"].is_object()
                                                       ? initParams["clientInfo"].value("name", "unknown")
                                                       : "unknown";
                    PE_INFO("[MCP] %s connected (%s)", clientName.c_str(), clientId.c_str());

                    return MakeJsonRpcResult(id, nlohmann::json{
                        {"protocolVersion", kMcpVersion},
                        {"capabilities", {
                            {"tools", nlohmann::json::object()},
                        }},
                        {"serverInfo", {
                            {"name", "phasmaeditor-tools"},
                            {"title", "PhasmaEditor Tools"},
                            {"version", "0.1.0"},
                        }},
                        {"instructions", "Use tools/list and tools/call to inspect and interact with the running PhasmaEditor instance."},
                    });
                }

                if (method == "ping")
                    return MakeJsonRpcResult(id, nlohmann::json::object());

                if (method == "tools/list")
                {
                    const auto tools = BuildEditorToolDefinitions(BuildCatalogContext(m_gui, m_runtime));
                    return MakeJsonRpcResult(id, nlohmann::json{{"tools", pagent::BuildMcpToolList(tools)}});
                }

                if (method == "tools/call")
                {
                    const auto &params = message.contains("params") && message["params"].is_object()
                                             ? message["params"]
                                             : nlohmann::json::object();
                    const std::string toolName = params.value("name", "");
                    const nlohmann::json args = params.contains("arguments") && params["arguments"].is_object()
                                                    ? params["arguments"]
                                                    : nlohmann::json::object();

                    PE_INFO("[MCP] [%s] → %s", clientId.c_str(), toolName.c_str());

                    const auto tools = BuildEditorToolDefinitions(BuildCatalogContext(m_gui, m_runtime));
                    const auto *tool = pagent::FindToolDefinition(tools, toolName);
                    if (!tool)
                        return MakeJsonRpcError(id, -32602, "Unknown tool: " + toolName);

                    const std::string raw = tool->handler(DumpJson(args));
                    nlohmann::json structured;
                    try
                    {
                        structured = nlohmann::json::parse(raw.empty() ? "{}" : raw);
                    }
                    catch (...)
                    {
                        structured = nlohmann::json{{"raw", raw}};
                    }

                    const bool isError = structured.contains("error");
                    if (isError)
                        PE_WARN("[MCP] [%s] ← %s: error", clientId.c_str(), toolName.c_str());
                    else
                        PE_INFO("[MCP] [%s] ← %s: ok", clientId.c_str(), toolName.c_str());
                    return MakeJsonRpcResult(id, MakeTextToolResult(structured, isError));
                }

                return hasId ? std::optional<nlohmann::json>(MakeJsonRpcError(id, -32601, "Method not found")) : std::nullopt;
            };

            if (body.is_array())
            {
                nlohmann::json responses = nlohmann::json::array();
                for (const auto &message : body)
                {
                    auto response = handleMessage(message);
                    if (response.has_value())
                        responses.push_back(*response);
                }

                if (responses.empty())
                {
                    res.status = 202;
                    return;
                }

                res.set_content(DumpJson(responses), "application/json");
                return;
            }

            auto response = handleMessage(body);
            if (!response.has_value())
            {
                res.status = 202;
                return;
            }

            res.set_content(DumpJson(*response), "application/json"); });

        m_server->Get("/health", [this](const httplib::Request &, httplib::Response &res)
                      { res.set_content(DumpJson(nlohmann::json{
                                            {"ok", true},
                                            {"service", "PhasmaEditor Tool Server"},
                                            {"port", m_port},
                                        }),
                                        "application/json"); });

        m_server->Get("/tools", [this](const httplib::Request &, httplib::Response &res)
                      {
            const auto tools = BuildEditorToolDefinitions(BuildCatalogContext(m_gui, m_runtime));
            res.set_content(DumpJson(nlohmann::json{{"tools", pagent::BuildMcpToolList(tools)}}), "application/json"); });

        m_server->Post("/tool", [this](const httplib::Request &req, httplib::Response &res)
                       {
            const auto origin = req.get_header_value("Origin");
            if (!IsLocalOrigin(origin))
            {
                res.status = 403;
                res.set_content(DumpJson(nlohmann::json{{"error", "forbidden origin"}}), "application/json");
                return;
            }

            nlohmann::json body;
            try
            {
                body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            }
            catch (...)
            {
                res.status = 400;
                res.set_content(DumpJson(nlohmann::json{{"error", "invalid json body"}}), "application/json");
                return;
            }

            const std::string toolName = body.value("name", "");
            const nlohmann::json args = body.contains("arguments") ? body["arguments"] : nlohmann::json::object();

            const auto tools = BuildEditorToolDefinitions(BuildCatalogContext(m_gui, m_runtime));
            const auto *tool = pagent::FindToolDefinition(tools, toolName);
            const bool ok = (tool != nullptr);
            const std::string result = tool ? tool->handler(DumpJson(args))
                                            : DumpJson(nlohmann::json{{"error", "unknown tool"}, {"tool", toolName}});
            if (!ok)
            {
                res.status = 404;
                res.set_content(result, "application/json");
                return;
            }

            res.set_content(result.empty() ? "{}" : result, "application/json"); });
    }

    void EditorToolServer::Start()
    {
        if (m_running.exchange(true))
            return;

        ConfigureRoutes();
        m_thread = std::thread([this]()
                               {
            if (!m_server->listen("127.0.0.1", m_port))
            {
                PE_WARN("[Agent] EditorToolServer failed to bind to 127.0.0.1:%d", m_port);
                m_running = false;
            } });

        m_server->wait_until_ready();
        if (m_running)
        {
            PE_INFO("[Agent] EditorToolServer listening on http://127.0.0.1:%d", m_port);
            PE_INFO("[MCP] MCP server is now ready");
        }
    }

    void EditorToolServer::Stop()
    {
        if (!m_running.exchange(false))
            return;

        PE_INFO("[MCP] MCP server disabled");
        if (m_server)
            m_server->stop();
        if (m_thread.joinable())
            m_thread.join();
    }
} // namespace pe
