#include "PhasmaAgent/OpenAIEmbedding.h"
#include <nlohmann/json.hpp>

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#endif
#include <httplib/httplib.h>

namespace pagent
{
    using json = nlohmann::json;

    OpenAIEmbedding::OpenAIEmbedding(std::string api_key, std::string model, int dims)
        : m_apiKey(std::move(api_key)), m_model(std::move(model)), m_dims(dims)
    {
    }

    std::vector<float> OpenAIEmbedding::Embed(const std::string &text)
    {
        if (text.empty() || m_apiKey.empty())
            return {};

        json body;
        body["model"] = m_model;
        body["input"] = text;
        if (m_model.find("text-embedding-3") != std::string::npos)
            body["dimensions"] = m_dims;

        const std::string bodyStr = body.dump();
        std::string responseBody;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::SSLClient cli("api.openai.com");
        cli.set_read_timeout(15);
        httplib::Headers headers = {{"Authorization", "Bearer " + m_apiKey}};
        auto res = cli.Post("/v1/embeddings", headers, bodyStr, "application/json");
        if (!res || res->status != 200)
            return {};
        responseBody = res->body;
#else
        return {};
#endif

        try
        {
            auto resp = json::parse(responseBody);
            if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty())
            {
                return resp["data"][0]["embedding"].get<std::vector<float>>();
            }
        }
        catch (...)
        {
        }

        return {};
    }
} // namespace pagent
