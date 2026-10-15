#include "Protocol.h"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace phasma::driver;

namespace
{
    void Check(bool condition, const char *name)
    {
        if (!condition)
        {
            throw std::runtime_error(name);
        }
    }

    void Reject(const std::function<void()> &test, const char *name)
    {
        try
        {
            test();
        }
        catch (const std::exception &)
        {
            return;
        }
        throw std::runtime_error(name);
    }

    Json Observation()
    {
        return {{"protocol_version", 1},
                {"game_id", "fixture"},
                {"session_id", "fixture"},
                {"state_id", 2},
                {"mode", "agent"},
                {"terminal", false},
                {"busy", false},
                {"score", 10},
                {"observation", {{"board", Json::array()}}},
                {"actions",
                 Json::array({{{"id", "left"}, {"description", "Move left"}, {"score", -5}},
                              {{"id", "drop"}, {"description", "Drop piece"}, {"score", 20}}})}};
    }

    void UrlTests()
    {
        Check(ParseLoopbackUrl("http://localhost:8876/mcp").host == L"127.0.0.1", "localhost must pin to loopback");
        Check(ParseLoopbackUrl("http://[::1]:1234/mcp").port == 1234, "IPv6 loopback");
        Check(ParseLoopbackUrl("http://127.0.0.1").path == L"/mcp", "default MCP path");

        for (const auto *url : {"https://127.0.0.1/mcp",
                                "http://example.com/mcp",
                                "http://127.0.0.1.evil/mcp",
                                "http://127.0.0.1:0/mcp",
                                "http://127.0.0.1:65536/mcp",
                                "http://127.0.0.1:/mcp",
                                "http://user@localhost/mcp",
                                "http://localhost/mcp?x=1",
                                "http://localhost/mcp#x",
                                "http://[::1]evil/mcp",
                                "http://localhost\\evil/mcp",
                                "http://localhost/\r\nheader"})
        {
            Reject([&]()
                   { ParseLoopbackUrl(url); }, "unsafe URL accepted");
        }
    }

    void RpcTests()
    {
        const Json response = {{"jsonrpc", "2.0"}, {"id", 7}, {"result", {{"ok", true}}}};
        Check(DecodeRpc(response.dump(), "application/json", 7)["ok"] == true, "JSON-RPC result");

        Reject([&]()
               { DecodeRpc(response.dump(), "application/json", 8); }, "wrong JSON-RPC id accepted");
        auto fractionalId = response;
        fractionalId["id"] = 7.0;
        Reject([&]()
               { DecodeRpc(fractionalId.dump(), "application/json", 7); }, "floating JSON-RPC id accepted");

        const auto stream = "event: message\r\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notice\"}\r\n\r\n"
                            "data: {\"jsonrpc\":\"2.0\",\"id\":7,\n"
                            "data: \"result\":{\"ok\":true}}\n\n";
        Check(DecodeRpc(stream, "text/event-stream; charset=utf-8", 7)["ok"] == true, "SSE multiline result");
        Reject([&]()
               { DecodeRpc(stream, "text/event-stream", 9); }, "SSE missing id accepted");

        const Json error = {{"jsonrpc", "2.0"}, {"id", 7}, {"error", {{"code", -1}}}};
        Reject([&]()
               { DecodeRpc(error.dump(), "application/json", 7); }, "RPC error accepted");

        const auto observation = Observation();
        const Json encoded = observation.dump();
        const Json content = {{"content", Json::array({{{"type", "text"}, {"text", encoded.dump()}}})}};
        Check(UnwrapToolResult(content) == observation, "double JSON text layer");
        Check(UnwrapToolResult({{"structuredContent", observation}}) == observation, "structured object");
        Check(UnwrapToolResult({{"structuredContent", encoded}}) == observation, "structured JSON string");
        Reject([]()
               { UnwrapToolResult({{"isError", true}}); }, "tool error accepted");
        Reject([]()
               { ParseJson(std::string(100, '[') + std::string(100, ']')); }, "deep JSON accepted");
    }

    void ObservationTests()
    {
        const Json description = {{"protocol_version", 1}, {"game_id", "fixture"}, {"description", "Play fixture"}};
        ValidateDescription(description);

        Reject([]()
               { ValidateDescription({{"protocol_version", 1.5}, {"game_id", "x"}, {"description", "x"}}); },
               "fractional game protocol accepted");
        Reject([]()
               { ValidateDescription({{"protocol_version", 2}}); }, "wrong game protocol accepted");

        auto state = Observation();
        ValidateObservation(state);
        Check(ChooseLocal(state).action == "drop" && ChooseLocal(state).model == "LOCAL", "adapter local score");

        const auto request = MakeJevRequest(description, state);
        Check(request["questions"]["action"]["criteria"].size() == 2, "one legal action question");
        Check(request["questions"]["action"]["criteria"]["left"] == "Move left", "action criteria description");
        Check(!request["state"]["current"].contains("actions"), "duplicated action descriptions");
        Check(request["state"]["current"]["action_scores"]["drop"] == 20, "adapter scores preserved");

        state["state_id"] = -1;
        Reject([&]()
               { ValidateObservation(state); }, "negative state id accepted");
        state = Observation();
        state["actions"][1]["id"] = "left";
        Reject([&]()
               { ValidateObservation(state); }, "duplicate action accepted");
        state = Observation();
        state["actions"][0].erase("score");
        state["actions"][1].erase("score");
        Reject([&]()
               { ChooseLocal(state); }, "missing local score accepted");
        state["actions"] = Json::object();
        Reject([&]()
               { ValidateObservation(state); }, "non-array actions accepted");
        state = Observation();
        state["score"] = std::numeric_limits<double>::infinity();
        Reject([&]()
               { ValidateObservation(state); }, "infinite game score accepted");
    }

