#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace phasma::driver
{
    using Json = nlohmann::json;

    struct Endpoint
    {
        std::wstring host;
        std::wstring path;
        unsigned short port = 80;
        bool secure = false;
    };

    struct Selection
    {
        std::string action;
        std::string model;
        double confidence = 1.0;
        std::uint64_t inputTokens = 0;
        std::uint64_t outputTokens = 0;
    };

    enum class ActOutcome
    {
        Observation,
        Redecide,
        Reject
    };

    Endpoint ParseLoopbackUrl(std::string_view url);
    Json ParseJson(std::string_view text);
    Json DecodeRpc(std::string_view body, std::string_view contentType, std::uint64_t requestId);
    Json UnwrapToolResult(const Json &result);
    void ValidateDescription(const Json &description);
    void ValidateObservation(const Json &observation);
    bool IsRealtime(const Json &description);
    ActOutcome ClassifyActResult(const Json &result, bool realtime);
    Json MakeJevRequest(const Json &description, const Json &observation);
    Selection ParseJevResponse(const Json &response, const Json &observation);
    Selection ChooseLocal(const Json &observation);
    std::optional<std::string> CleanKey(std::string_view text);
} // namespace phasma::driver
