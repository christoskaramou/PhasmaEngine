#pragma once

#include "Protocol.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace phasma::driver
{
    struct HttpResponse
    {
        unsigned long status = 0;
        std::string body;
        std::string contentType;
        std::string session;
    };

    class HttpClient
    {
    public:
        explicit HttpClient(Endpoint endpoint);
        ~HttpClient();
        HttpClient(const HttpClient &) = delete;
        HttpClient &operator=(const HttpClient &) = delete;

        HttpResponse Post(std::string_view body,
                          std::wstring_view headers,
                          std::size_t limit,
                          const std::atomic_bool &cancelled);

    private:
        struct Handles;
        std::unique_ptr<Handles> handles;
    };
} // namespace phasma::driver
