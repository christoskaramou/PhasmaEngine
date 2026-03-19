#include "PhasmaAgent/GoogleEmbedding.h"
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

    GoogleEmbedding::GoogleEmbedding(std::string api_key, std::string model, int dims)
        : m_apiKey(std::move(api_key)), m_model(std::move(model)), m_dims(dims)
    {
    }

    std::vector<float> GoogleEmbedding::Embed(const std::string &text)
    {
        if (IsCancelled() || text.empty() || m_apiKey.empty() || m_failed.load())
            return {};

        json body;
        body["model"] = "models/" + m_model;
        body["content"] = {{"parts", json::array({{{"text", text}}})}};
        body["output_dimensionality"] = m_dims;

        const std::string host = "generativelanguage.googleapis.com";
        const std::string path = "/v1beta/models/" + m_model + ":embedContent";
        const std::string bodyStr = body.dump(-1, ' ', false, json::error_handler_t::replace);

        std::string responseBody;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::SSLClient cli(host);
        cli.set_read_timeout(10);
        httplib::Headers headers = {{"x-goog-api-key", m_apiKey}};
        auto res = cli.Post(path, headers, bodyStr, "application/json");
        if (!res)
        {
            fprintf(stderr, "[GoogleEmbedding] HTTP request failed (no response)\n");
            return {};
        }
        if (res->status != 200)
        {
            // Auth errors (400/401/403) are permanent — log once and stop retrying
            if (res->status == 400 || res->status == 401 || res->status == 403)
            {
                if (!m_failed.exchange(true))
                    fprintf(stderr, "[GoogleEmbedding] HTTP %d (auth error, disabling): %s\n", res->status, res->body.substr(0, 300).c_str());
            }
            else
            {
                fprintf(stderr, "[GoogleEmbedding] HTTP %d: %s\n", res->status, res->body.substr(0, 300).c_str());
            }
            return {};
        }
        responseBody = res->body;
#else
        return {};
#endif

        try
        {
            auto resp = json::parse(responseBody);
            if (resp.contains("embedding") && resp["embedding"].contains("values"))
            {
                return resp["embedding"]["values"].get<std::vector<float>>();
            }
        }
        catch (...)
        {
        }

        return {};
    }
} // namespace pagent