    void JevTests()
    {
        const auto state = Observation();

        Json response = {{"model", "jev-fixture"},
                         {"usage", {{"input_tokens", 23}, {"output_tokens", 4}}},
                         {"answers",
                          {{"action",
                            {{"type", "choice"},
                             {"choice", "drop"},
                             {"confidence", 0.9},
                             {"probabilities", {{"left", 0.1}, {"drop", 0.9}}}}}}}};

        const auto result = ParseJevResponse(response, state);
        Check(result.action == "drop" && result.inputTokens == 23 && result.outputTokens == 4, "Jev selection usage");

        response["answers"]["action"]["choice"] = "invented";
        Reject([&]()
               { ParseJevResponse(response, state); }, "invented Jev action accepted");
        response["answers"]["action"]["choice"] = "drop";
        response["answers"]["action"]["confidence"] = 1.1;
        Reject([&]()
               { ParseJevResponse(response, state); }, "invalid confidence accepted");
        response["answers"]["action"]["confidence"] = 0.9;
        response["answers"]["action"]["probabilities"]["left"] = 0.9;
        Reject([&]()
               { ParseJevResponse(response, state); }, "invalid probability sum accepted");
        response["answers"]["action"]["probabilities"]["left"] = 0.1;
        response["usage"]["input_tokens"] = -1;
        Reject([&]()
               { ParseJevResponse(response, state); }, "negative Jev usage accepted");
    }

    void RealtimeTests()
    {
        Check(IsRealtime({{"state_tracking", "game_epoch"}}), "game epoch tracking is real-time");
        Check(!IsRealtime({{"state_tracking", "public_state"}}), "public state tracking is turn-based");
        Check(!IsRealtime(Json::object()), "missing state tracking is turn-based");
        Check(!IsRealtime({{"state_tracking", 1}}), "non-string state tracking is turn-based");

        const auto failure = [](const char *code)
        {
            return Json{{"ok", false}, {"error", {{"code", code}, {"message", "fixture"}}}};
        };

        Check(ClassifyActResult(Observation(), true) == ActOutcome::Observation, "real-time act observation");
        Check(ClassifyActResult(Observation(), false) == ActOutcome::Observation, "turn-based act observation");
        for (const auto *code : {"stale_state", "busy"})
        {
            Check(ClassifyActResult(failure(code), true) == ActOutcome::Redecide, "real-time games decide again");
            Check(ClassifyActResult(failure(code), false) == ActOutcome::Reject, "turn-based games stay strict");
        }

        for (const auto *code : {"illegal_action", "invalid_request", "callback_failed"})
        {
            Check(ClassifyActResult(failure(code), true) == ActOutcome::Reject, "real errors still end the run");
        }

        Check(ClassifyActResult({{"ok", false}}, true) == ActOutcome::Reject, "missing error code rejected");
        Check(ClassifyActResult({{"ok", false}, {"error", {{"code", 7}}}}, true) == ActOutcome::Reject,
              "numeric error code rejected");
        Check(ClassifyActResult(Json::array(), true) == ActOutcome::Reject, "non-object act result rejected");
    }

    void KeyTests()
    {
        Check(CleanKey("  sk-abc123\r\n") == "sk-abc123", "pasted key trimmed of whitespace and line breaks");
        Check(CleanKey("\tkey\n") == "key", "tabs trimmed");
        Check(!CleanKey(""), "empty clipboard is no key");
        Check(!CleanKey(" \r\n "), "whitespace is no key");
        Check(!CleanKey("two words"), "inner space rejected");
        Check(!CleanKey("tab\tinside"), "inner tab rejected");
        Check(!CleanKey("k\xC3\xA9y"), "non-ASCII rejected");
        Check(!CleanKey(std::string(4097, 'k')), "over 4096 characters rejected");
        Check(CleanKey(std::string(4096, 'k')).has_value(), "4096 characters accepted");
    }
} // namespace

int main()
{
    try
    {
        UrlTests();
        RpcTests();
        ObservationTests();
        JevTests();
        RealtimeTests();
        KeyTests();
        std::cout << "PASS: loopback URLs, MCP JSON/SSE and encoded tools, game observations, local policy, Jev "
                     "fixtures, real-time act outcomes, pasted keys\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
