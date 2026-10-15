#include "Http.h"
#include "Vault.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace phasma::driver
{
    namespace
    {
        std::atomic_bool cancelled = false;

        BOOL WINAPI ConsoleControl(DWORD event)
        {
            if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT)
            {
                cancelled.store(true);
                return TRUE;
            }

            return FALSE;
        }

        struct Options
        {
            std::string url = "http://127.0.0.1:8876/mcp";
            std::string policy;
            std::string keySource = "env";
            unsigned int steps = 20;
            unsigned int paceMs = 0;
            std::optional<unsigned int> seed;
            std::filesystem::path trace;
        };

        unsigned int Number(std::string_view text, unsigned int maximum)
        {
            unsigned int value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value > maximum)
            {
                throw std::runtime_error("Invalid numeric command-line argument");
            }

            return value;
        }

        Options Arguments(int argc, char *argv[])
        {
            Options options;
            for (int i = 1; i < argc; ++i)
            {
                const std::string_view name = argv[i];
                if (++i == argc)
                {
                    throw std::runtime_error("Missing command-line argument value");
                }
                const std::string value = argv[i];
                if (name == "--url")
                {
                    options.url = value;
                }
                else if (name == "--policy")
                {
                    options.policy = value;
                }
                else if (name == "--key-source")
                {
                    options.keySource = value;
                }
                else if (name == "--steps")
                {
                    options.steps = Number(value, 10000);
                }
                else if (name == "--pace-ms")
                {
                    options.paceMs = Number(value, 10000);
                }
                else if (name == "--seed")
                {
                    options.seed = Number(value, 2147483647);
                }
                else if (name == "--trace")
                {
                    options.trace = std::filesystem::path(std::u8string(value.begin(), value.end()));
                }
                else
                {
                    throw std::runtime_error("Unknown command-line argument");
                }
            }

            const bool validPolicy = options.policy == "local" || options.policy == "jev";
            const bool validKeySource = options.keySource == "env" || options.keySource == "vault";
            if (!validPolicy || !validKeySource || options.steps == 0)
            {
                throw std::runtime_error(
                    "Use --policy local|jev, --steps 1..10000 (default 20) and --key-source env|vault");
            }

            return options;
        }

        std::filesystem::path DefaultTrace()
        {
            std::array<wchar_t, 32768> executable{};
            const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (length == 0 || length == executable.size())
            {
                throw std::runtime_error("Cannot locate the driver executable");
            }

            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            const auto name =
                "driver_" + std::to_string(milliseconds) + "_" + std::to_string(GetCurrentProcessId()) + ".jsonl";
            return std::filesystem::path(executable.data()).parent_path() / "traces" / name;
        }

        class Trace
        {
        public:
            explicit Trace(const std::filesystem::path &path)
            {
                if (!path.parent_path().empty())
                {
                    std::filesystem::create_directories(path.parent_path());
                }
                stream.open(path, std::ios::app);
                if (!stream)
                {
                    throw std::runtime_error("Cannot open the JSONL trace");
                }
            }

            void Write(Json event)
            {
                event["time_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
                stream << event.dump() << '\n';
                stream.flush();
                if (!stream)
                {
                    throw std::runtime_error("Cannot write the JSONL trace");
                }
            }

        private:
            std::ofstream stream;
        };

        class McpClient
        {
        public:
            explicit McpClient(Endpoint target) : http(std::move(target)) {}

            void Initialize()
            {
                const auto result = Request("initialize",
                                            {{"protocolVersion", "2025-06-18"},
                                             {"capabilities", Json::object()},
                                             {"clientInfo", {{"name", "PhasmaGameDriver"}, {"version", "1.0"}}}});
                protocol = result.at("protocolVersion").get<std::string>();
                if (protocol != "2025-06-18")
                {
                    throw std::runtime_error("Unsupported MCP protocol version");
                }

                Send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
            }

            Json Tool(std::string_view name, const Json &arguments = Json::object())
            {
                return UnwrapToolResult(Request("tools/call", {{"name", name}, {"arguments", arguments}}));
            }

        private:
            HttpResponse Send(const Json &payload)
            {
                std::wstring headers = L"Accept: application/json, text/event-stream\r\n";
                if (!protocol.empty())
                {
                    headers += L"MCP-Protocol-Version: " + std::wstring(protocol.begin(), protocol.end()) + L"\r\n";
                }

                if (!session.empty())
                {
                    headers += L"Mcp-Session-Id: " + std::wstring(session.begin(), session.end()) + L"\r\n";
                }

                auto response = http.Post(payload.dump(), headers, 2 * 1024 * 1024, cancelled);
                if (response.status < 200 || response.status >= 300)
                {
                    throw std::runtime_error("MCP request failed with HTTP " + std::to_string(response.status));
                }

                if (!response.session.empty())
                {
                    if (!session.empty() && session != response.session)
                    {
                        throw std::runtime_error("MCP session changed unexpectedly");
                    }
                    session = response.session;
                }

                return response;
            }

            Json Request(std::string_view method, const Json &params)
            {
                const auto id = nextId++;
                const auto response = Send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
                return DecodeRpc(response.body, response.contentType, id);
            }

            HttpClient http;
            std::string session;
            std::string protocol;
            std::uint64_t nextId = 1;
        };

        std::string ValidateKey(std::string value)
        {
            const auto key = CleanKey(value);
            if (!key || *key != value)
            {
                throw std::runtime_error("API key has an invalid format");
            }
            return value;
        }

        std::string ApiKey()
        {
            for (const auto *name : {"JEV_API_KEY", "TYPESAFE_API_KEY"})
            {
                const DWORD length = GetEnvironmentVariableA(name, nullptr, 0);
                if (length == 0)
                {
                    continue;
                }
                if (length > 4097)
                {
                    throw std::runtime_error("API key has an invalid format");
                }

                std::string value(length, '\0');
                const DWORD written = GetEnvironmentVariableA(name, value.data(), length);
                if (written == 0 || written >= length)
                {
                    throw std::runtime_error("API key environment changed while reading it");
                }
                value.resize(written);
                return ValidateKey(std::move(value));
            }
            throw std::runtime_error("Jev policy requires JEV_API_KEY or TYPESAFE_API_KEY in the environment");
        }

        class JevClient
        {
        public:
            explicit JevClient(const std::string &key) : http({L"api.typesafe.ai", L"/v1/systemone", 443, true})
            {
                headers = L"Accept: application/json\r\nAuthorization: Bearer " + std::wstring(key.begin(), key.end()) +
                          L"\r\n";
            }

            Selection Choose(const Json &description, const Json &observation)
            {
                const auto response =
                    http.Post(MakeJevRequest(description, observation).dump(), headers, 65536, cancelled);
                if (response.status != 200)
                {
                    throw std::runtime_error("Jev request failed with HTTP " + std::to_string(response.status) +
                                             "; no retry was attempted");
                }
                return ParseJevResponse(ParseJson(response.body), observation);
            }

        private:
            HttpClient http;
            std::wstring headers;
        };

        void ValidateSession(const Json &before, const Json &after)
        {
            ValidateObservation(after);
            const bool sameGame = after["game_id"] == before["game_id"];
            const bool sameSession = after["session_id"] == before["session_id"];
            if (!sameGame || !sameSession || after["state_id"] < before["state_id"])
            {
                throw std::runtime_error("Game identity changed or its state moved backwards; reconnect explicitly");
            }
        }

        // Real-time games also pause and sit between pieces with no actions; only Ctrl+C ends that wait.
        Json WaitReady(McpClient &client, Json state, bool realtime)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            const auto waiting = [&]()
            {
                const bool idle = realtime && state["actions"].empty();
                return (state["busy"].get<bool>() || idle) && !state["terminal"].get<bool>();
            };

            while (waiting())
            {
                const bool expired = !realtime && std::chrono::steady_clock::now() > deadline;
                if (cancelled.load() || expired)
                {
                    throw std::runtime_error("Cancelled or game stayed busy for 30 seconds");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                auto next = client.Tool("game_observe");
                ValidateSession(state, next);
                state = std::move(next);
            }
            return state;
        }

        void WaitUntil(std::chrono::steady_clock::time_point deadline)
        {
            while (!cancelled.load() && std::chrono::steady_clock::now() < deadline)
            {
                const auto left = deadline - std::chrono::steady_clock::now();
                const auto slice = std::chrono::milliseconds(50);
                std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(left, slice));
            }
        }

        struct Totals
        {
            unsigned int decisions = 0;
            unsigned int redecides = 0;
            unsigned int calls = 0;
            std::uint64_t input = 0;
            std::uint64_t output = 0;
            std::int64_t decisionMs = 0;
        };

        // The decision expired while it was made (a real-time piece locked): observe the new state and decide again.
        Json Redecide(McpClient &client,
                      const Json &state,
                      const Json &result,
                      const Selection &selected,
                      Trace &trace,
                      Totals &totals)
        {
            ++totals.redecides;
            trace.Write({{"event", "redecide"}, {"action_id", selected.action}, {"error", result["error"]}});
            std::cout << selected.model << " re-decide " << totals.redecides << " after " << result["error"]["code"]
                      << " input_tokens=" << selected.inputTokens << '\n';

            auto current = client.Tool("game_observe");
            ValidateSession(state, current);
            return WaitReady(client, std::move(current), true);
        }

        Json PlayStep(McpClient &client,
                      const Json &description,
                      const Json &state,
                      JevClient *jev,
                      Trace &trace,
                      Totals &totals,
                      bool realtime)
        {
            if (state["actions"].empty())
            {
                throw std::runtime_error("Game is idle but offers no legal actions");
            }

            const auto started = std::chrono::steady_clock::now();
            if (jev)
            {
                ++totals.calls;
            }

            const auto selected = jev ? jev->Choose(description, state) : ChooseLocal(state);
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                    .count();

            totals.input += selected.inputTokens;
            totals.output += selected.outputTokens;
            totals.decisionMs += elapsed;

            trace.Write({{"event", "decision"},
                         {"policy", selected.model},
                         {"state", state},
                         {"action_id", selected.action},
                         {"confidence", selected.confidence},
                         {"duration_ms", elapsed},
                         {"input_tokens", selected.inputTokens},
                         {"output_tokens", selected.outputTokens}});

            auto next = client.Tool(
                "game_act",
                {{"session_id", state["session_id"]}, {"state_id", state["state_id"]}, {"action_id", selected.action}});
            const auto outcome = ClassifyActResult(next, realtime);
            if (outcome == ActOutcome::Redecide)
            {
                return Redecide(client, state, next, selected, trace, totals);
            }

            if (outcome == ActOutcome::Reject)
            {
                const auto error = next.contains("error") ? next["error"].dump() : next.dump();
                throw std::runtime_error("Game rejected the action: " + error.substr(0, 256));
            }

            ValidateSession(state, next);

            ++totals.decisions;
            std::cout << selected.model << " step " << totals.decisions << " action=" << selected.action
                      << " score=" << next["score"] << " decision_ms=" << elapsed
                      << " input_tokens=" << selected.inputTokens << '\n';

            return WaitReady(client, std::move(next), realtime);
        }

        Json InitialState(McpClient &client, const Json &description, const Options &options)
        {
            if (options.seed && !description.value("reset_supported", false))
            {
                throw std::runtime_error("Game does not advertise reset support; omit --seed to attach");
            }

            auto state =
                options.seed ? client.Tool("game_reset", {{"seed", *options.seed}}) : client.Tool("game_observe");
            ValidateObservation(state);

            if (state["game_id"] != description["game_id"])
            {
                throw std::runtime_error("Game observation does not match game_describe.game_id");
            }

            return WaitReady(client, std::move(state), IsRealtime(description));
        }

        void Run(const Options &options, Trace &trace)
        {
            std::optional<JevClient> jev;
            if (options.policy == "jev")
            {
                jev.emplace(options.keySource == "vault" ? ValidateKey(ReadSavedJevKey()) : ApiKey());
            }

            McpClient client(ParseLoopbackUrl(options.url));
            client.Initialize();
            const auto description = client.Tool("game_describe");
            ValidateDescription(description);
            auto state = InitialState(client, description, options);
            trace.Write({{"event", "start"},
                         {"policy", options.policy == "local" ? "LOCAL" : "JEV"},
                         {"description", description},
                         {"steps_limit", options.steps}});

            Totals totals;
            const bool realtime = IsRealtime(description);

            try
            {
                // Every decision is a paid call, so re-decides count toward --steps too.
                while (totals.decisions + totals.redecides < options.steps && !state["terminal"].get<bool>())
                {
                    if (cancelled.load())
                    {
                        throw std::runtime_error("Cancelled");
                    }
                    const auto stepStart = std::chrono::steady_clock::now();
                    state = PlayStep(client, description, state, jev ? &*jev : nullptr, trace, totals, realtime);
                    WaitUntil(stepStart + std::chrono::milliseconds(options.paceMs));

                    // A real-time game kept running through the pace wait: decide on what it shows now.
                    if (realtime && options.paceMs > 0)
                    {
                        auto current = client.Tool("game_observe");
                        ValidateSession(state, current);
                        state = WaitReady(client, std::move(current), true);
                    }
                }
            }
            catch (...)
            {
                trace.Write({{"event", "interrupted"},
                             {"api_calls", totals.calls},
                             {"input_tokens", totals.input},
                             {"output_tokens", totals.output}});
                throw;
            }

            trace.Write({{"event", "finish"},
                         {"decisions", totals.decisions},
                         {"redecides", totals.redecides},
                         {"api_calls", totals.calls},
                         {"input_tokens", totals.input},
                         {"output_tokens", totals.output},
                         {"decision_ms", totals.decisionMs},
                         {"terminal", state["terminal"]},
                         {"score", state["score"]}});

            std::cout << "Finished: decisions=" << totals.decisions << " redecides=" << totals.redecides
                      << " api_calls=" << totals.calls
                      << " input_tokens=" << totals.input << " output_tokens=" << totals.output
                      << " terminal=" << state["terminal"] << " score=" << state["score"] << '\n';
        }

        // Unicode clipboard text, left on the clipboard. Another program may hold the clipboard for a moment.
        std::optional<std::string> ReadClipboardText()
        {
            bool open = false;
            for (int attempt = 0; attempt < 10 && !open; ++attempt)
            {
                open = OpenClipboard(nullptr) != FALSE;
                if (!open)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            if (!open)
            {
                return std::nullopt;
            }

            std::optional<std::string> text;
            if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
            {
                if (const auto *wide = static_cast<const wchar_t *>(GlobalLock(data)))
                {
                    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
                    std::string utf8(size > 0 ? size : 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), size, nullptr, nullptr);
                    utf8.resize(size > 0 ? size - 1 : 0);
                    text = std::move(utf8);
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
            return text;
        }

        // `--key-status`, `--save-key-from-clipboard` or `--remove-key`: one "KEY <word>" line, never the key.
        int RunKeyCommand(std::string_view command)
        {
            try
            {
                if (command == "--key-status")
                {
                    std::cout << (HasSavedJevKey() ? "KEY saved" : "KEY missing") << '\n';
                    return 0;
                }

                if (command == "--remove-key")
                {
                    std::cout << (RemoveJevKey() ? "KEY removed" : "KEY missing") << '\n';
                    return 0;
                }

                const auto text = ReadClipboardText();
                const bool blank = !text || text->find_first_not_of(" \t\r\n") == std::string::npos;
                const auto key = blank ? std::nullopt : CleanKey(*text);
                if (blank)
                {
                    std::cout << "KEY clipboard-empty\n";
                    return 0;
                }

                if (!key)
                {
                    std::cout << "KEY invalid\n";
                    return 0;
                }

                SaveJevKey(*key);
                std::cout << "KEY saved\n";
                return 0;
            }
            catch (...)
            {
                // WinRT vault errors are not std::exception; any failure is one line, never a crash.
                std::cout << "KEY error\n";
                return 1;
            }
        }
    } // namespace
} // namespace phasma::driver

