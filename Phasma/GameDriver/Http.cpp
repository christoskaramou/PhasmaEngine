#include "Http.h"

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace phasma::driver
{
    namespace
    {
        struct InternetCloser
        {
            void operator()(void *handle) const { WinHttpCloseHandle(handle); }
        };

        using InternetHandle = std::unique_ptr<void, InternetCloser>;

        InternetHandle Checked(HINTERNET handle)
        {
            if (!handle)
            {
                throw std::runtime_error("WinHTTP could not create a request handle");
            }
            return InternetHandle(handle);
        }

        void Require(BOOL succeeded)
        {
            if (!succeeded)
            {
                throw std::runtime_error("WinHTTP request failed (error " + std::to_string(GetLastError()) + ")");
            }
        }

        void CheckCancelled(const std::atomic_bool &cancelled)
        {
            if (cancelled.load())
            {
                throw std::runtime_error("Cancelled");
            }
        }

        void SetRemainingTimeout(HINTERNET request, std::chrono::steady_clock::time_point deadline)
        {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                    .count();

            if (remaining <= 0)
            {
                throw std::runtime_error("HTTP request exceeded its 15-second deadline");
            }

            DWORD timeout = static_cast<DWORD>(remaining);
            Require(WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout)));
        }

        std::string Header(HINTERNET request, const wchar_t *name)
        {
            std::array<wchar_t, 1024> buffer{};
            DWORD size = static_cast<DWORD>(sizeof(buffer));

            if (!WinHttpQueryHeaders(
                    request, WINHTTP_QUERY_CUSTOM, name, buffer.data(), &size, WINHTTP_NO_HEADER_INDEX))
            {
                if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
                {
                    return {};
                }
                throw std::runtime_error("Invalid or oversized HTTP response header");
            }

            std::string value;
            for (const auto character : buffer)
            {
                if (character == L'\0')
                {
                    break;
                }
                if (character < 32 || character > 126)
                {
                    throw std::runtime_error("HTTP response header must contain plain ASCII");
                }
                value.push_back(static_cast<char>(character));
            }
            return value;
        }

        std::string ReadBody(HINTERNET request,
                             std::size_t limit,
                             const std::atomic_bool &cancelled,
                             std::chrono::steady_clock::time_point deadline)
        {
            std::string body;
            std::array<char, 8192> buffer{};

            while (true)
            {
                CheckCancelled(cancelled);
                SetRemainingTimeout(request, deadline);

                DWORD bytes = 0;
                Require(WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes));

                if (bytes == 0)
                {
                    return body;
                }

                if (body.size() + bytes > limit)
                {
                    throw std::runtime_error("HTTP response exceeds its size limit");
                }

                body.append(buffer.data(), bytes);
            }
        }
    } // namespace

    struct HttpClient::Handles
    {
        Endpoint endpoint;
        InternetHandle session;
        InternetHandle connection;
    };

    HttpClient::HttpClient(Endpoint endpoint) : handles(std::make_unique<Handles>())
    {
        handles->endpoint = std::move(endpoint);

        handles->session = Checked(WinHttpOpen(
            L"PhasmaGameDriver/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));

        Require(WinHttpSetTimeouts(handles->session.get(), 2000, 3000, 5000, 15000));
        handles->connection =
            Checked(WinHttpConnect(handles->session.get(), handles->endpoint.host.c_str(), handles->endpoint.port, 0));
    }

    HttpClient::~HttpClient() = default;

    HttpResponse HttpClient::Post(std::string_view body,
                                  std::wstring_view headers,
                                  std::size_t limit,
                                  const std::atomic_bool &cancelled)
    {
        CheckCancelled(cancelled);

        if (body.size() > 2 * 1024 * 1024)
        {
            throw std::runtime_error("HTTP request exceeds 2 MiB");
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        const auto &endpoint = handles->endpoint;
        const DWORD flags = endpoint.secure ? WINHTTP_FLAG_SECURE : 0;

        auto request = Checked(WinHttpOpenRequest(handles->connection.get(),
                                                  L"POST",
                                                  endpoint.path.c_str(),
                                                  nullptr,
                                                  WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  flags));

        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        Require(WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));

        DWORD retries = 0;
        Require(WinHttpSetOption(request.get(), WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)));

        const std::wstring allHeaders = L"Content-Type: application/json\r\n" + std::wstring(headers);
        Require(WinHttpSendRequest(request.get(),
                                   allHeaders.c_str(),
                                   static_cast<DWORD>(allHeaders.size()),
                                   const_cast<char *>(body.data()),
                                   static_cast<DWORD>(body.size()),
                                   static_cast<DWORD>(body.size()),
                                   0));
        CheckCancelled(cancelled);
        SetRemainingTimeout(request.get(), deadline);
        Require(WinHttpReceiveResponse(request.get(), nullptr));

        HttpResponse response;
        DWORD size = sizeof(response.status);
        Require(WinHttpQueryHeaders(request.get(),
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    &response.status,
                                    &size,
                                    WINHTTP_NO_HEADER_INDEX));
        response.contentType = Header(request.get(), L"Content-Type");
        response.session = Header(request.get(), L"Mcp-Session-Id");
        response.body = ReadBody(request.get(), limit, cancelled, deadline);

        return response;
    }
} // namespace phasma::driver
