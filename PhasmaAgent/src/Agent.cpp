#include "PhasmaAgent/Agent.h"
#include "AgentImpl.h"

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>

namespace pagent
{
    std::vector<ProviderInfo> DiscoverProviders()
    {
        std::vector<ProviderInfo> providers;

        const char *anthropicKey = std::getenv("PAGENT_ANTHROPIC_API_KEY");
        const char *openaiKey = std::getenv("PAGENT_OPENAI_API_KEY");
        const char *geminiKey = std::getenv("PAGENT_GEMINI_API_KEY");

        if (anthropicKey)
            providers.push_back({Provider::Anthropic, "Anthropic", anthropicKey, "claude-haiku-4-5"});
        if (openaiKey)
            providers.push_back({Provider::OpenAI, "OpenAI", openaiKey, "gpt-4.1-mini"});
        if (geminiKey)
            providers.push_back({Provider::Gemini, "Gemini", geminiKey, "gemini-2.5-flash"});
        providers.push_back({Provider::Ollama, "Ollama", "", "llama3.2"});

        return providers;
    }

    int GetDefaultProviderIndex(const std::vector<ProviderInfo> &providers)
    {
        const char *env = std::getenv("PAGENT_PROVIDER");
        if (!env)
            return 0;

        std::string providerStr = env;
        for (auto &c : providerStr)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        for (int i = 0; i < static_cast<int>(providers.size()); ++i)
        {
            const auto &p = providers[i];
            if ((providerStr == "anthropic" && p.provider == Provider::Anthropic) ||
                (providerStr == "openai" && p.provider == Provider::OpenAI) ||
                (providerStr == "gemini" && p.provider == Provider::Gemini) ||
                (providerStr == "ollama" && p.provider == Provider::Ollama))
                return i;
        }
        return 0;
    }

    Agent::Agent(AgentConfig config)
        : m_impl(std::make_unique<Impl>(std::move(config)))
    {
    }

    Agent::~Agent() = default;

    Agent::Agent(Agent &&) noexcept = default;
    Agent &Agent::operator=(Agent &&) noexcept = default;

    void Agent::RegisterTool(ToolDefinition tool)
    {
        m_impl->RegisterTool(std::move(tool));
    }

    void Agent::UnregisterTool(const std::string &name)
    {
        m_impl->UnregisterTool(name);
    }

    void Agent::ClearTools()
    {
        m_impl->ClearTools();
    }

    bool Agent::Send(const std::string &user_message)
    {
        return m_impl->Send(user_message);
    }

    void Agent::Poll()
    {
        m_impl->Poll();
    }

    void Agent::SetEventCallback(EventCallback callback)
    {
        m_impl->SetEventCallback(std::move(callback));
    }

    bool Agent::IsBusy() const
    {
        return m_impl->IsBusy();
    }

    void Agent::CancelPending()
    {
        m_impl->CancelPending();
    }

    void Agent::ClearHistory()
    {
        m_impl->ClearHistory();
    }

    std::vector<HistoryEntry> Agent::GetHistory() const
    {
        return m_impl->GetHistory();
    }

    void Agent::InjectSystemMessage(const std::string &content)
    {
        m_impl->InjectSystemMessage(content);
    }

    void Agent::SetModel(const std::string &model)
    {
        m_impl->SetModel(model);
    }

    TokenUsage Agent::GetUsage() const
    {
        return m_impl->GetUsage();
    }

    Provider Agent::GetProvider() const
    {
        return m_impl->GetProvider();
    }