int main(int argc, char *argv[])
{
    using namespace phasma::driver;

    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "PhasmaGameDriver --policy local|jev [--url http://127.0.0.1:8876/mcp]\n"
                     "                 [--steps 20] [--seed N] [--trace output.jsonl] [--key-source env|vault]\n"
                     "                 [--pace-ms 0]\n"
                     "local uses adapter scores and makes zero model calls.\n"
                     "jev uses TypeSafe billing and requires JEV_API_KEY or TYPESAFE_API_KEY.\n"
                     "--key-source vault reads the saved Web Credential jev/api-key in-process instead.\n"
                     "--steps N caps decisions (each a paid Jev call), re-decides of a real-time game included.\n"
                     "--pace-ms N makes each placement take at least N ms (0..10000, default 0) for human-like play;\n"
                     "a real-time game keeps running meanwhile, so the driver observes again before deciding.\n"
                     "--seed resets the game after protocol verification; omitted means attach.\n"
                     "Ctrl+C cancels after the current bounded WinHTTP wait. No automatic API retry.\n";
        return 0;
    }
    SetConsoleCtrlHandler(ConsoleControl, TRUE);
    // A game that starts the driver reads its step lines live; a pipe would otherwise hold them until exit.
    std::cout << std::unitbuf;

    // Key commands stand alone: with any other argument they are not run.
    const std::string_view first = argc == 2 ? argv[1] : "";
    const bool keyCommand = first == "--key-status" || first == "--save-key-from-clipboard" || first == "--remove-key";
    if (keyCommand)
    {
        return RunKeyCommand(first);
    }

    try
    {
        auto options = Arguments(argc, argv);
        ParseLoopbackUrl(options.url);
        if (options.trace.empty())
        {
            options.trace = DefaultTrace();
        }
        Trace trace(options.trace);
        std::cout << "Trace: " << options.trace.string() << '\n';
        Run(options, trace);
        return 0;
    }
    catch (const Json::exception &)
    {
        std::cerr << "Game driver failed: unexpected JSON response schema\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "Game driver failed: " << error.what() << '\n';
    }

    return cancelled.load() ? 130 : 1;
}
