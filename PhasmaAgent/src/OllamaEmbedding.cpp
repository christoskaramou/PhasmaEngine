#include "PhasmaAgent/OllamaEmbedding.h"
#include <nlohmann/json.hpp>
#include <httplib/httplib.h>

namespace pagent
{
    using json = nlohmann::json;

    OllamaEmbedding::OllamaEmbedding(std::string model, int dims, std::string base_url)
        : m_model(std::move(model)), m_dims(dims)
    {
        const std::string httpPrefix = "http://";
        if (base_url.rfind(httpPrefix, 0) == 0)
            m_host = base_url.substr(httpPrefix.size());
        else
            m_host = std::move(base_url);
    }

    std::vector<float> OllamaEmbedding::Embed(const std::string &text)
    {
        if (IsCancelled() || text.empty())
            return {};

        auto results = EmbedBatch({text});
        return results.empty() ? std::vector<float>{} : std::move(results[0]);
    }

    std::vector<std::vector<float>> OllamaEmbedding::EmbedBatch(const std::vector<std::string> &texts)
    {
        if (IsCancelled() || texts.empty())
            return {};

        json body;
        body["model"] = m_model;
        if (texts.size() == 1)
            body["input"] = texts[0];
        else
            body["input"] = texts;

        httplib::Client cli(m_host);
        cli.set_read_timeout(60);
        auto res = cli.Post("/api/embed", body.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
        if (!res || res->status != 200)
            return {};

        try
        {
            auto resp = json::parse(res->body);
            if (!resp.contains("embeddings") || !resp["embeddings"].is_array())
                return {};

            const auto &arr = resp["embeddings"];
            std::vector<std::vector<float>> results;
            results.reserve(arr.size());
            for (const auto &e : arr)
            {
                auto vec = e.get<std::vector<float>>();
                if (!vec.empty())
                    m_dims = static_cast<int>(vec.size());
                results.push_back(std::move(vec));
            }
            return results;
        }
        catch (...)
        {
        }

        return {};
    }
} // namespace pagent