    std::vector<Agent::ModelInfo> Agent::FetchModelInfos(Provider provider,
                                                         const std::string &api_key,
                                                         const std::string &base_url)
    {
        using json = nlohmann::json;

        // Anthropic has no model listing endpoint
        if (provider == Provider::Anthropic)
            return {{"claude-sonnet-4-6", true}, {"claude-opus-4-6", true}, {"claude-haiku-4-5", true}};

        // Helper: fetch JSON from an HTTP(S) endpoint
        auto httpGet = [](const std::string &host, const std::string &path,
                          bool useHttps, const httplib::Headers &headers) -> std::string
        {
            if (!useHttps)
            {
                httplib::Client cli(host);
                cli.set_read_timeout(5);
                auto res = cli.Get(path, headers);
                if (res && res->status == 200)
                    return res->body;
            }
            else
            {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(host);
                cli.set_read_timeout(5);
                auto res = cli.Get(path, headers);
                if (res && res->status == 200)
                    return res->body;
#endif
            }
            return {};
        };

        // Helper: parse Ollama /api/tags JSON into model list
        auto parseOllamaModels = [](const std::string &body) -> std::vector<std::pair<std::string, int64_t>>
        {
            std::vector<std::pair<std::string, int64_t>> result;
            try
            {
                auto j = json::parse(body);
                if (j.contains("models") && j["models"].is_array())
                {
                    for (const auto &m : j["models"])
                    {
                        std::string name = m.value("name", "");
                        int64_t size = m.value("size", int64_t(0));
                        if (name.size() > 7 && name.substr(name.size() - 7) == ":latest")
                            name = name.substr(0, name.size() - 7);
                        if (!name.empty())
                            result.push_back({name, size});
                    }
                }
            }
            catch (...)
            {
            }
            return result;
        };

        // Ollama: merge local models + remote library
        if (provider == Provider::Ollama)
        {
            std::string ollamaHost = base_url.empty() ? "http://localhost:11434" : base_url;
            const std::string httpPrefix = "http://";
            if (ollamaHost.rfind(httpPrefix, 0) == 0)
                ollamaHost = ollamaHost.substr(httpPrefix.size());

            std::vector<ModelInfo> models;
            std::set<std::string> localNames;

            // Fetch locally downloaded models, filter out those without tool support
            std::string localBody = httpGet(ollamaHost, "/api/tags", false, {});
            for (auto &[name, size] : parseOllamaModels(localBody))
            {
                if (!SupportsTools(Provider::Ollama, name, base_url))
                    continue;
                models.push_back({name, true});
                localNames.insert(name);
            }

            // Fetch all available models from ollama.com/library
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            std::string libraryHtml = httpGet("ollama.com", "/library", true, {});
            {
                const std::string prefix = "href=\"/library/";
                size_t pos = 0;
                while ((pos = libraryHtml.find(prefix, pos)) != std::string::npos)
                {
                    pos += prefix.size();
                    auto end = libraryHtml.find('"', pos);
                    if (end == std::string::npos)
                        break;
                    std::string name = libraryHtml.substr(pos, end - pos);
                    if (!name.empty() && localNames.find(name) == localNames.end())
                    {
                        localNames.insert(name);
                        models.push_back({name, false});
                    }
                }
            }
#endif

            std::sort(models.begin(), models.end(), [](const ModelInfo &a, const ModelInfo &b)
                      {
                if (a.local != b.local) return a.local; // local first
                return a.name < b.name; });
            return models;
        }

        // Non-Ollama providers
        std::string host;
        std::string path;
        httplib::Headers headers;

        switch (provider)
        {
        case Provider::OpenAI:
            host = "api.openai.com";
            path = "/v1/models";
            headers = {{"Authorization", "Bearer " + api_key}};
            break;
        case Provider::Gemini:
            host = "generativelanguage.googleapis.com";
            path = "/v1beta/models?key=" + api_key;
            break;
        default:
            return {};
        }

        if (!api_key.empty() && headers.empty() && provider != Provider::Gemini)
            headers = {{"Authorization", "Bearer " + api_key}};

        std::string responseBody = httpGet(host, path, true, headers);
        if (responseBody.empty())
            return {};

        std::vector<ModelInfo> models;
        try
        {
            auto j = json::parse(responseBody);
            if (j.contains("data") && j["data"].is_array())
            {
                for (const auto &m : j["data"])
                {
                    std::string id = m.value("id", "");
                    if (id.empty())
                        continue;

                    // Filter non-chat models for OpenAI/Gemini
                    if (provider == Provider::OpenAI)
                    {
                        // Only include chat-capable models (gpt-*, o1-*, o3-*, o4-*, chatgpt-*)
                        bool isChat = id.rfind("gpt-", 0) == 0 ||
                                      id.rfind("o1-", 0) == 0 ||
                                      id.rfind("o3-", 0) == 0 ||
                                      id.rfind("o4-", 0) == 0 ||
                                      id.rfind("chatgpt-", 0) == 0;
                        if (!isChat)
                            continue;
                    }
                    else if (provider == Provider::Gemini)
                    {
                        // Gemini returns "models/gemini-..." — strip prefix, skip non-generative
                        const std::string geminiPrefix = "models/";
                        if (id.rfind(geminiPrefix, 0) == 0)
                            id = id.substr(geminiPrefix.size());
                        if (id.find("embedding") != std::string::npos ||
                            id.find("aqa") != std::string::npos ||
                            id.find("imagen") != std::string::npos)
                            continue;
                    }

                    models.push_back({id, true});
                }
            }
            std::sort(models.begin(), models.end(), [](const ModelInfo &a, const ModelInfo &b)
                      { return a.name < b.name; });
        }
        catch (...)
        {
        }

        return models;
    }

