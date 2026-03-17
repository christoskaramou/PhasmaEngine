#pragma once

#include <string>
#include <functional>
#include <map>

namespace pagent
{
    class ImageDescriber
    {
    public:
        using HttpHandler = std::function<
            std::pair<int, std::string>(
                const std::string &method,
                const std::string &url,
                const std::map<std::string, std::string> &headers,
                const std::string &body)>;

        static std::string Describe(const std::string &base64_image,
                                    const std::string &mime_type,
                                    const std::string &gemini_api_key,
                                    const HttpHandler &custom_http = {});
    };
} // namespace pagent
