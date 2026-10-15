#pragma once
#include "ProjectNative.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The game driver's named-action protocol (Phasma/GameDriver) for C++ scripts, ported from Lua's
// gamekit/driver.lua. A game supplies observe, act and an optional reset; a Controller registers game_describe,
// game_observe, game_act and game_reset (ABI 29 registerTool) for its lifetime and validates every request
// before game code runs. Results are JSON text: protocol errors are {"ok":false,"error":{code,message}}.
namespace phasma::gamekit
{
    struct Action
    {
        std::string id;              // 1..128 bytes, unique
        std::string description;     // 1..512 bytes
        std::optional<double> score; // finite; the driver's local policy plays the highest
    };

    struct State
    {
        std::string mode; // 1..128 bytes
        bool terminal = false;
        double score = 0.0; // finite
        bool busy = false;  // an action is still playing out
        // The public game state as JSON object text. Write it in a fixed order: without a stateKey, every change
        // of the public state (this text included) invalidates the observed decision.
        std::string observation = "{}";
        std::vector<Action> actions; // at most 256
    };

    struct Game
    {
        std::string id;          // 1..96 bytes
        std::string description; // 1..2048 bytes
        std::function<void(State &)> observe;
        std::function<void(const std::string &actionId)> act; // only ever called with a legal action id
        std::function<void(uint32_t seed)> reset;             // optional; seeds 0..2147483647
        // Optional, for real-time games: only the logical conditions that invalidate a pending decision
        // (HP, wave, enemy identities), so moving visuals do not.
        std::function<std::string(const State &)> stateKey;
    };

