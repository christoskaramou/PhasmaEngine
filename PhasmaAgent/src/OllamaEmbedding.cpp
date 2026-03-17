#include "PhasmaAgent/OllamaEmbedding.h"
#include <nlohmann/json.hpp>
#include <httplib/httplib.h>

namespace pagent
{
    using json = nlohmann::json;

    OllamaEmbedding::OllamaEmbedding(std::string model, int dims, std::string base_url)
        : m_model(std::move(model)), m_dims(dims), m_baseUrl(std::move(base_url))
    {
    }

    std::vector<float> OllamaEmbedding::Embed(const std::string &text)
    {
        if (text.empty())
            return {};

        json body;
        body["model"] = m_model;
        body["input"] = text;

        std::string host = m_baseUrl;
        const std::string httpPrefix = "http://";
        if (host.rfind(httpPrefix, 0) == 0)
            host = host.substr(httpPrefix.size());

        httplib::Client cli(host);
        cli.set_read_timeout(30);
        auto res = cli.Post("/api/embed", body.dump(), "application/json");
        if (!res || res->status != 200)
            return {};

        try
        {
            auto resp = json::parse(res->body);
            if (resp.contains("embeddings") && resp["embeddings"].is_array() && !resp["embeddings"].empty())
            {
                auto vec = resp["embeddings"][0].get<std::vector<float>>();
                m_dims = static_cast<int>(vec.size());
                return vec;
            }
        }
        catch (...)
        {
        }

        return {};
    }
} // namespace pagent
