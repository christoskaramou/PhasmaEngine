#pragma once

#include <sol/sol.hpp>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace pe
{
    // Registry of MCP tools registered from game scripts: Lua (mcp.register_tool{...}) and C++ (ScriptApi
    // registerTool). A running script registers on the MAIN thread; the player's MCP server (PlayerMcp) lists
    // them on its worker thread and invokes them back on the main thread via the host action queue. Handlers
    // are only ever touched on the main thread (registration + invocation); the worker thread reads only the
    // name/description/schema metadata, under the mutex.
    class AgentToolRegistry
    {
    public:
        struct Meta
        {
            std::string name;
            std::string description;
            nlohmann::json inputSchema;
        };

        // A C++ script's tool: returns the result JSON, or sets ok=false and fills error.
        using NativeHandler = std::function<nlohmann::json(const nlohmann::json &args, bool &ok, std::string &error)>;

        static AgentToolRegistry &Instance();

        // Main thread (from Lua). Replaces any existing tool with the same name.
        void Register(std::string name, std::string description, nlohmann::json inputSchema,
                      sol::protected_function handler);
        void Unregister(const std::string &name);
        // Main thread (from C++ scripts). owner (nonzero) is the registering script instance; same-name
        // replacement as Register.
        void RegisterNative(std::string name, std::string description, nlohmann::json inputSchema,
                            NativeHandler handler, uint64_t owner);
        bool UnregisterNative(const std::string &name, uint64_t owner);
        // Drop owner's C++ tools (0: every C++ tool) before that code can go away.
        void ClearNative(uint64_t owner);
        // Drop every Lua tool — called on Lua teardown/reload so stale sol handles never outlive their Lua
        // state. C++ tools survive: their scripts survive a Lua reload.
        void ClearLua();

        // Worker thread: metadata snapshot for tools/list. Never touches a sol::function.
        std::vector<Meta> SnapshotMeta() const;

        // Main thread only (inside the queued action): invoke a registered tool by name. Marshals the
        // JSON args to a Lua table, calls the handler, and marshals its return back to JSON. On failure
        // sets ok=false and fills error.
        nlohmann::json Invoke(sol::state_view lua, const std::string &name, const nlohmann::json &args,
                              bool &ok, std::string &error) const;

    private:
        struct Entry
        {
            std::string name;
            std::string description;
            nlohmann::json inputSchema;
            sol::protected_function handler;
            NativeHandler native;
            uint64_t owner = 0; // nonzero for C++ tools
        };

        mutable std::mutex m_mtx;
        std::vector<Entry> m_tools;
    };

    // JSON <-> Lua value marshaling (sol2). Used by the registry and the player MCP tool layer.
    sol::object JsonToLua(sol::state_view lua, const nlohmann::json &value);
    nlohmann::json LuaToJson(const sol::object &value);
} // namespace pe