    // A JSON string literal; bytes outside the escapes pass through, so give it UTF-8.
    inline std::string Quote(std::string_view text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += c;
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
                out += escape;
            }
            else
                out += c;
        }
        out += '"';
        return out;
    }

    // A finite number, as short as round-trips.
    inline std::string Number(double value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.15g", value);
        if (std::strtod(text, nullptr) != value)
            std::snprintf(text, sizeof(text), "%.17g", value);
        return text;
    }

    namespace detail
    {
        enum class Kind
        {
            String,
            Integer
        };

        struct Field
        {
            std::string key;
            Kind kind = Kind::String;
            std::string text;     // a String
            uint64_t integer = 0; // a non-negative Integer
        };

        inline const Field *Find(const std::vector<Field> &fields, std::string_view key)
        {
            for (const Field &field : fields)
            {
                if (field.key == key)
                    return &field;
            }
            return nullptr;
        }

        // Reads the flat JSON object a tool call receives. Every protocol field is a string or a non-negative integer,
        // so anything else (nesting, other literals, fractions, duplicate keys) fails the whole request.
        class Reader
        {
        public:
            explicit Reader(std::string_view text) : m_text(text) {}

            bool Object(std::vector<Field> &fields)
            {
                if (!Take('{'))
                    return false;

                if (Take('}'))
                    return End();

                do
                {
                    Field field;
                    const bool entry = String(field.key) && Take(':') && Value(field);
                    if (!entry || Find(fields, field.key))
                        return false;

                    fields.push_back(std::move(field));
                }
                while (Take(','));

                return Take('}') && End();
            }

        private:
            void Space()
            {
                while (m_at < m_text.size() && std::string_view(" \t\r\n").find(m_text[m_at]) != std::string_view::npos)
                    ++m_at;
            }

            bool Take(char c)
            {
                Space();
                if (m_at == m_text.size() || m_text[m_at] != c)
                    return false;

                ++m_at;
                return true;
            }

            bool End()
            {
                Space();
                return m_at == m_text.size();
            }

            bool Value(Field &field)
            {
                Space();
                if (m_at < m_text.size() && m_text[m_at] == '"')
                {
                    field.kind = Kind::String;
                    return String(field.text);
                }

                field.kind = Kind::Integer;
                return Integer(field.integer);
            }

            bool Integer(uint64_t &value)
            {
                const size_t start = m_at;
                while (m_at < m_text.size() && m_text[m_at] >= '0' && m_text[m_at] <= '9')
                {
                    const uint64_t digit = static_cast<uint64_t>(m_text[m_at] - '0');
                    if (value > (UINT64_MAX - digit) / 10)
                        return false;

                    value = value * 10 + digit;
                    ++m_at;
                }

                const size_t digits = m_at - start;
                return digits == 1 || (digits > 1 && m_text[start] != '0');
            }

            bool String(std::string &out)
            {
                if (!Take('"'))
                    return false;

                while (m_at < m_text.size())
                {
                    const char c = m_text[m_at++];
                    if (c == '"')
                        return true;

                    if (static_cast<unsigned char>(c) < 0x20)
                        return false;

                    if (c != '\\')
                        out += c;
                    else if (!Escape(out))
                        return false;
                }
                return false;
            }

            bool Escape(std::string &out)
            {
                if (m_at == m_text.size())
                    return false;

                constexpr std::string_view from = "\"\\/bfnrt";
                constexpr std::string_view to = "\"\\/\b\f\n\r\t";
                const char c = m_text[m_at++];
                const size_t index = from.find(c);
                if (index != std::string_view::npos)
                {
                    out += to[index];
                    return true;
                }
                return c == 'u' && Unicode(out);
            }

            bool Unicode(std::string &out)
            {
                uint32_t code = 0;
                if (!Hex(code) || (code >= 0xDC00 && code <= 0xDFFF))
                    return false;

                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    uint32_t low = 0;
                    if (m_text.substr(m_at, 2) != "\\u")
                        return false;

                    m_at += 2;
                    if (!Hex(low) || low < 0xDC00 || low > 0xDFFF)
                        return false;

                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }

                // UTF-8: a lead byte, then six bits per continuation byte.
                constexpr unsigned char lead[] = {0x00, 0xC0, 0xE0, 0xF0};
                const int extra = code < 0x80 ? 0 : code < 0x800 ? 1
                                                : code < 0x10000 ? 2
                                                                 : 3;
                out += static_cast<char>(lead[extra] | (code >> (6 * extra)));
                for (int i = extra - 1; i >= 0; --i)
                    out += static_cast<char>(0x80 | ((code >> (6 * i)) & 0x3F));
                return true;
            }

            bool Hex(uint32_t &value)
            {
                if (m_text.size() - m_at < 4)
                    return false;

                for (int i = 0; i < 4; ++i)
                {
                    const int digit = HexDigit(m_text[m_at++]);
                    if (digit < 0)
                        return false;

                    value = value * 16 + static_cast<uint32_t>(digit);
                }
                return true;
            }

            static int HexDigit(char c)
            {
                if (c >= '0' && c <= '9')
                    return c - '0';

                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;

                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;

                return -1;
            }

            std::string_view m_text;
            size_t m_at = 0;
        };
    } // namespace detail

    // Declare it after the game state its callbacks use. Not copyable or movable: the host calls it by address.
    class Controller
    {
    public:
        static constexpr uint64_t MaxSeed = 2147483647;
        static constexpr uint64_t MaxState = 9007199254740991; // 2^53 - 1, exact in every client's numbers

        // Throws std::invalid_argument for an invalid game. Without the player MCP server (Android) the game still
        // runs; it just registers no tools.
        Controller(const ScriptApi &api, Game game) : m_world(api), m_game(std::move(game))
        {
            const bool named = Bounded(m_game.id, 96) && Bounded(m_game.description, 2048);
            if (!named || !m_game.observe || !m_game.act)
                throw std::invalid_argument("gamekit: a game needs a 1..96 byte id, a 1..2048 byte description, observe and act");

            NewSession();
            m_world.RegisterTool("game_describe", "Describe this game's named-action protocol.", EmptySchema,
                                 &Tool<&Controller::Describe>, this);
            m_world.RegisterTool("game_observe",
                                 "Observe public game state and legal actions. Parse the returned JSON text.",
                                 EmptySchema,
                                 &Tool<&Controller::Observe>,
                                 this);
            m_world.RegisterTool("game_act", "Apply one legal action against the observed decision state; never retry.",
                                 ActSchema, &Tool<&Controller::Act>, this);
            m_world.RegisterTool("game_reset", "Start a new game session with an explicit seed.", ResetSchema,
                                 &Tool<&Controller::Reset>, this);
        }

        ~Controller()
        {
            for (const char *name : {"game_describe", "game_observe", "game_act", "game_reset"})
                m_world.UnregisterTool(name);
        }

        Controller(const Controller &) = delete;
        Controller &operator=(const Controller &) = delete;

    private:
        static constexpr const char *EmptySchema = R"({"type":"object","additionalProperties":false})";
        static constexpr const char *ActSchema = R"({"type":"object","additionalProperties":false,"required":["session_id","state_id","action_id"],"properties":{"session_id":{"type":"string","minLength":1,"maxLength":256},"state_id":{"type":"integer","minimum":1,"maximum":9007199254740991},"action_id":{"type":"string","minLength":1,"maxLength":128}}})";
        static constexpr const char *ResetSchema = R"({"type":"object","additionalProperties":false,"required":["seed"],"properties":{"seed":{"type":"integer","minimum":0,"maximum":2147483647}}})";

        template <std::string (Controller::*Method)(std::string_view)>
        static uint32_t Tool(void *user, const char *args, const char **result, uint32_t *size) noexcept
        {
            auto *self = static_cast<Controller *>(user);
            try
            {
                self->m_result = (self->*Method)(args ? args : "");
            }
            catch (...)
            {
                return 0;
            }

            *result = self->m_result.data();
            *size = static_cast<uint32_t>(self->m_result.size());
            return 1;
        }

        static bool Bounded(std::string_view text, size_t maximum)
        {
            return !text.empty() && text.size() <= maximum;
        }

        static const char *Bool(bool value)
        {
            return value ? "true" : "false";
        }

        static std::string Failure(std::string_view code, std::string_view message)
        {
            return "{\"ok\":false,\"error\":{\"code\":" + Quote(code) + ",\"message\":" + Quote(message) + "}}";
        }

        // Runs game code; a throw becomes a failure with this code.
        template <class F>
        static bool Guard(std::string &out, const char *code, F &&fn)
        {
            try
            {
                fn();
                return true;
            }
            catch (const std::exception &error)
            {
                out = Failure(code, error.what());
            }
            catch (...)
            {
                out = Failure(code, "the game callback threw");
            }
            return false;
        }

        static bool Only(const std::vector<detail::Field> &fields, std::initializer_list<std::string_view> keys)
        {
            return std::all_of(fields.begin(), fields.end(), [&](const detail::Field &field)
                               { return std::find(keys.begin(), keys.end(), field.key) != keys.end(); });
        }

        static bool IsString(const detail::Field *field, size_t maximum)
        {
            return field && field->kind == detail::Kind::String && Bounded(field->text, maximum);
        }

        static bool IsInteger(const detail::Field *field, uint64_t low, uint64_t high)
        {
            if (!field || field->kind != detail::Kind::Integer)
                return false;

            return field->integer >= low && field->integer <= high;
        }

        // Only its braces are checked: the text is the game's own, and the host parses the whole result.
        static bool IsObject(std::string_view text)
        {
            const size_t first = text.find_first_not_of(" \t\r\n");
            const size_t last = text.find_last_not_of(" \t\r\n");
            return first != std::string_view::npos && text[first] == '{' && text[last] == '}';
        }

        static const char *Check(const State &state)
        {
            if (!Bounded(state.mode, 128) || !std::isfinite(state.score) || !IsObject(state.observation))
                return "observe must give a 1..128 byte mode, a finite score and an observation JSON object";

            if (state.actions.size() > 256)
                return "actions must hold at most 256 named actions";

            std::vector<std::string_view> ids;
            for (const Action &action : state.actions)
            {
                const bool scored = !action.score || std::isfinite(*action.score);
                if (!Bounded(action.id, 128) || !Bounded(action.description, 512) || !scored)
                    return "each action needs a 1..128 byte id, a 1..512 byte description and an optional finite score";

                ids.push_back(action.id);
            }

            std::sort(ids.begin(), ids.end());
            if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
                return "action ids must be unique";

            return nullptr;
        }

        static std::string Encode(const State &state)
        {
            std::string json = "{\"mode\":" + Quote(state.mode);
            json += ",\"terminal\":";
            json += Bool(state.terminal);
            json += ",\"score\":" + Number(state.score);
            json += ",\"busy\":";
            json += Bool(state.busy);
            json += ",\"observation\":" + state.observation;
            json += ",\"actions\":[";
            for (size_t i = 0; i < state.actions.size(); ++i)
            {
                const Action &action = state.actions[i];
                json += i ? ",{\"id\":" : "{\"id\":";
                json += Quote(action.id);
                json += ",\"description\":" + Quote(action.description);
                if (action.score)
                    json += ",\"score\":" + Number(*action.score);
                json += '}';
            }
            json += "]}";
            return json;
        }

        void NewSession()
        {
            static uint64_t sequence = 0; // restarts with the module; the clock keeps ids unique across reloads
            char address[32];
            std::snprintf(address, sizeof(address), "%p", static_cast<void *>(this));
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            m_session = m_game.id + ":" + address + ":" + std::to_string(++sequence) + ":" + std::to_string(ticks);
            m_stateId = 1;
            m_fingerprint.reset();
        }

        // Observes the game into out (false: out is the failure), advancing the state id when the decision state
        // changed and recording what the next act may do.
        bool Refresh(std::string &out)
        {
            State state;
            const bool observed = Guard(out, "callback_failed", [&]
                                        { m_game.observe(state); });
            if (!observed)
                return false;

            if (const char *problem = Check(state))
            {
                out = Failure("invalid_observation", problem);
                return false;
            }

            std::string json = Encode(state);
            std::string fingerprint = json;
            const bool keyed = !m_game.stateKey || Guard(out, "invalid_observation", [&]
                                                         { fingerprint = m_game.stateKey(state); });
            if (!keyed)
                return false;

            if (m_fingerprint && *m_fingerprint != fingerprint)
                ++m_stateId;
            m_fingerprint = std::move(fingerprint);
            m_busy = state.busy;
            m_legal.clear();
            for (const Action &action : state.actions)
                m_legal.push_back(action.id);

            json.pop_back(); // reopen the object for the protocol fields
            out = std::move(json);
            out += ",\"protocol_version\":1,\"game_id\":" + Quote(m_game.id);
            out += ",\"session_id\":" + Quote(m_session);
            out += ",\"state_id\":" + std::to_string(m_stateId) + "}";
            return true;
        }

        std::string Describe(std::string_view)
        {
            std::string out = "{\"protocol_version\":1,\"game_id\":" + Quote(m_game.id);
            out += ",\"description\":" + Quote(m_game.description);
            out += ",\"reset_supported\":";
            out += Bool(m_game.reset != nullptr);
            out += ",\"state_tracking\":";
            out += m_game.stateKey ? "\"game_epoch\"" : "\"public_state\"";
            out += ",\"seed_min\":0,\"seed_max\":" + std::to_string(MaxSeed);
            out += R"(,"tools":["game_describe","game_observe","game_act","game_reset"]})";
            return out;
        }

        std::string Observe(std::string_view)
        {
            std::string out;
            Refresh(out);
            return out;
        }

        std::string Act(std::string_view args)
        {
            std::vector<detail::Field> fields;
            const bool read = detail::Reader(args).Object(fields);
            const bool parsed = read && Only(fields, {"session_id", "state_id", "action_id"});
            const detail::Field *session = detail::Find(fields, "session_id");
            const detail::Field *state = detail::Find(fields, "state_id");
            const detail::Field *action = detail::Find(fields, "action_id");
            const bool ids = IsString(session, 256) && IsString(action, 128);
            if (!parsed || !ids || !IsInteger(state, 1, MaxState))
                return Failure("invalid_request", "act requires session_id, integer state_id and action_id");

            std::string out;
            if (!Refresh(out))
                return out;

            if (session->text != m_session || state->integer != m_stateId)
                return Failure("stale_state", "observe the current session and state before acting; never retry an action");

            if (m_busy)
                return Failure("busy", "the current action is still running; observe until busy is false");

            if (std::find(m_legal.begin(), m_legal.end(), action->text) == m_legal.end())
                return Failure("illegal_action", "action_id is not legal in the observed state");

            // Consume before calling game code: a throw may have already changed the game.
            ++m_stateId;
            m_fingerprint.reset();
            const bool acted = Guard(out, "callback_failed", [&]
                                     { m_game.act(action->text); });
            if (!acted)
                return out;

            Refresh(out);
            return out;
        }

        std::string Reset(std::string_view args)
        {
            std::vector<detail::Field> fields;
            const bool parsed = detail::Reader(args).Object(fields) && Only(fields, {"seed"});
            const detail::Field *seed = detail::Find(fields, "seed");
            if (!parsed || !IsInteger(seed, 0, MaxSeed))
                return Failure("invalid_request", "reset requires an integer seed in 0..2147483647");

            if (!m_game.reset)
                return Failure("unsupported", "this game does not support reset");

            NewSession();
            std::string out;
            const bool reset = Guard(out, "callback_failed", [&]
                                     { m_game.reset(static_cast<uint32_t>(seed->integer)); });
            if (!reset)
                return out;

            Refresh(out);
            return out;
        }

        World m_world;
        Game m_game;
        std::string m_session;
        uint64_t m_stateId = 1;
        std::optional<std::string> m_fingerprint;
        bool m_busy = false;
        std::vector<std::string> m_legal;
        std::string m_result; // the last tool result, which the host copies before calling again
    };
} // namespace phasma::gamekit
