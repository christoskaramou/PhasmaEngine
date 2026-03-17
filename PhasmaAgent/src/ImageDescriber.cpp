#include "ImageDescriber.h"
#include <nlohmann/json.hpp>

#include <httplib/httplib.h>

namespace pagent
{
    using json = nlohmann::json;

    std::string ImageDescriber::Describe(const std::string &base64_image,
                                         const std::string &mime_type,
                                         const std::string &gemini_api_key,
                                         const HttpHandler &custom_http)
    {
        if (base64_image.empty() || gemini_api_key.empty())
            return "[Image: no description available]";

        // Build Gemini generateContent request with the image
        json body;
        body["contents"] = json::array({{{"role", "user"},
                                         {"parts", json::array({{{"text", "Describe this image concisely in 2-3 sentences for an AI assistant context. Focus on what is shown."}},
                                                                {{"inlineData", {{"mimeType", mime_type}, {"data", base64_image}}}}})}}});
        body["generationConfig"] = {{"maxOutputTokens", 256}, {"temperature", 0.2f}};

        const std::string model = "gemini-2.5-flash";
        const std::string host = "generativelanguage.googleapis.com";
        const std::string path = "/v1beta/models/" + model + ":generateContent";
        const std::string bodyStr = body.dump();

        std::string responseBody;

        if (custom_http)
        {
            std::map<std::string, std::string> headers = {
                {"x-goog-api-key", gemini_api_key},
                {"Content-Type", "application/json"}};
            auto [status, resp] = custom_http("POST", "https://" + host + path, headers, bodyStr);
            if (status != 200)
                return "[Image: description failed]";
            responseBody = resp;
        }
        else
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(host);
            cli.set_read_timeout(30);
            httplib::Headers headers = {{"x-goog-api-key", gemini_api_key}};
            auto res = cli.Post(path, headers, bodyStr, "application/json");
            if (!res || res->status != 200)
                return "[Image: description failed]";
            responseBody = res->body;
#else
            return "[Image: HTTPS not available for description]";
#endif
        }

        // Parse response
        try
        {
            auto resp = json::parse(responseBody);
            const auto &candidates = resp.value("candidates", json::array());
            if (!candidates.empty())
            {
                const auto &parts = candidates[0].value("content", json::object()).value("parts", json::array());
                for (const auto &part : parts)
                {
                    if (part.contains("text"))
                        return part["text"].get<std::string>();
                }
            }
        }
        catch (...)
        {
        }

        return "[Image: could not parse description]";
    }
} // namespace pagent