    std::vector<std::string> Agent::FetchModels(Provider provider,
                                                const std::string &api_key,
                                                const std::string &base_url)
    {
        auto infos = FetchModelInfos(provider, api_key, base_url);
        std::vector<std::string> names;
        names.reserve(infos.size());
        for (auto &info : infos)
            names.push_back(std::move(info.name));
        return names;
    }
    Agent::CancelToken Agent::PullModel(const std::string &model,
                                        ProgressCallback progressCb,
                                        CompleteCallback completeCb)
    {
        return OllamaProcess::Pull(model, std::move(progressCb), std::move(completeCb));
    }

    void Agent::CancelPull(const CancelToken &token)
    {
        OllamaProcess::CancelPull(token);
    }

    bool Agent::SupportsTools(Provider provider, const std::string &model,
                              const std::string &base_url)
    {
        if (provider != Provider::Ollama)
            return true;

        using json = nlohmann::json;
        std::string ollamaHost = base_url.empty() ? "http://localhost:11434" : base_url;
        const std::string httpPrefix = "http://";
        if (ollamaHost.rfind(httpPrefix, 0) == 0)
            ollamaHost = ollamaHost.substr(httpPrefix.size());

        httplib::Client cli(ollamaHost);
        cli.set_read_timeout(3);
        std::string body = "{\"name\":\"" + model + "\"}";
        auto res = cli.Post("/api/show", body, "application/json");
        if (!res || res->status != 200)
            return true; // assume yes if can't check

        try
        {
            auto j = json::parse(res->body);
            if (j.contains("capabilities") && j["capabilities"].is_array())
            {
                for (const auto &cap : j["capabilities"])
                    if (cap.get<std::string>() == "tools")
                        return true;
                return false;
            }
        }
        catch (...)
        {
        }
        return true;
    }

    void Agent::UnloadModel(Provider provider, const std::string &model,
                            const std::string &base_url)
    {
        if (provider == Provider::Ollama)
            OllamaProcess::UnloadModel(model, base_url);
    }

    // Load pricing JSON once (thread-safe via static init).
    // Falls back to hardcoded defaults if file not found.
    static const nlohmann::ordered_json &GetPricingData()
    {
        static const nlohmann::ordered_json data = []()
        {
            // Try paths relative to common locations
            for (const char *path : {"pricing.json",
                                     "PhasmaAgent/pricing.json",
                                     "../PhasmaAgent/pricing.json",
                                     "../../PhasmaAgent/pricing.json",
                                     "../../../PhasmaAgent/pricing.json"})
            {
                std::ifstream f(path);
                if (f.is_open())
                {
                    try
                    {
                        return nlohmann::ordered_json::parse(f);
                    }
                    catch (...)
                    {
                    }
                }
            }
            return nlohmann::ordered_json{};
        }();
        return data;
    }

    // Look up pricing for a model from the JSON data.
    // Checks each key in the provider object — first substring match wins.
    // Falls back to "default" entry if present, otherwise zeros.
    struct PricingEntry
    {
        float input = 0.0f;
        float output = 0.0f;
        float cacheRead = 0.0f;
        float cacheWrite = 0.0f;
    };

    static PricingEntry LookupPricing(const nlohmann::ordered_json &data,
                                      const std::string &providerKey,
                                      const std::string &model)
    {
        PricingEntry p;
        if (!data.contains(providerKey) || !data[providerKey].is_object())
            return p;

        const auto &prov = data[providerKey];
        for (auto &[key, val] : prov.items())
        {
            if (model.find(key) != std::string::npos)
            {
                p.input = val.value("input", 0.0f);
                p.output = val.value("output", 0.0f);
                p.cacheRead = val.value("cache_read", 0.0f);
                p.cacheWrite = val.value("cache_write", 0.0f);
                return p;
            }
        }
        return p;
    }

    float EstimateCostUSD(Provider provider, const std::string &model,
                          int inputTokens, int outputTokens,
                          int cacheReadTokens, int cacheWriteTokens)
    {
        if (provider == Provider::Ollama)
            return 0.0f;

        const char *providerKey = nullptr;
        switch (provider)
        {
        case Provider::Anthropic:
            providerKey = "anthropic";
            break;
        case Provider::OpenAI:
            providerKey = "openai";
            break;
        case Provider::Gemini:
            providerKey = "gemini";
            break;
        default:
            return 0.0f;
        }

        auto p = LookupPricing(GetPricingData(), providerKey, model);

        return (inputTokens * p.input + outputTokens * p.output +
                cacheReadTokens * p.cacheRead + cacheWriteTokens * p.cacheWrite) /
               1000000.0f;
    }
} // namespace pagent
